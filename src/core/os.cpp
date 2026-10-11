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
    size_t start = 0;
    while (start <= paths.size()) {
        const size_t sep = paths.find(':', start);
        std::string_view dir = paths.substr(start, sep == std::string_view::npos ? std::string_view::npos : sep - start);
        if (dir.empty())
            dir = ".";
        std::filesystem::path candidate {std::string(dir) + "/" + std::string(name)};
        if (usable(candidate)) {
            std::filesystem::path abs = std::filesystem::absolute(candidate, ec);
            return ec ? candidate : abs;
        }
        if (sep == std::string_view::npos)
            break;
        start = sep + 1;
    }
    return std::nullopt;
}

} // namespace

std::optional<std::filesystem::path> find_program(std::string_view name) {
    /* memoized (incl. misses): every build probes the same handful of
     * programs (llvm-ar, ccache, sccache, clang-scan-deps, conan, ...) and
     * the answer cannot change within one truss run */
    static std::map<std::string, std::optional<std::filesystem::path>, std::less<>> cache;
    static std::mutex mu;
    std::lock_guard lk(mu);
    if (const auto it = cache.find(name); it != cache.end())
        return it->second;
    const auto found = find_program_uncached(name);
    cache.emplace(std::string(name), found);
    return found;
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
    auto outcome = detail::run_process(argv, capture, opts.cwd);
    if (!outcome)
        return std::unexpected(outcome.error());
    return ProcessResult {outcome->exit_code, std::move(outcome->output)};
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
    const std::string tmpl = (temp_dir() / "truss-XXXXXX").string();
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    if (char* dir = mkdtemp(buf.data()); dir != nullptr)
        dir_ = dir;
}

temp_scope::temp_scope(temp_scope&& other) noexcept : dir_(std::move(other.dir_)) {
    other.dir_.clear();
}

temp_scope& temp_scope::operator=(temp_scope&& other) noexcept {
    if (this != &other) {
        if (!dir_.empty())
            detail::remove_tree(dir_);
        dir_ = std::move(other.dir_);
        other.dir_.clear();
    }
    return *this;
}

temp_scope::~temp_scope() {
    if (!dir_.empty())
        detail::remove_tree(dir_); // best effort; never throws
}

const std::filesystem::path& temp_scope::path() const { return dir_; }

} // namespace truss::os
