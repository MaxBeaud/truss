// module-scan result cache: skip the compiler's p1689 scan for TUs that have
// not changed since the last invocation. The expensive per-invocation work in
// `./truss` is the scan (~10 ms per TU through clang-scan-deps / GCC); samurai
// already handles incremental compilation, so an unchanged tree must cost
// about nothing here too.
module;
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module truss;
import :api;
import :impl;

namespace truss::detail {

namespace {

/* Bump when the entry format OR the meaning of a key input changes: every
 * entry then misses and is rewritten on the next scan. */
constexpr int kCacheFormat = 1;

std::atomic<std::size_t> g_hot {0};
std::atomic<std::size_t> g_cold {0};
std::atomic<std::size_t> g_skipped {0};

struct StatInfo {
    std::int64_t mtime {}; /* file_time_type ticks, exact */
    std::uintmax_t size {};
    bool ok {};
};

StatInfo stat_of(const std::filesystem::path& p) {
    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(p, ec);
    if (ec)
        return {};
    const auto size = std::filesystem::file_size(p, ec);
    if (ec)
        return {};
    return {static_cast<std::int64_t>(mtime.time_since_epoch().count()), size, true};
}

/* the compiler binary's identity: an in-place upgrade (same path) must not
 * keep serving results computed by the old compiler */
StatInfo compiler_stat(const std::string& compiler) {
    if (auto found = os::find_program(compiler); found.has_value())
        return stat_of(*found);
    return {}; /* bare name not on PATH: the key still carries the name */
}

} // namespace

void scan_cache_reset_stats() {
    g_hot.store(0, std::memory_order_relaxed);
    g_cold.store(0, std::memory_order_relaxed);
    g_skipped.store(0, std::memory_order_relaxed);
}

ScanCacheStats scan_cache_stats() {
    return {g_hot.load(std::memory_order_relaxed), g_cold.load(std::memory_order_relaxed),
            g_skipped.load(std::memory_order_relaxed)};
}

void scan_cache_note_skipped() { g_skipped.fetch_add(1, std::memory_order_relaxed); }

std::string scan_cache_key(const std::string& cwd, const std::string& compiler,
                           std::string_view compile_flags, const std::string& source) {
    const StatInfo cc = compiler_stat(compiler);
    /* the scanner is a separate binary for clang: its version matters too
     * (zig drives clang-scan-deps not at all, so it is not part of its key) */
    const std::string compiler_base = std::filesystem::path(compiler).filename().string();
    StatInfo scanner {};
    if (family_from_compiler_name(compiler) == CompilerFamily::Clang && compiler_base != "zig")
        scanner = compiler_stat("clang-scan-deps");

    std::string material;
    material.reserve(cwd.size() + source.size() + compile_flags.size() + compiler.size() + 128);
    const auto field = [&](std::string_view name, std::string_view value) {
        material.append(name).push_back('=');
        material.append(std::to_string(value.size())).push_back(':');
        material.append(value).push_back('\n');
    };
    field("format", std::to_string(kCacheFormat));
    field("cwd", cwd);
    field("source", source);
    field("compiler", compiler);
    field("compiler-stat", std::to_string(cc.mtime) + " " + std::to_string(cc.size));
    field("scanner-stat", std::to_string(scanner.mtime) + " " + std::to_string(scanner.size));
    field("flags", compile_flags);
    return sha256_bytes(material);
}

Result<std::optional<std::pair<std::vector<std::string>, std::vector<std::string>>>>
scan_cache_lookup(const std::filesystem::path& cache_dir, std::string_view key,
                  const std::string& source) {
    const auto file = cache_dir / (std::string(key) + ".scan");
    auto text = read_file(file);
    if (!text)
        return std::optional<std::pair<std::vector<std::string>, std::vector<std::string>>> {};
    const StatInfo now = stat_of(source);
    if (!now.ok)
        return std::optional<std::pair<std::vector<std::string>, std::vector<std::string>>> {};

    std::size_t pos = 0;
    const auto next_line = [&]() -> std::string_view {
        if (pos >= text->size())
            return {};
        const auto eol = text->find('\n', pos);
        const auto end = eol == std::string::npos ? text->size() : eol;
        const std::string_view line(text->data() + pos, end - pos);
        pos = eol == std::string::npos ? text->size() : eol + 1;
        return line;
    };
    const auto miss = [] {
        return std::optional<std::pair<std::vector<std::string>, std::vector<std::string>>> {};
    };

    if (next_line() != "truss-scan-cache " + std::to_string(kCacheFormat))
        return miss();
    const std::string_view stamp = next_line();
    const auto space = stamp.find(' ');
    if (space == std::string_view::npos)
        return miss();
    const auto parse_i64 = [](std::string_view s, std::int64_t& out) {
        if (s.empty())
            return false;
        std::int64_t v = 0;
        for (char c : s) {
            if (c < '0' || c > '9')
                return false;
            v = v * 10 + (c - '0');
        }
        out = v;
        return true;
    };
    std::int64_t mtime = 0;
    std::int64_t size = 0;
    if (!parse_i64(stamp.substr(0, space), mtime) || !parse_i64(stamp.substr(space + 1), size))
        return miss();
    if (mtime != now.mtime || static_cast<std::uintmax_t>(size) != now.size)
        return miss(); /* the source changed: rescan */

    std::pair<std::vector<std::string>, std::vector<std::string>> out;
    for (std::string_view line = next_line(); !line.empty(); line = next_line()) {
        if (line.size() < 3 || line[1] != '\t')
            return miss(); /* truncated/corrupt entry: treat as a miss */
        if (line[0] == 'i')
            out.first.emplace_back(line.substr(2));
        else if (line[0] == 'h')
            out.second.emplace_back(line.substr(2));
        else
            return miss();
    }
    g_hot.fetch_add(1, std::memory_order_relaxed);
    return std::optional<std::pair<std::vector<std::string>, std::vector<std::string>>> {
        std::move(out)};
}

Result<void> scan_cache_store(const std::filesystem::path& cache_dir, std::string_view key,
                              const std::string& source,
                              std::span<const std::string> imports,
                              std::span<const std::string> header_imports) {
    const StatInfo now = stat_of(source);
    if (!now.ok)
        return {};
    std::string data;
    data.reserve(64 + 32 * (imports.size() + header_imports.size()));
    data += "truss-scan-cache " + std::to_string(kCacheFormat) + "\n";
    data += std::to_string(now.mtime) + " " + std::to_string(now.size) + "\n";
    for (const auto& name : imports) {
        data.push_back('i');
        data.push_back('\t');
        data += name;
        data.push_back('\n');
    }
    for (const auto& name : header_imports) {
        data.push_back('h');
        data.push_back('\t');
        data += name;
        data.push_back('\n');
    }
    /* entries are tiny (one line per import); a concurrent writer can leave a
     * partial file visible, and the parser above rejects short lines, so a
     * torn entry degrades to a miss rather than a wrong graph */
    if (auto r = write_file(cache_dir / (std::string(key) + ".scan"), data); !r)
        return r;
    g_cold.fetch_add(1, std::memory_order_relaxed);
    return {};
}

/* Bound the cache directory: a long-lived tree accumulates one small file per
 * changed TU per mode forever. Beyond the cap, the oldest three quarters are
 * dropped (they are derived data: a miss costs one rescan). */
Result<void> scan_cache_gc(const std::filesystem::path& cache_dir, std::size_t max_entries) {
    std::error_code ec;
    if (!std::filesystem::is_directory(cache_dir, ec))
        return {};
    struct Entry {
        std::filesystem::file_time_type when;
        std::filesystem::path path;
    };
    std::vector<Entry> entries;
    for (std::filesystem::directory_iterator it(cache_dir, ec), end; it != end; it.increment(ec)) {
        if (ec)
            break;
        const auto ext = it->path().extension();
        if (ext != ".scan" && ext != ".text")
            continue;
        std::error_code time_ec;
        const auto when = it->last_write_time(time_ec);
        if (time_ec)
            continue;
        entries.push_back({when, it->path()});
    }
    if (entries.size() <= max_entries)
        return {};
    std::ranges::sort(entries, [](const Entry& a, const Entry& b) { return a.when < b.when; });
    const std::size_t keep = max_entries - max_entries / 4; /* three quarters */
    const std::size_t drop = entries.size() - keep;
    for (std::size_t i = 0; i < drop; ++i)
        std::filesystem::remove(entries[i].path, ec);
    return {};
}

/* --- text-scan cache: skip reading every source on every invocation ------ */

namespace {

/* the text scan's answer depends only on the file's bytes, so the key is the
 * path alone; the entry is validated by (mtime, size) like the compiler-scan
 * cache. Format version lives in the key material, so bumping it orphans old
 * entries. */
std::string text_key(const std::string& source) {
    std::string material = "truss-text-cache-1\n";
    material.append(std::to_string(source.size())).push_back(':');
    material += source;
    return sha256_bytes(material);
}

} // namespace

Result<ModuleUnitInfo> scan_module_unit_cached(const std::filesystem::path& cache_dir,
                                               const std::string& source) {
    const std::string key = text_key(source);
    const auto file = cache_dir / (key + ".text");
    const StatInfo now = stat_of(source);
    if (now.ok) {
        if (auto text = read_file(file); text) {
            std::size_t pos = 0;
            const auto next = [&]() -> std::string_view {
                if (pos >= text->size())
                    return {};
                const auto eol = text->find('\n', pos);
                const auto end = eol == std::string::npos ? text->size() : eol;
                const std::string_view line(text->data() + pos, end - pos);
                pos = eol == std::string::npos ? text->size() : eol + 1;
                return line;
            };
            bool ok = next() == "truss-text-scan 1";
            const std::string_view stamp = next();
            const auto space = stamp.find(' ');
            std::uintmax_t mtime = 0;
            std::uintmax_t size = 0;
            const auto parse = [](std::string_view v, std::uintmax_t& out) {
                if (v.empty())
                    return false;
                out = 0;
                for (char c : v) {
                    if (c < '0' || c > '9')
                        return false;
                    out = out * 10 + static_cast<std::uintmax_t>(c - '0');
                }
                return true;
            };
            if (ok && space != std::string_view::npos
                && parse(stamp.substr(0, space), mtime) && parse(stamp.substr(space + 1), size))
                ok = static_cast<std::int64_t>(mtime) == now.mtime && size == now.size;
            else
                ok = false;
            ModuleUnitInfo info;
            info.path = source;
            for (std::string_view line = ok ? next() : std::string_view {}; ok && !line.empty();
                 line = next()) {
                if (line.size() < 3 || line[1] != '\t') {
                    ok = false;
                    break;
                }
                const std::string_view value = line.substr(2);
                switch (line[0]) {
                case 'n': info.mentions_module_syntax = value == "1"; break;
                case 'm': info.module_name = std::string(value); break;
                case 'i': info.imports.emplace_back(value); break;
                case 'h': info.header_imports.emplace_back(value); break;
                default: ok = false; break;
                }
            }
            if (ok)
                return info;
        }
    }
    auto info = scan_module_unit(source);
    if (!info)
        return info;
    /* best effort: a read-only build dir just rescans next time */
    if (const StatInfo stamp = stat_of(source); stamp.ok) {
        std::string data = "truss-text-scan 1\n";
        data += std::to_string(stamp.mtime) + " " + std::to_string(stamp.size) + "\n";
        data += std::string("n\t") + (info->mentions_module_syntax ? "1" : "0") + "\n";
        if (!info->module_name.empty())
            data += "m\t" + info->module_name + "\n";
        for (const auto& name : info->imports)
            data += "i\t" + name + "\n";
        for (const auto& name : info->header_imports)
            data += "h\t" + name + "\n";
        static_cast<void>(write_file(cache_dir / (key + ".text"), data));
    }
    return info;
}

} // namespace truss::detail
