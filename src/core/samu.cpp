module;
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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
        char buf[8192];
        for (;;) {
            const ssize_t n = ::read(in_, buf, sizeof buf);
            if (n > 0) {
                pending.append(buf, static_cast<std::size_t>(n));
                std::size_t nl = 0;
                while ((nl = pending.find('\n')) != std::string::npos) {
                    line(std::string_view(pending).substr(0, nl));
                    pending.erase(0, nl + 1);
                }
                continue;
            }
            if (n < 0 && errno == EINTR)
                continue;
            break;
        }
        if (!pending.empty())
            line(pending);
        clear();
        if (cursor_hidden_)
            emit("\x1b[?25h"); /* show the cursor again */
    }

private:
    void emit(std::string_view s) {
        while (!s.empty()) {
            const ssize_t n = ::write(out_, s.data(), s.size());
            if (n <= 0)
                return;
            s.remove_prefix(static_cast<std::size_t>(n));
        }
    }

    void clear() {
        if (drawn_ == 0)
            return;
        emit("\r");
        for (std::size_t i = 0; i < drawn_; ++i)
            emit("\x1b[1A");
        emit("\x1b[J"); /* erase the block, leave earlier output alone */
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
            clear();
            emit(s);
            emit("\n");
            return;
        }
        in_flight_.emplace_back(desc);
        if (in_flight_.size() > max_lines_)
            in_flight_.erase(in_flight_.begin());
        started_ = started;
        total_ = total;
        redraw();
    }

    void redraw() {
        clear();
        std::string head = "  \x1b[2m[\x1b[0m" + std::to_string(started_) + "/"
                         + std::to_string(total_) + "\x1b[2m]\x1b[0m ";
        if (total_ != 0) {
            const std::size_t pct = (100 * started_) / total_;
            head += "\x1b[2;36m" + std::to_string(pct) + "%\x1b[0m ";
        }
        emit(head);
        emit("\n");
        ++drawn_;
        for (const auto& job : in_flight_) {
            emit("  \x1b[2m\u25b8\x1b[0m ");
            emit(job);
            emit("\n");
            ++drawn_;
        }
        if (!cursor_hidden_) {
            emit("\x1b[?25l"); /* hide the cursor while the block is live */
            cursor_hidden_ = true;
        }
    }

    int in_ {};
    int out_ {};
    std::size_t max_lines_ {};
    std::vector<std::string> in_flight_;
    std::size_t started_ {};
    std::size_t total_ {};
    std::size_t drawn_ {};
    bool cursor_hidden_ {};
};

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
     * thread; our real stdout is saved and restored afterwards */
    std::fflush(stdout);
    const int saved = ::dup(STDOUT_FILENO);
    int fds[2] {-1, -1};
    if (saved < 0 || ::pipe(fds) != 0) {
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
        ::dup2(saved, STDOUT_FILENO);
        ::close(fds[0]);          /* EOF for the pump */
        /* jthread joins here; it renders to `saved` until the pipe drains */
    }
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
