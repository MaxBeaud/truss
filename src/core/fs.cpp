module;
#include <algorithm>
#include <array>
#include <atomic>
#include <coroutine>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <unistd.h>

module truss;
import :api;
import :impl;

namespace truss::detail {
namespace {

Error io_error(const std::error_code& ec, const std::filesystem::path& p) {
    return Error {Error::Kind::Io, ec.message(), p};
}

// --- brace expansion: "src/*.{cpp,c}" -> ["src/*.cpp", "src/*.c"] ---
void expand_braces(std::string_view pattern, std::vector<std::string>& out, size_t depth = 0) {
    if (depth > 8) { // pathological nesting guard
        out.emplace_back(pattern);
        return;
    }
    const auto open = pattern.find('{');
    if (open == std::string_view::npos) {
        out.emplace_back(pattern);
        return;
    }
    // find the matching close brace, tracking nesting
    int level = 0;
    size_t close = std::string_view::npos;
    for (size_t i = open; i < pattern.size(); ++i) {
        if (pattern[i] == '{') ++level;
        else if (pattern[i] == '}' && --level == 0) { close = i; break; }
    }
    if (close == std::string_view::npos) { // unbalanced, treat literally
        out.emplace_back(pattern);
        return;
    }
    const std::string_view head = pattern.substr(0, open);
    const std::string_view tail = pattern.substr(close + 1);
    // split alternatives on top-level commas
    level = 0;
    size_t start = open + 1;
    for (size_t i = open + 1; i <= close; ++i) {
        if (i < close && pattern[i] == '{') ++level;
        else if (i < close && pattern[i] == '}') --level;
        if (i == close || (pattern[i] == ',' && level == 0)) {
            const std::string piece = std::string(head) + std::string(pattern.substr(start, i - start)) + std::string(tail);
            expand_braces(piece, out, depth + 1);
            start = i + 1;
        }
    }
}

/* --- convert one brace-free glob to an ECMAScript regex over the whole
 * relative path. `**` crosses '/', `*`/`?` do not. --- */
std::string glob_to_regex(std::string_view pattern) {
    std::string r = "^";
    for (size_t i = 0; i < pattern.size();) {
        const char c = pattern[i];
        switch (c) {
        case '*':
            if (i + 1 < pattern.size() && pattern[i + 1] == '*') {
                if ((i == 0 || pattern[i - 1] == '/') && i + 2 < pattern.size() && pattern[i + 2] == '/') {
                    /* a full double-star component followed by a slash: zero or
                     * more whole components (never a partial name), so "a" +
                     * double-star + "/b" matches a/b and a/x/b but not a/xb,
                     * and a leading double-star does not swallow fox.cpp */
                    r += "(?:[^/]+/)*";
                    i += 3;
                } else {
                    r += ".*";
                    i += 2;
                    if (i < pattern.size() && pattern[i] == '/') ++i; // "**/" consumes separator
                }
            } else {
                r += "[^/]*";
                ++i;
            }
            break;
        case '?': r += "[^/]"; ++i; break;
        case '.': case '+': case '(': case ')': case '[': case ']':
        case '^': case '$': case '|': case '\\':
            r += '\\'; r += c; ++i; break;
        default: r += c; ++i; break;
        }
    }
    r += '$';
    return r;
}

bool is_hidden(const std::filesystem::path& rel) {
    for (const auto& part : rel) {
        const std::string s = part.string();
        if (!s.empty() && s.front() == '.') return true;
    }
    return false;
}

/* Longest leading run of magic-free components; lets us scan a small subtree
 * instead of the whole tree. */
std::filesystem::path literal_prefix(const std::string_view pattern) {
    std::filesystem::path out;
    size_t i = 0;
    while (i < pattern.size()) {
        const size_t slash = pattern.find('/', i);
        const std::string_view comp = pattern.substr(i, slash == std::string_view::npos ? std::string_view::npos : slash - i);
        if (comp.find_first_of("*?{") != std::string_view::npos) break;
        if (!comp.empty()) out /= comp;
        if (slash == std::string_view::npos) break;
        i = slash + 1;
    }
    return out;
}

} // namespace

bool has_glob_magic(std::string_view pattern) {
    return pattern.find_first_of("*?{") != std::string_view::npos;
}

Result<std::vector<std::filesystem::path>> glob(std::string_view pattern) {
    const std::array<std::string, 1> single {std::string(pattern)};
    return glob_multi(single);
}

Result<std::vector<std::filesystem::path>> glob_multi(std::span<const std::string> patterns) {
    /* brace-expand every pattern, then compile one regex per alternative */
    std::vector<std::string> expanded;
    for (const auto& p : patterns)
        expand_braces(p, expanded);

    std::vector<std::regex> regexes;
    regexes.reserve(expanded.size());
    std::vector<std::filesystem::path> roots;
    for (const auto& p : expanded) {
        regexes.emplace_back(glob_to_regex(p));
        /* scan from the magic-free prefix, not from the whole tree; a fully
         * literal pattern scans its parent directory instead of itself */
        std::filesystem::path base = literal_prefix(p);
        if (base == p)
            base = base.parent_path();
        if (base.empty())
            base = ".";
        if (std::ranges::find(roots, base) == roots.end())
            roots.push_back(base);
    }

    std::vector<std::filesystem::path> matches;
    std::error_code ec;
    for (const auto& base : roots) {
        if (!std::filesystem::is_directory(base, ec))
            continue; // a missing (or non-directory) base simply matches nothing
        ec.clear();
        auto it = std::filesystem::recursive_directory_iterator(
            base, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end {};
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            /* lexically_relative (pure path arithmetic) instead of relative():
             * the latter weakly-canonicalizes both sides, i.e. syscalls per
             * entry; the iterator builds its paths from base, so the lexical
             * form is exact and free */
            const std::filesystem::path rel = it->path().lexically_relative(".");
            if (rel.empty()) continue;
            if (is_hidden(rel)) {
                if (it->is_directory(ec))
                    it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec) && !it->is_symlink(ec)) continue;
            const std::string rel_str = path_str(rel);
            for (const auto& re : regexes) {
                if (std::regex_match(rel_str, re)) {
                    matches.push_back(rel);
                    break;
                }
            }
        }
        /* an increment error (an entry deleted by a concurrent process mid
         * walk) degrades to the matches collected so far — like fs::walk —
         * instead of failing the whole glob */
    }

    std::ranges::sort(matches);
    matches.erase(std::ranges::unique(matches).begin(), matches.end()); /* overlapping roots */
    return matches;
}

std::vector<std::string> parse_depfile(std::string_view text) {
    std::vector<std::string> out;
    std::string word;
    bool after_colon = false; /* inside the prerequisites of the current rule */
    const auto flush = [&] {
        if (!word.empty())
            out.push_back(std::move(word));
        word.clear();
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\' && i + 1 < text.size()) {
            const char n = text[i + 1];
            if (n == '\n' || (n == '\r' && i + 2 < text.size() && text[i + 2] == '\n')) {
                flush(); /* line continuation: only a separator */
                i += n == '\r' ? 2 : 1;
            } else if (after_colon) {
                word.push_back(n); /* `\ ` -> space (also `\#`, `\\`) */
                ++i;
            }
        } else if (c == '\n') {
            flush();
            after_colon = false; /* next rule: skip its target again */
        } else if (c == ' ' || c == '\t' || c == '\r') {
            flush();
        } else if (!after_colon) {
            if (c == ':')
                after_colon = true;
        } else {
            word.push_back(c);
        }
    }
    flush();
    return out;
}

Result<std::string> read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in)
        return std::unexpected(Error {Error::Kind::Io, "failed to open file for reading", p});
    const std::streamoff size = in.tellg();
    in.seekg(0);
    std::string s;
    if (size > 0) { // regular file: one allocation, one read
        s.resize(static_cast<std::size_t>(size));
        in.read(s.data(), size);
        s.resize(static_cast<std::size_t>(in.gcount()));
    } else { // unseekable or empty: stream it out
        std::ostringstream ss;
        ss << in.rdbuf();
        s = ss.str();
    }
    if (in.bad())
        return std::unexpected(Error {Error::Kind::Io, "failed to read file", p});
    return s;
}

Result<void> write_file(const std::filesystem::path& p, std::string_view data) {
    std::error_code ec;
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path(), ec);
    if (ec)
        return std::unexpected(io_error(ec, p.parent_path()));

    /* write a sibling temp file then rename over the target: an interrupted
     * run never leaves a truncated manifest/lockfile/spec behind (rename is
     * atomic within a filesystem). The pid+counter keeps concurrent writers
     * apart, and removing any stale entry first refuses to write through a
     * pre-placed symlink at the temp name. */
    static std::atomic<unsigned> tmp_seq {0};
    std::filesystem::path tmp = p;
    tmp += ".truss-tmp." + std::to_string(getpid()) + "."
         + std::to_string(tmp_seq.fetch_add(1, std::memory_order_relaxed));
    std::filesystem::remove(tmp, ec); /* unlinks a stale file or symlink */
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return std::unexpected(Error {Error::Kind::Io, "failed to open file for writing", p});
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.close();
        if (!out) {
            std::filesystem::remove(tmp, ec);
            return std::unexpected(Error {Error::Kind::Io, "failed to write file", p});
        }
    }
    std::filesystem::rename(tmp, p, ec);
    if (ec) {
        const Error err = io_error(ec, p);
        std::filesystem::remove(tmp, ec);
        return std::unexpected(err);
    }
    return {};
}

/* write only when content differs: keeps mtimes stable so generated build
 * inputs (unity TUs) do not trigger spurious recompiles */
Result<void> write_file_if_changed(const std::filesystem::path& p, std::string_view data) {
    if (auto existing = read_file(p); existing && *existing == data)
        return {};
    return write_file(p, data);
}

Result<void> make_dirs(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    if (ec)
        return std::unexpected(io_error(ec, p));
    return {};
}

Result<void> remove_tree(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    if (ec)
        return std::unexpected(io_error(ec, p));
    return {};
}

/* --- lockfile (truss.lock): tiny key=value pin file ---------------------- */

std::map<std::string, std::string> read_lockfile(const std::filesystem::path& file) {
    std::map<std::string, std::string> out;
    std::ifstream in(file);
    std::string line;
    const auto trim = [](std::string s) {
        const auto b = s.find_first_not_of(" \t\r");
        const auto e = s.find_last_not_of(" \t\r");
        return b == std::string::npos ? std::string {} : s.substr(b, e - b + 1);
    };
    while (std::getline(in, line)) {
        /* a '#' starts a comment only at line start or after whitespace:
         * pinned values (git tags, URLs) may contain '#' themselves */
        for (std::size_t i = 0; i < line.size(); ++i)
            if (line[i] == '#' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) {
                line.erase(i);
                break;
            }
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        out[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return out;
}

Result<void> write_lockfile(const std::filesystem::path& file,
                            const std::map<std::string, std::string>& entries) {
    std::string body = "# truss.lock — dependency pins (generated by truss)\n";
    for (const auto& [k, v] : entries)
        body += k + " = " + v + "\n";
    return write_file(file, body);
}

std::string path_str(const std::filesystem::path& p) {
    return p.generic_string();
}

/* --- native sha256 (FIPS 180-4): the fetch pinning digest, no external
 * sha256sum dependency ------------------------------------------------------ */
namespace {

constexpr std::array<std::uint32_t, 64> kSha256K {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

void sha256_transform(std::uint32_t h[8], const std::uint8_t* p) {
    const auto rotr = [](std::uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); };
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (std::uint32_t(p[i * 4]) << 24) | (std::uint32_t(p[i * 4 + 1]) << 16) |
               (std::uint32_t(p[i * 4 + 2]) << 8) | std::uint32_t(p[i * 4 + 3]);
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = hh + S1 + ch + kSha256K[i] + w[i];
        const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

/* Incremental context: sha256_file streams large tarballs through bounded
 * memory instead of slurping the whole file. */
struct Sha256Ctx {
    std::uint32_t h[8] {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::array<std::uint8_t, 64> buf {};
    std::size_t buflen {};
    std::uint64_t total {};

    void update(const std::uint8_t* p, std::size_t n) {
        total += n;
        if (buflen) { // complete a partial block first
            const std::size_t take = std::min<std::size_t>(buf.size() - buflen, n);
            std::memcpy(buf.data() + buflen, p, take);
            buflen += take;
            p += take;
            n -= take;
            if (buflen == buf.size()) {
                sha256_transform(h, buf.data());
                buflen = 0;
            }
        }
        while (n >= buf.size()) { sha256_transform(h, p); p += buf.size(); n -= buf.size(); }
        if (n) { std::memcpy(buf.data(), p, n); buflen = n; }
    }

    std::string final_hex() { // padding: message + 0x80 + zeros + 64-bit length
        std::array<std::uint8_t, 128> tail {};
        const std::size_t n = buflen;
        std::memcpy(tail.data(), buf.data(), n);
        tail[n] = 0x80;
        const std::size_t padded = (n < 56) ? 64 : 128;
        const std::uint64_t bits = total * 8;
        for (int i = 0; i < 8; ++i)
            tail[padded - 1 - i] = static_cast<std::uint8_t>(bits >> (8 * i));
        for (std::size_t off = 0; off < padded; off += 64)
            sha256_transform(h, tail.data() + off);

        static constexpr char hexdig[] = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (const std::uint32_t word : h)
            for (int i = 3; i >= 0; --i) {
                const std::uint8_t byte = (word >> (8 * i)) & 0xff;
                out.push_back(hexdig[byte >> 4]);
                out.push_back(hexdig[byte & 0xf]);
            }
        return out;
    }
};

} // namespace

} // namespace truss::detail

/* --- public truss::fs (phase 13): thin surface over the internals ---------- */

namespace truss::fs {

Result<std::vector<std::filesystem::path>> glob(std::string_view pattern) {
    return detail::glob(pattern);
}

Result<std::vector<std::filesystem::path>> glob(std::initializer_list<std::string_view> patterns) {
    const std::vector<std::string> pats(patterns.begin(), patterns.end());
    return detail::glob_multi(pats);
}

generator<std::filesystem::path> walk(const std::filesystem::path& dir, WalkOptions opts) {
    std::error_code ec;
    auto it = std::filesystem::recursive_directory_iterator(
        dir, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec)
        co_return; // missing/unreadable root yields nothing (forgiving, like glob)
    const int max_depth = opts.depth == walk_unlimited ? std::numeric_limits<int>::max() : opts.depth;
    for (const std::filesystem::recursive_directory_iterator end {}; it != end; it.increment(ec)) {
        if (ec)
            break; // unreadable directory: stop rather than fail the loop
        if (opts.skip_hidden) {
            const std::string name = it->path().filename().string();
            if (!name.empty() && name.front() == '.') {
                it.disable_recursion_pending();
                continue;
            }
        }
        co_yield it->path();
        if (static_cast<int>(it.depth()) >= max_depth)
            it.disable_recursion_pending();
    }
}

std::optional<std::filesystem::path> find_upward(std::string_view filename,
                                                 const std::filesystem::path& from_dir) {
    std::error_code ec;
    std::filesystem::path cur = std::filesystem::absolute(from_dir, ec);
    if (ec)
        cur = from_dir;
    for (;;) {
        const std::filesystem::path candidate = cur / filename;
        if (std::filesystem::exists(candidate, ec))
            return candidate;
        const std::filesystem::path parent = cur.parent_path();
        if (parent.empty() || parent == cur)
            return std::nullopt;
        cur = parent;
    }
}

std::optional<std::filesystem::path> find_upward(std::string_view filename) {
    std::error_code ec;
    std::filesystem::path cur = std::filesystem::current_path(ec);
    return find_upward(filename, ec ? std::filesystem::path {"."} : cur);
}

std::optional<std::filesystem::path> first_existing(
        std::initializer_list<std::filesystem::path> candidates) {
    std::error_code ec;
    for (const auto& cand : candidates)
        if (std::filesystem::exists(cand, ec))
            return cand;
    return std::nullopt;
}

std::optional<std::filesystem::path> find_library(std::string_view name,
                                                  std::span<const std::filesystem::path> search_dirs) {
    /* directory-first: the caller's search-path priority wins over suffix order;
     * lib{name}.* first, then the bare MSVC-style {name}.lib */
    static constexpr std::array<std::string_view, 4> suffixes {".a", ".so", ".dylib", ".lib"};
    std::error_code ec;
    for (const auto& dir : search_dirs) {
        for (const std::string_view suffix : suffixes) {
            const std::filesystem::path cand = dir / ("lib" + std::string(name) + std::string(suffix));
            if (std::filesystem::is_regular_file(cand, ec))
                return cand;
        }
        const std::filesystem::path msvc = dir / (std::string(name) + ".lib");
        if (std::filesystem::is_regular_file(msvc, ec))
            return msvc;
    }
    return std::nullopt;
}

const std::vector<std::filesystem::path>& system_library_dirs() {
    /* best-effort convenience for the no-dirs overload: dynamic-loader env
     * paths first, then the usual system locations (incl. the multiarch dir
     * for the host arch on GNU/Linux, Homebrew on macOS). Explicit dirs
     * remain the reliable route (pkg-config, -L dirs, vcpkg_installed/...). */
    static const std::vector<std::filesystem::path> dirs = [] {
        std::vector<std::filesystem::path> out;
        const auto add_env = [&](std::string_view var) {
            const auto value = os::env(var);
            if (!value)
                return;
            for (const std::string_view entry : detail::split_env_list(*value))
                if (!entry.empty())
                    out.emplace_back(entry);
        };
        if constexpr (os::is_linux())
            add_env("LD_LIBRARY_PATH");
        else if constexpr (os::is_macos())
            add_env("DYLD_LIBRARY_PATH");

        out.emplace_back("/usr/local/lib");
        if constexpr (os::is_linux()) {
            out.emplace_back("/usr/lib");
            switch (os::host_arch()) {
            case Arch::X64:    out.emplace_back("/usr/lib/x86_64-linux-gnu"); break;
            case Arch::Arm64:  out.emplace_back("/usr/lib/aarch64-linux-gnu"); break;
            case Arch::Arm32:  out.emplace_back("/usr/lib/arm-linux-gnueabihf"); break;
            case Arch::Riscv64: out.emplace_back("/usr/lib/riscv64-linux-gnu"); break;
            default: break;
            }
            out.emplace_back("/lib");
        } else if constexpr (os::is_macos()) {
            out.emplace_back("/usr/lib");
            out.emplace_back("/opt/homebrew/lib");
        }
        return out;
    }();
    return dirs;
}

std::optional<std::filesystem::path> find_library(std::string_view name) {
    return find_library(name, system_library_dirs());
}

Result<std::string> read_text(const std::filesystem::path& p) { return detail::read_file(p); }
Result<void> write_text(const std::filesystem::path& p, std::string_view data) {
    return detail::write_file(p, data);
}
Result<void> write_text_if_changed(const std::filesystem::path& p, std::string_view data) {
    return detail::write_file_if_changed(p, data);
}

namespace {
generator<std::string> lines_of(std::filesystem::path p) {
    std::ifstream in(p);
    std::string line;
    while (std::getline(in, line))
        co_yield line; // stream errors mid-file simply end the iteration
}
} // namespace

Result<generator<std::string>> read_lines(const std::filesystem::path& p) {
    std::ifstream in(p);
    if (!in)
        return std::unexpected(Error {Error::Kind::Io, "failed to open file for reading", p});
    return lines_of(std::move(p));
}

Result<std::string> sha256_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return std::unexpected(Error {Error::Kind::Io, "failed to open file for reading", p});
    detail::Sha256Ctx ctx; // streamed in chunks: bounded memory for big tarballs
    std::array<char, 65536> chunk {};
    while (in) {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        if (const std::streamsize got = in.gcount(); got > 0)
            ctx.update(reinterpret_cast<const std::uint8_t*>(chunk.data()),
                       static_cast<std::size_t>(got));
    }
    if (in.bad())
        return std::unexpected(Error {Error::Kind::Io, "failed to read file", p});
    return ctx.final_hex();
}

Result<std::filesystem::path> current_dir() {
    std::error_code ec;
    auto p = std::filesystem::current_path(ec);
    if (ec)
        return std::unexpected(detail::io_error(ec, "."));
    return p;
}

Result<void> copy_tree(const std::filesystem::path& from, const std::filesystem::path& to) {
    return detail::copy_tree(from, to);
}

std::string generic_str(const std::filesystem::path& p) { return detail::path_str(p); }

} // namespace truss::fs
