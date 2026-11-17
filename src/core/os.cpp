/**
 * truss — truss::os runtime helpers (program lookup, env).
 * The consteval platform predicates live in the :api partition itself.
 */
module;
#include <array>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <sys/utsname.h>
#include <unistd.h>

module truss;
import :api;
import :impl;

namespace truss::os {
namespace {

std::optional<std::filesystem::path> find_program_uncached(std::string_view name) {
    std::error_code ec;
    const auto usable = [&](const std::filesystem::path& p) {
        return std::filesystem::is_regular_file(p, ec) && access(p.c_str(), X_OK) == 0;
    };

    /* a name containing '/' is a path, not a program: check it directly */
    if (name.find('/') != std::string_view::npos) {
        const std::filesystem::path p {name};
        if (!usable(p))
            return std::nullopt;
        std::filesystem::path abs = std::filesystem::absolute(p, ec);
        return ec ? p : abs;
    }

    /* PATH walk; empty entry means cwd (POSIX), unset PATH a sane default */
    const char* path_var = std::getenv("PATH");
    const std::string_view paths = (path_var != nullptr && *path_var != '\0')
                                       ? std::string_view {path_var}
                                       : std::string_view {"/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"};
    for (const std::string_view entry : detail::split_env_list(paths)) {
        const std::string_view dir = entry.empty() ? std::string_view {"."} : entry;
        std::filesystem::path candidate {std::string(dir) + "/" + std::string(name)};
        if (usable(candidate)) {
            std::filesystem::path abs = std::filesystem::absolute(candidate, ec);
            return ec ? candidate : abs;
        }
    }
    return std::nullopt;
}

} // namespace

} // namespace truss::os

namespace truss::detail {

std::vector<std::string_view> split_env_list(std::string_view value) {
    std::vector<std::string_view> out;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t sep = value.find(':', start);
        out.push_back(value.substr(start, sep == std::string_view::npos ? std::string_view::npos
                                                                        : sep - start));
        if (sep == std::string_view::npos)
            break;
        start = sep + 1;
    }
    return out;
}

} // namespace truss::detail

namespace truss::os {

std::optional<std::filesystem::path> find_program(std::string_view name) {
    /* memoized (incl. misses): every build probes the same handful of
     * programs (llvm-ar, ccache, sccache, clang-scan-deps, conan, ...) and
     * the answer cannot change within one truss run. The (disk-walking)
     * lookup happens OUTSIDE the lock; a re-check under it closes the race */
    static std::map<std::string, std::optional<std::filesystem::path>, std::less<>> cache;
    static std::mutex mu;
    {
        std::lock_guard lk(mu);
        if (const auto it = cache.find(name); it != cache.end())
            return it->second;
    }
    auto found = find_program_uncached(name);
    {
        std::lock_guard lk(mu);
        const auto [it, inserted] = cache.try_emplace(std::string(name), std::move(found));
        return it->second;
    }
}

std::optional<std::filesystem::path> find_program(std::initializer_list<std::string_view> names) {
    for (const std::string_view name : names)
        if (auto found = find_program(name))
            return found;
    return std::nullopt;
}

bool program_exists(std::string_view name) {
    return find_program(name).has_value();
}

bool all_programs(std::initializer_list<std::string_view> names) {
    for (const std::string_view name : names)
        if (!find_program(name))
            return false;
    return true;
}

bool any_program(std::initializer_list<std::string_view> names) {
    for (const std::string_view name : names)
        if (find_program(name))
            return true;
    return false;
}

std::optional<std::string> env(std::string_view name) {
    const std::string key(name); // getenv needs a NUL-terminated name
    if (const char* value = std::getenv(key.c_str()); value != nullptr && *value != '\0')
        return std::string {value};
    return std::nullopt;
}

/* --- machine specs --------------------------------------------------------- */

unsigned cpu_count() {
    return std::thread::hardware_concurrency();
}

std::uint64_t memory_bytes() {
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || page_size <= 0)
        return 0;
    return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page_size);
}

std::optional<std::string> os_version() {
    if constexpr (is_linux()) {
        if (auto release = detail::read_file("/etc/os-release"); release) {
            std::string_view rest = *release;
            while (!rest.empty()) {
                const std::string_view::size_type nl = rest.find('\n');
                std::string_view line = rest.substr(0, nl);
                if (line.ends_with("\r"))
                    line.remove_suffix(1);
                if (line.starts_with("PRETTY_NAME=")) {
                    std::string name {line.substr(12)};
                    if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
                        name = name.substr(1, name.size() - 2);
                    if (!name.empty())
                        return name;
                }
                if (nl == std::string_view::npos)
                    break;
                rest.remove_prefix(nl + 1);
            }
        }
    }
    struct utsname u {};
    if (uname(&u) == 0)
        return std::string {u.sysname} + " " + u.release;
    return std::nullopt;
}

std::optional<std::string> hostname() {
    std::array<char, 256> buf {};
    if (gethostname(buf.data(), buf.size()) != 0)
        return std::nullopt;
    buf.back() = '\0';
    return std::string {buf.data()};
}

std::filesystem::path temp_dir() {
    std::error_code ec;
    auto p = std::filesystem::temp_directory_path(ec);
    return ec ? std::filesystem::path {"/tmp"} : p;
}

/* --- process --------------------------------------------------------------- */

namespace {
Result<ProcessResult> spawn(std::span<const std::string> argv, bool capture, const RunOptions& opts) {
    if (opts.timeout < 0)
        return std::unexpected(Error {Error::Kind::Config,
                                      "timeout must not be negative (0 = no limit)"});
    return detail::run_process(argv, capture, opts.cwd, opts.timeout)
        .transform([](auto outcome) { return ProcessResult {outcome.exit_code, std::move(outcome.output)}; });
}
} // namespace

Result<ProcessResult> run(std::span<const std::string> argv, RunOptions opts) {
    return spawn(argv, false, opts);
}

Result<ProcessResult> capture(std::span<const std::string> argv, RunOptions opts) {
    return spawn(argv, true, opts);
}

Result<ProcessResult> run(std::initializer_list<std::string> argv, RunOptions opts) {
    return run(std::span<const std::string> {argv}, opts);
}

Result<ProcessResult> capture(std::initializer_list<std::string> argv, RunOptions opts) {
    return capture(std::span<const std::string> {argv}, opts);
}

/* --- temp_scope ------------------------------------------------------------ */

temp_scope::temp_scope() {
    /* std::string::data() is mutable and NUL-terminated since C++17 */
    std::string buf = (temp_dir() / "truss-XXXXXX").string();
    if (char* dir = mkdtemp(buf.data()); dir != nullptr)
        dir_ = dir;
}

temp_scope::temp_scope(temp_scope&& other) noexcept : dir_(std::move(other.dir_)) {
    other.dir_.clear();
}

temp_scope& temp_scope::operator=(temp_scope&& other) noexcept {
    if (this != &other) {
        if (!dir_.empty())
            static_cast<void>(detail::remove_tree(dir_)); // best effort
        dir_ = std::move(other.dir_);
        other.dir_.clear();
    }
    return *this;
}

temp_scope::~temp_scope() {
    if (!dir_.empty())
        static_cast<void>(detail::remove_tree(dir_)); // best effort; never throws
}

const std::filesystem::path& temp_scope::path() const { return dir_; }

std::vector<std::filesystem::path> find_truss_modules() {
    std::vector<std::filesystem::path> result;
    std::set<std::filesystem::path> seen; /* prefix + exe dir are frequently the same */

    /* a candidate counts only when both BMIs the build script needs are there */
    const auto try_add = [&](std::filesystem::path p) {
        std::error_code ec;
        if (!std::filesystem::exists(p / "truss.pcm", ec) ||
            !std::filesystem::exists(p / "truss_api.pcm", ec))
            return;
        if (!seen.insert(p).second)
            return; /* already listed (prefix and exe dir often coincide) */
        result.push_back(std::move(p));
    };

    // Check TRUSS_INSTALL_PREFIX environment variable first
    if (const char* prefix = std::getenv("TRUSS_INSTALL_PREFIX"))
        try_add(std::filesystem::path(prefix) / "lib" / "truss");

    // Get the directory of the current truss executable
    std::error_code ec;
    auto self_exe = std::filesystem::canonical("/proc/self/exe", ec);
    if (!ec && !self_exe.empty())
        try_add(self_exe.parent_path().parent_path() / "lib" / "truss");

    // Standard installation paths
    const char* home = std::getenv("HOME");
    for (const auto& path_template : {"$HOME/.local/lib/truss", ".local/lib/truss",
                                      "/usr/local/lib/truss", "/usr/lib/truss",
                                      "/usr/lib64/truss"}) {
        std::string path_str = std::string(path_template);
        // Expand $HOME (skip the candidate when HOME is unset: a cwd-relative
        // ~/.local would silently point somewhere else)
        if (path_str.starts_with("$HOME")) {
            if (home == nullptr)
                continue;
            path_str.replace(0, 5, home);
        }
        // If the path is relative (like .local/lib/truss), make it absolute from current directory
        std::filesystem::path p(path_str);
        if (!p.is_absolute()) {
            auto cwd = std::filesystem::current_path(ec);
            if (ec)
                continue;
            p = cwd / p;
        }
        try_add(std::move(p));
    }

    return result;
}

} // namespace truss::os
