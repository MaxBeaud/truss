module;
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <expected>
#include <filesystem>
#include <poll.h>
#include <span>
#include <string>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

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

/* Spawn one child and wait it out. capture_mode: 0 = none, 1 = stdout+stderr
 * merged, 2 = stdout only. timeout_seconds: wall-clock budget from spawn
 * (0 = none) — on expiry the child's whole process group is SIGKILLed and a
 * Command error is returned.
 *
 * Children run in their own process group (posix_spawnattr setpgroup), so the
 * timeout kill reaches the whole tree: the grandchild that inherited the
 * capture pipe and outlived its parent would otherwise wedge the reader
 * forever. A daemon that setsid()s away escapes the group but detaches its
 * stdio first, which releases the pipe; the deadline is the backstop. */
Result<ProcOutcome> run_impl(std::span<const std::string> argv, int capture_mode,
                             const std::filesystem::path& cwd, int timeout_seconds) {
    if (argv.empty())
        return std::unexpected(Error {Error::Kind::Config, "cannot run an empty command"});

    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& arg : argv)
        cargv.push_back(const_cast<char*>(arg.c_str()));
    cargv.push_back(nullptr);

    ProcOutcome outcome;
    struct pipe_guard {
        int fds[2] {-1, -1};
        void close_read() {
            if (fds[0] != -1) {
                ::close(fds[0]);
                fds[0] = -1;
            }
        }
        void close_write() {
            if (fds[1] != -1) {
                ::close(fds[1]);
                fds[1] = -1;
            }
        }
        ~pipe_guard() {
            close_read();
            close_write();
        }
        pipe_guard() = default;
        pipe_guard(const pipe_guard&) = delete;
        pipe_guard& operator=(const pipe_guard&) = delete;
    } pipe;
    int (&pipe_fd)[2] = pipe.fds;
    /* RAII over posix_spawn_file_actions_t / posix_spawnattr_t: every early
     * return below stays leak-proof without hand-written destroy calls */
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

    posix_spawnattr_t attr;
    if (posix_spawnattr_init(&attr) != 0)
        return std::unexpected(Error {Error::Kind::Io, "cannot init spawn attributes"});
    struct attr_guard {
        posix_spawnattr_t& attr;
        ~attr_guard() { posix_spawnattr_destroy(&attr); }
    } attr_guard {attr};
    posix_spawnattr_setpgroup(&attr, 0); /* 0 = the child becomes its own group leader */
    if (posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP) != 0)
        return std::unexpected(Error {Error::Kind::Io, "cannot set spawn process group"});

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
    const int rc = posix_spawnp(&pid, cargv[0], &actions, &attr, cargv.data(), environ);

    if (rc != 0) {
        return std::unexpected(Error {
            Error::Kind::Command,
            std::string("failed to spawn '") + cargv[0] + "': " + std::strerror(rc),
        });
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(timeout_seconds > 0 ? timeout_seconds : 0);
    const auto expired = [&] {
        return timeout_seconds > 0 && std::chrono::steady_clock::now() >= deadline;
    };
    const auto kill_group = [&] {
        ::kill(-pid, SIGKILL); /* the child's process group (grandchildren too) */
        ::kill(pid, SIGKILL);  /* belt and braces if the group assignment failed */
    };
    /* reap without blocking where possible; the child's exit status lands in
     * outcome.exit_code. Returns true once reaped. A waitpid failure (other
     * than EINTR or a plain "still running") yields exit_code -1. */
    const auto reap = [&](bool block) -> bool {
        int status = 0;
        pid_t waited = 0;
        do {
            waited = waitpid(pid, &status, block ? 0 : WNOHANG);
        } while (waited < 0 && errno == EINTR && block);
        if (waited < 0) {
            if (errno == EINTR)
                return false; /* non-blocking poll interrupted: retry on next tick */
            outcome.exit_code = -1; /* waitpid failed outright: no reliable status */
            return true;
        }
        if (waited == 0)
            return false;
        if (WIFEXITED(status)) outcome.exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) outcome.exit_code = 128 + WTERMSIG(status);
        else outcome.exit_code = -1;
        return true;
    };

    bool reaped = false;
    bool timed_out = false;
    if (capture_mode && pipe_fd[1] != -1) {
        pipe.close_write();
        std::array<char, 64 * 1024> buf; /* 64 KiB: fewer read syscalls on chatty captures */
        /* a runaway child must not grow the buffer without end: past the cap
         * the pipe is drained (so the child never blocks) but dropped */
        constexpr std::size_t kMaxCapture = 16u << 20;
        for (;;) {
            /* poll in short slices: EOF ends the loop, the deadline stays
             * honest, and the direct child is reaped as soon as it exits */
            pollfd pfd {pipe_fd[0], POLLIN, 0};
            const int ready = ::poll(&pfd, 1, 100);
            if (ready > 0) {
                const ssize_t n = read(pipe_fd[0], buf.data(), buf.size());
                if (n == 0)
                    break; /* EOF: every writer is gone */
                if (n < 0 && errno != EINTR)
                    break;
                if (n > 0 && outcome.output.size() < kMaxCapture)
                    outcome.output.append(buf.data(), static_cast<std::size_t>(n));
            } else if (ready < 0 && errno != EINTR) {
                break; /* poll failed: stop reading, reap below */
            }
            if (!reaped)
                reaped = reap(false); /* re-calling reap after success hits
                                       * ECHILD and clobbers the exit code */
            if (expired()) {
                /* kill the group exactly once, then leave: re-killing a
                 * possibly-reaped pid could signal a recycled one */
                timed_out = true;
                if (!reaped)
                    kill_group();
                break;
            }
        }
        if (timed_out) {
            /* the killed writers close the pipe; drain the tail under a short
             * bounded grace — a setsid escapee still holding the write end
             * must not wedge the drain (it would defeat the timeout) */
            const auto drain_deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(2);
            for (;;) {
                pollfd pfd {pipe_fd[0], POLLIN, 0};
                if (::poll(&pfd, 1, 100) <= 0) {
                    if (std::chrono::steady_clock::now() >= drain_deadline)
                        break;
                    continue;
                }
                const ssize_t n = read(pipe_fd[0], buf.data(), buf.size());
                if (n <= 0)
                    break;
                if (outcome.output.size() < kMaxCapture)
                    outcome.output.append(buf.data(), static_cast<std::size_t>(n));
            }
        }
        pipe.close_read();
    } else if (timeout_seconds <= 0) {
        reaped = reap(true); /* no deadline: a plain blocking wait, no polling */
    } else {
        /* Linux pidfd_open + poll wakes immediately when the child exits with
         * zero polling sleep; fall back to nanosleep if unavailable. */
        int pidfd = -1;
#if defined(__linux__) && defined(SYS_pidfd_open)
        pidfd = static_cast<int>(::syscall(SYS_pidfd_open, pid, 0u));
#endif
        for (;;) {
            reaped = reap(false);
            if (reaped)
                break;
            if (expired()) {
                timed_out = true;
                kill_group();
                reaped = reap(true); /* SIGKILL: immediate */
                break;
            }
            if (pidfd >= 0) {
                const auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()).count();
                const int wait_ms = rem <= 0 ? 0 : (rem > 1000 ? 1000 : static_cast<int>(rem));
                pollfd pfd {pidfd, POLLIN, 0};
                ::poll(&pfd, 1, wait_ms);
            } else {
                const struct timespec ts {0, 20 * 1000 * 1000};
                ::nanosleep(&ts, nullptr);
            }
        }
        if (pidfd >= 0)
            ::close(pidfd);
    }
    if (!reaped)
        reap(true); /* blocking: the child has exited (or was just SIGKILLed) */

    if (timed_out)
        return std::unexpected(Error {
            Error::Kind::Command,
            std::string("'") + cargv[0] + "' timed out after " +
                std::to_string(timeout_seconds) + "s (process group killed)",
        });
    return outcome;
}

} // namespace

Result<ProcOutcome> run_process(std::span<const std::string> argv, bool capture, int timeout_seconds) {
    return run_impl(argv, capture ? 1 : 0, {}, timeout_seconds);
}

Result<ProcOutcome> run_process(std::span<const std::string> argv, bool capture,
                                const std::filesystem::path& cwd, int timeout_seconds) {
    return run_impl(argv, capture ? 1 : 0, cwd, timeout_seconds);
}

Result<ProcOutcome> run_process_stdout(std::span<const std::string> argv, int timeout_seconds) {
    return run_impl(argv, 2, {}, timeout_seconds);
}

} // namespace truss::detail
