/**
 * truss — truss::os runtime helpers (program lookup, env).
 * The consteval platform predicates live in the :api partition itself.
 */
module;
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

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

std::optional<std::string> env(std::string_view name) {
    const std::string key(name); // getenv needs a NUL-terminated name
    if (const char* value = std::getenv(key.c_str()); value != nullptr && *value != '\0')
        return std::string {value};
    return std::nullopt;
}

} // namespace truss::os
