module;
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string_view>

#include <unistd.h>

module truss;
import :api;
import :impl;

namespace truss::log {
namespace {

/* levels: 0 debug, 1 info, 2 warn, 3 error; messages below the threshold are
 * dropped. Default: info. TRUSS_LOG overrides (quiet == error-only); the CLI's
 * -q flag calls detail::log_set_quiet(). */
std::atomic<int> g_min_level {1};

/* ANSI SGR codes. Colour is on only for a terminal, and honours the
 * de-facto standard env knobs: NO_COLOR disables, CLICOLOR_FORCE=1 enables
 * even through a pipe, CLICOLOR=0 disables. */
constexpr const char* kReset = "\x1b[0m";
constexpr const char* kBold = "\x1b[1m";
constexpr const char* kCyan = "\x1b[0;36m";
constexpr const char* kRed = "\x1b[0;31m";
constexpr const char* kGreen = "\x1b[0;32m";
constexpr const char* kOrange = "\x1b[0;33m";

bool color_enabled() {
    static const bool enabled = [] {
        if (const char* no = std::getenv("NO_COLOR"); no != nullptr && *no != '\0')
            return false;
        if (const char* force = std::getenv("CLICOLOR_FORCE"); force != nullptr && *force != '\0'
            && std::strcmp(force, "0") != 0)
            return true;
        if (const char* clicolor = std::getenv("CLICOLOR"); clicolor != nullptr
            && std::strcmp(clicolor, "0") == 0)
            return false;
        return ::isatty(STDOUT_FILENO) != 0;
    }();
    return enabled;
}

bool level_enabled(int level) { return level >= g_min_level.load(std::memory_order_relaxed); }

void log_line(const char* color, const char* level, std::string_view msg, int severity) {
    if (!level_enabled(severity))
        return;
    if (color_enabled())
        std::printf("%s%s: %.*s%s\n", color, level, static_cast<int>(msg.size()), msg.data(), kReset);
    else
        std::printf("%s: %.*s\n", level, static_cast<int>(msg.size()), msg.data());
    std::fflush(stdout);
}

} // namespace

} // namespace truss::log

namespace truss::detail {

void log_set_min_level(int level) { log::g_min_level.store(level, std::memory_order_relaxed); }

void log_init_from_env() {
    static const bool once = [] {
        const char* v = std::getenv("TRUSS_LOG");
        if (v == nullptr)
            return true;
        const std::string_view s(v);
        if (s == "quiet" || s == "error" || s == "0")
            log::g_min_level.store(3);
        else if (s == "warn")
            log::g_min_level.store(2);
        else if (s == "debug")
            log::g_min_level.store(0);
        else
            log::g_min_level.store(1);
        return true;
    }();
    (void)once;
}

} // namespace truss::detail

namespace truss::log {

void info(std::string_view msg) { log_line(kCyan, "[INFO]", msg, 1); }
void warn(std::string_view msg) { log_line(kOrange, "[WARN]", msg, 2); }
void success(std::string_view msg) { log_line(kGreen, "[SUCCESS]", msg, 1); }
void error(std::string_view msg) { log_line(kRed, "[ERROR]", msg, 3); }

void error(const Error& err) {
    std::string msg = err.message;
    if (err.path.has_value()) {
        msg += " (";
        msg += detail::path_str(*err.path);
        msg += ")";
    }
    log_line(kRed, "[ERROR]", msg, 3);
}

void step(std::string_view name, std::string_view detail) {
    if (!level_enabled(1))
        return;
    const bool color = color_enabled();
    /* `name` is left-padded to 10 columns so consecutive phase lines align */
    std::string padded(name);
    if (padded.size() < 10)
        padded.append(10 - padded.size(), ' ');
    if (color)
        std::printf("%s%s\u25b8%s %s%s%s %.*s%s\n", kBold, kCyan, kReset, kBold, padded.c_str(), kReset,
                    static_cast<int>(detail.size()), detail.data(), kReset);
    else
        std::printf("-> %s %.*s\n", padded.c_str(), static_cast<int>(detail.size()), detail.data());
    std::fflush(stdout);
}

} // namespace truss::log
