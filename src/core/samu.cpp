module;
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../samu/truss_samu.h"

module truss;
import :api;
import :impl;

namespace truss::detail {

namespace {

/* Rendering of samurai's streamed progress when stdout is a terminal.
 *
 * samurai prints one line per job START (`[started/total] description`) and
 * flushes a job's captured output when it FINISHES. A live block is drawn
 * with the jobs currently in flight (approximated FIFO: the oldest start is
 * evicted when a new one arrives), so finished files disappear instead of
 * scrolling away like ninja/cargo. Any other line (compiler diagnostics,
 * warnings) clears the block, prints verbatim, and lets the next start
 * redraw.
 *
 * Everything is best-effort presentation: if anything about the pump fails,
 * the caller falls back to samurai's plain output. */
class ProgressView {
public:
    ProgressView(int pipe_read, int out_fd, std::size_t max_lines)
        : in_(pipe_read), out_(out_fd), max_lines_(std::max<std::size_t>(1, max_lines)) {}

    void run() {
        std::string pending;
        std::size_t cursor = 0;
        char buf[8192];
        for (;;) {
            const ssize_t n = ::read(in_, buf, sizeof buf);
            if (n > 0) {
                pending.append(buf, static_cast<std::size_t>(n));
                std::size_t nl = 0;
                while ((nl = pending.find('\n', cursor)) != std::string::npos) {
                    line(std::string_view(pending).substr(cursor, nl - cursor));
                    cursor = nl + 1;
                }
                if (cursor == pending.size()) {
                    pending.clear();
                    cursor = 0;
                } else if (cursor >= 4096) {
                    pending.erase(0, cursor);
                    cursor = 0;
                }
                continue;
            }
            if (n < 0 && errno == EINTR)
                continue;
            break;
        }
        if (cursor < pending.size())
            line(std::string_view(pending).substr(cursor));
        std::string fin;
        append_clear(fin);
        if (cursor_hidden_) {
            fin += "\x1b[?25h"; /* show the cursor again */
            cursor_hidden_ = false;
        }
        emit(fin);
    }

private:
    void emit(std::string_view s) {
        while (!s.empty()) {
            const ssize_t n = ::write(out_, s.data(), s.size());
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                return;
            }
            if (n == 0)
                return;
            s.remove_prefix(static_cast<std::size_t>(n));
        }
    }

    std::size_t term_cols() const {
        struct winsize ws {};
        if (::ioctl(out_, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 20)
            return static_cast<std::size_t>(ws.ws_col);
        return 80;
    }

    void append_clear(std::string& frame) {
        if (drawn_ == 0)
            return;
        frame += '\r';
        for (std::size_t i = 0; i < drawn_; ++i)
            frame += "\x1b[1A";
        frame += "\x1b[J"; /* erase the block, leave earlier output alone */
        drawn_ = 0;
    }

    static bool is_status(std::string_view s, std::size_t& started, std::size_t& total,
                          std::string_view& desc) {
        if (s.size() < 5 || s.front() != '[')
            return false;
        const auto close = s.find(']');
        if (close == std::string_view::npos)
            return false;
        const auto slash = s.substr(1, close - 1).find('/');
        if (slash == std::string_view::npos)
            return false;
        const auto num = [&](std::string_view t, std::size_t& out) {
            if (t.empty())
                return false;
            std::size_t v = 0;
            for (char c : t) {
                if (c < '0' || c > '9')
                    return false;
                v = v * 10 + static_cast<std::size_t>(c - '0');
            }
            out = v;
            return true;
        };
        const auto bracket = s.substr(1, close - 1);
        if (!num(bracket.substr(0, slash), started) || !num(bracket.substr(slash + 1), total))
            return false;
        desc = s.substr(close + 1);
        while (!desc.empty() && desc.front() == ' ')
            desc.remove_prefix(1);
        return true;
    }

    void line(std::string_view s) {
        std::size_t started = 0, total = 0;
        std::string_view desc;
        if (!is_status(s, started, total, desc)) {
            /* job output / any foreign line: never hide it behind the block */
            std::string frame;
            append_clear(frame);
            frame.append(s);
            frame.push_back('\n');
            emit(frame);
            return;
        }
        in_flight_.emplace_back(desc);
        if (in_flight_.size() > max_lines_)
            in_flight_.pop_front();
        started_ = started;
        total_ = total;
        redraw();
    }

    void redraw() {
        const std::size_t cols = term_cols();
        /* prefix "  ▸ " is 4 columns wide; keep every job line single-row so
         * \x1b[1A cursor math stays exact even on narrow terminals */
        const std::size_t max_job = cols > 5 ? cols - 5 : 15;
        std::string frame;
        frame.reserve(256 + in_flight_.size() * 80);
        frame += "\x1b[?2026h"; /* DEC synchronized output: single flicker-free frame */
        append_clear(frame);
        frame += "  \x1b[2m[\x1b[0m" + std::to_string(started_) + "/"
               + std::to_string(total_) + "\x1b[2m]\x1b[0m ";
        if (total_ != 0) {
            const std::size_t pct = (100 * started_) / total_;
            frame += "\x1b[2;36m" + std::to_string(pct) + "%\x1b[0m ";
        }
        frame += '\n';
        ++drawn_;
        for (const auto& job : in_flight_) {
            frame += "  \x1b[2m\u25b8\x1b[0m ";
            if (job.size() > max_job) {
                frame.append(job, 0, max_job - 1);
                frame += "\u2026";
            } else {
                frame += job;
            }
            frame += '\n';
            ++drawn_;
        }
        if (!cursor_hidden_) {
            frame += "\x1b[?25l"; /* hide the cursor while the block is live */
            cursor_hidden_ = true;
        }
        frame += "\x1b[?2026l";
        emit(frame);
    }

    int in_ {};
    int out_ {};
    std::size_t max_lines_ {};
    std::deque<std::string> in_flight_;
    std::size_t started_ {};
    std::size_t total_ {};
    std::size_t drawn_ {};
    bool cursor_hidden_ {};
};

int samu_cloexec_pipe(int fds[2]) {
#if defined(__APPLE__)
    if (::pipe(fds) != 0)
        return -1;
    ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return 0;
#else
    return ::pipe2(fds, O_CLOEXEC);
#endif
}

/* TRUSS_PROGRESS=plain disables the live block (also implied by -v, -n,
 * --explain and by stdout not being a terminal). */
bool live_progress_wanted(bool verbose, bool dry_run, bool explain) {
    if (verbose || dry_run || explain)
        return false;
    if (const char* v = std::getenv("TRUSS_PROGRESS"); v != nullptr)
        return std::string_view(v) != "plain";
    return ::isatty(STDOUT_FILENO) != 0;
}

} // namespace

Result<void> samu_build(std::string_view manifest, int jobs, bool verbose,
                        bool dry_run, bool explain, std::span<const std::string> targets) {
    std::vector<const char*> ctargets;
    ctargets.reserve(targets.size());
    for (const auto& t : targets)
        ctargets.push_back(t.c_str());

    const std::string manifest_str(manifest);
    truss_samu_options opts {};
    opts.manifest = manifest_str.c_str();
    /* "[started/total] " (samurai's default is "[started/total] " too, but
     * spelling it out keeps the parser above and samurai in sync) */
    opts.statusfmt = "[%s/%t] ";
    opts.maxjobs = jobs;
    opts.maxfail = 0; /* samu default: stop at first failure */
    opts.verbose = verbose ? 1 : 0;
    opts.dryrun = dry_run ? 1 : 0;
    opts.explain = explain ? 1 : 0;
    opts.ntargets = static_cast<int>(ctargets.size());
    opts.targets = ctargets.empty() ? nullptr : ctargets.data();

    const bool live = live_progress_wanted(verbose, dry_run, explain);
    if (!live) {
        /* NOTE: samurai's fatal() exits the process on hard errors (unknown
         * target, corrupt logs). Build *job* failures make build() report and
         * return; truss treats a non-zero truss_samu_build as failure. */
        if (const int rc = truss_samu_build(&opts); rc != 0)
            return std::unexpected(Error {Error::Kind::Command, "samurai build failed", std::filesystem::path(manifest_str)});
        return {};
    }

    /* live view: samurai's stdout goes through a pipe pumped by a reader
     * thread; our real stdout is saved and restored afterwards. Both `saved`
     * and the pipe ends are CLOEXEC so samurai's compiler children never
     * inherit them (dup2 clears CLOEXEC on fd 1 for the write end). */
    std::fflush(stdout);
    const int saved = ::fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0);
    int fds[2] {-1, -1};
    if (saved < 0 || samu_cloexec_pipe(fds) != 0) {
        if (saved >= 0)
            ::close(saved);
        return std::unexpected(Error {Error::Kind::Io, "cannot create the progress pipe"});
    }
    if (::dup2(fds[1], STDOUT_FILENO) < 0) {
        ::close(saved);
        ::close(fds[0]);
        ::close(fds[1]);
        return std::unexpected(Error {Error::Kind::Io, "cannot redirect build output"});
    }
    ::close(fds[1]); /* fd 1 is the write end now */

    int rc = -1;
    {
        std::jthread pump([&](std::stop_token) {
            ProgressView view(fds[0], saved, static_cast<std::size_t>(jobs <= 0 ? 5 : std::min(jobs, 5)));
            view.run();
        });
        rc = truss_samu_build(&opts);
        std::fflush(stdout);      /* push samurai's buffered lines into the pipe */
        ::dup2(saved, STDOUT_FILENO); /* closes the write end -> delivers EOF to pump */
        /* jthread joins here; it drains the pipe to EOF before we close fds[0] */
    }
    ::close(fds[0]);
    ::close(saved);
    if (rc != 0)
        return std::unexpected(Error {Error::Kind::Command, "samurai build failed", std::filesystem::path(manifest_str)});
    return {};
}

Result<void> samu_compdb(std::string_view manifest, std::string_view outpath) {
    const std::string m(manifest), o(outpath);
    if (const int rc = truss_samu_compdb(m.c_str(), o.c_str()); rc != 0)
        return std::unexpected(Error {Error::Kind::Command, "compdb generation failed", std::filesystem::path(m)});
    return {};
}

} // namespace truss::detail
