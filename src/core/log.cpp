module;
#include <cstdio>
#include <string_view>

#include <unistd.h>

module truss;
import :api;
import :impl;

namespace truss::detail {
namespace {

constexpr const char* kReset = "\x1b[0m";
constexpr const char* kGray = "\x1b[0;36m";
constexpr const char* kRed = "\x1b[0;31m";
constexpr const char* kGreen = "\x1b[0;32m";
constexpr const char* kOrange = "\x1b[0;33m";

bool color_enabled() {
    static const bool enabled = ::isatty(STDOUT_FILENO) != 0;
    return enabled;
}

void log_line(const char* color, const char* level, std::string_view msg) {
    if (color_enabled())
        std::printf("%s%s: %.*s%s\n", color, level, static_cast<int>(msg.size()), msg.data(), kReset);
    else
        std::printf("%s: %.*s\n", level, static_cast<int>(msg.size()), msg.data());
    std::fflush(stdout);
}

} // namespace

void log_info(std::string_view msg) { log_line(kGray, "[INFO]", msg); }
void log_warn(std::string_view msg) { log_line(kOrange, "[WARN]", msg); }
void log_success(std::string_view msg) { log_line(kGreen, "[SUCCESS]", msg); }
void log_error(std::string_view msg) { log_line(kRed, "[ERROR]", msg); }

void log_error(const Error& err) {
    std::string msg = err.message;
    if (err.path.has_value()) {
        msg += " (";
        msg += path_str(*err.path);
        msg += ")";
    }
    log_line(kRed, "[ERROR]", msg);
}

} // namespace truss::detail
