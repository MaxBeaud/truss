module;
#include <array>
#include <cerrno>
#include <cstring>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

module truss;
import :api;
import :impl;

namespace truss::detail {
namespace {

int cloexec_pipe(int fds[2]) {
#if defined(__APPLE__)
    /* no pipe2 on macOS: the fork-window race is acceptable there (truss's
     * concurrent spawners are Linux-first; posix_spawn on macOS is atomic) */
    if (pipe(fds) != 0)
        return -1;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return 0;
#else
    return pipe2(fds, O_CLOEXEC);
#endif
}

Result<ProcOutcome> run_impl(std::span<const std::string> argv, int capture_mode,
                             const std::filesystem::path& cwd) {
    /* capture_mode: 0 = none, 1 = stdout+stderr merged, 2 = stdout only */
    if (argv.empty())
        return std::unexpected(Error {Error::Kind::Config, "cannot run an empty command"});

    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& arg : argv)
        cargv.push_back(const_cast<char*>(arg.c_str()));
    cargv.push_back(nullptr);

    ProcOutcome outcome;
    int pipe_fd[2] = {-1, -1};
    /* RAII over posix_spawn_file_actions_t: every early return below stays
     * leak-proof without hand-written destroy calls */
    struct actions_guard {
        posix_spawn_file_actions_t actions {};
        bool valid {false};
        actions_guard() {
            valid = posix_spawn_file_actions_init(&actions) == 0;
        }
        ~actions_guard() {
            if (valid)
                posix_spawn_file_actions_destroy(&actions);
        }
        actions_guard(const actions_guard&) = delete;
        actions_guard& operator=(const actions_guard&) = delete;
    } guard;
    /* without file actions neither the cwd nor the capture dup2 can be
     * honoured — fail loudly (init only fails on OOM) instead of silently
     * running in the wrong directory or dropping the requested capture */
    if (!guard.valid)
        return std::unexpected(Error {Error::Kind::Io, "cannot init spawn file actions"});
    auto& actions = guard.actions;

    if (!cwd.empty()) {
        const int chdir_rc = posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
        if (chdir_rc != 0) /* these actions return an error number, not -1 */
            return std::unexpected(Error {
                Error::Kind::Io,
                "cannot spawn in cwd '" + path_str(cwd) + "': " + std::strerror(chdir_rc),
            });
    }

    /* O_CLOEXEC: fetch_all and scan-deps spawn from several threads at once;
     * an inheritable pipe end leaks into the sibling children, which then hold
     * the write end open and delay this reader's EOF until they exit. The
     * dup2 file action clears the flag on the child's stdout/stderr copy. */
    if (capture_mode && cloexec_pipe(pipe_fd) != 0)
        return std::unexpected(Error {Error::Kind::Io, std::string("cannot create capture pipe: ") + std::strerror(errno)});
    if (capture_mode) {
        /* these actions return error numbers like addchdir: a silent failure
         * would run the child without the requested redirect/close */
        if (int rc = posix_spawn_file_actions_adddup2(&actions, pipe_fd[1], STDOUT_FILENO); rc != 0)
            return std::unexpected(Error {Error::Kind::Io, std::string("cannot dup2 stdout: ") + std::strerror(rc)});
        if (capture_mode == 1) {
            if (int rc = posix_spawn_file_actions_adddup2(&actions, pipe_fd[1], STDERR_FILENO); rc != 0)
                return std::unexpected(Error {Error::Kind::Io, std::string("cannot dup2 stderr: ") + std::strerror(rc)});
        }
        if (int rc = posix_spawn_file_actions_addclose(&actions, pipe_fd[0]); rc != 0)
            return std::unexpected(Error {Error::Kind::Io, std::string("cannot close pipe read end: ") + std::strerror(rc)});
        if (int rc = posix_spawn_file_actions_addclose(&actions, pipe_fd[1]); rc != 0)
            return std::unexpected(Error {Error::Kind::Io, std::string("cannot close pipe write end: ") + std::strerror(rc)});
    }

    pid_t pid {};
    /* spawnp: search PATH like a shell, matching ninja/samu job semantics */
    const int rc = posix_spawnp(&pid, cargv[0], &actions, nullptr, cargv.data(), environ);

    if (rc != 0) {
        if (pipe_fd[0] != -1) { close(pipe_fd[0]); close(pipe_fd[1]); }
        return std::unexpected(Error {
            Error::Kind::Command,
            std::string("failed to spawn '") + cargv[0] + "': " + std::strerror(rc),
        });
    }

    if (capture_mode && pipe_fd[1] != -1) {
        close(pipe_fd[1]);
        std::array<char, 64 * 1024> buf; /* 64 KiB: fewer read syscalls on chatty captures */
        /* a runaway child must not grow the buffer without end: past the cap
         * the pipe is drained (so the child never blocks) but dropped */
        constexpr std::size_t kMaxCapture = 16u << 20;
        for (;;) {
            const ssize_t n = read(pipe_fd[0], buf.data(), buf.size());
            if (n > 0) {
                if (outcome.output.size() < kMaxCapture)
                    outcome.output.append(buf.data(), static_cast<size_t>(n));
            }
            else if (n == 0) break;
            else if (errno != EINTR) break;
        }
        close(pipe_fd[0]);
    }

    int status = 0;
    int waited = 0;
    while ((waited = waitpid(pid, &status, 0)) < 0 && errno == EINTR) {}
    if (waited < 0)
        outcome.exit_code = -1; /* waitpid failed outright: no reliable status */
    else if (WIFEXITED(status)) outcome.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) outcome.exit_code = 128 + WTERMSIG(status);
    else outcome.exit_code = -1;

    return outcome;
}

} // namespace

Result<ProcOutcome> run_process(std::span<const std::string> argv, bool capture) {
    return run_impl(argv, capture ? 1 : 0, {});
}

Result<ProcOutcome> run_process(std::span<const std::string> argv, bool capture,
                                const std::filesystem::path& cwd) {
    return run_impl(argv, capture ? 1 : 0, cwd);
}

Result<ProcOutcome> run_process_stdout(std::span<const std::string> argv) {
    return run_impl(argv, 2, {});
}

} // namespace truss::detail
