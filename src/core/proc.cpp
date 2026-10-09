module;
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <expected>
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

Result<ProcOutcome> run_impl(std::span<const std::string> argv, int capture_mode) {
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
    posix_spawn_file_actions_t actions;
    const bool have_actions = posix_spawn_file_actions_init(&actions) == 0;

    if (capture_mode && have_actions && pipe(pipe_fd) == 0) {
        posix_spawn_file_actions_adddup2(&actions, pipe_fd[1], STDOUT_FILENO);
        if (capture_mode == 1)
            posix_spawn_file_actions_adddup2(&actions, pipe_fd[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipe_fd[0]);
        posix_spawn_file_actions_addclose(&actions, pipe_fd[1]);
    }

    pid_t pid {};
    /* spawnp: search PATH like a shell, matching ninja/samu job semantics */
    const int rc = posix_spawnp(&pid, cargv[0], have_actions ? &actions : nullptr, nullptr, cargv.data(), environ);
    if (have_actions)
        posix_spawn_file_actions_destroy(&actions);

    if (rc != 0) {
        if (pipe_fd[0] != -1) { close(pipe_fd[0]); close(pipe_fd[1]); }
        return std::unexpected(Error {
            Error::Kind::Command,
            std::string("failed to spawn '") + cargv[0] + "': " + std::strerror(rc),
        });
    }

    if (capture_mode && pipe_fd[1] != -1) {
        close(pipe_fd[1]);
        std::array<char, 4096> buf {};
        for (;;) {
            const ssize_t n = read(pipe_fd[0], buf.data(), buf.size());
            if (n > 0) outcome.output.append(buf.data(), static_cast<size_t>(n));
            else if (n == 0) break;
            else if (errno != EINTR) break;
        }
        close(pipe_fd[0]);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(status)) outcome.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) outcome.exit_code = 128 + WTERMSIG(status);
    else outcome.exit_code = -1;

    return outcome;
}

} // namespace

Result<ProcOutcome> run_process(std::span<const std::string> argv, bool capture) {
    return run_impl(argv, capture ? 1 : 0);
}

Result<ProcOutcome> run_process_stdout(std::span<const std::string> argv) {
    return run_impl(argv, 2);
}

bool program_exists(std::string_view name) {
    const std::array<std::string, 3> argv {"sh", "-c", "command -v " + std::string(name) + " >/dev/null 2>&1"};
    const auto outcome = run_process(argv, true);
    return outcome && outcome->exit_code == 0;
}

} // namespace truss::detail
