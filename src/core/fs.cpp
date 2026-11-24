module;
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
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
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <unistd.h>
#include <fcntl.h>

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

// --- linear-time DP matcher for one brace-free glob over the whole path.
// `**` crosses '/', `*`/`?` do not. A full double-star component matches zero
// or more whole path components (never a partial name). Zero exceptions, no
// regex backtracking. ---
bool glob_match(std::string_view pattern, std::string_view text) {
    const std::size_t m = pattern.size();
    const std::size_t n = text.size();
    std::vector<std::uint8_t> dp(n + 1, 0);
    dp[0] = 1;
    for (std::size_t i = 0; i < m;) {
        const char c = pattern[i];
        if (c == '*') {
            if (i + 1 < m && pattern[i + 1] == '*') {
                if ((i == 0 || pattern[i - 1] == '/') && i + 2 < m && pattern[i + 2] == '/') {
                    // full double-star component: zero or more whole components
                    for (std::size_t j = 0; j < n; ++j) {
                        if (!dp[j] || text[j] == '/')
                            continue;
                        const std::size_t slash = text.find('/', j);
                        if (slash != std::string_view::npos)
                            dp[slash + 1] = 1;
                    }
                    i += 3;
                } else {
                    /* partial `**`: matches zero or more of any char */
                    i += 2;
                    if (i < m && pattern[i] == '/')
                        ++i;
                    bool seen = false;
                    for (std::size_t j = 0; j <= n; ++j) {
                        if (dp[j])
                            seen = true;
                        else if (seen)
                            dp[j] = 1;
                    }
                }
            } else {
                /* single `*`: zero or more non-'/' chars */
                for (std::size_t j = 0; j < n; ++j)
                    if (dp[j] && text[j] != '/')
                        dp[j + 1] = 1;
                ++i;
            }
        } else if (c == '?') {
            for (std::size_t j = n; j > 0; --j)
                dp[j] = (dp[j - 1] && text[j - 1] != '/') ? 1 : 0;
            dp[0] = 0;
            ++i;
        } else {
            for (std::size_t j = n; j > 0; --j)
                dp[j] = (dp[j - 1] && text[j - 1] == c) ? 1 : 0;
            dp[0] = 0;
            ++i;
        }
    }
    return dp[n] != 0;
}

bool is_hidden(const std::filesystem::path& rel) {
    for (const auto& part : rel) {
        const auto& s = part.native(); /* reference: no per-component copy */
        if (!s.empty() && s.front() == '.' && s != "." && s != "..")
            return true;
    }
    return false;
}

struct PatternPlan {
    std::filesystem::path base;   // "." when no leading literal components
    std::optional<int> max_depth; // nullopt = unlimited (pattern contains "**")
};

/* Split an expanded (brace-free) glob pattern into its longest leading run of
 * magic-free components (`base`) and the max directory depth below `base` that
 * can ever match (nullopt when the suffix contains `**`). */
PatternPlan analyze_pattern(const std::string_view pattern) {
    std::filesystem::path base;
    std::size_t i = 0;
    if (!pattern.empty() && pattern.front() == '/') {
        base = "/";
        i = 1;
    }
    std::size_t suffix_start = i;
    while (i < pattern.size()) {
        const std::size_t slash = pattern.find('/', i);
        const std::string_view comp =
            pattern.substr(i, slash == std::string_view::npos ? std::string_view::npos : slash - i);
        if (comp.find_first_of("*?{") != std::string_view::npos)
            break;
        if (!comp.empty())
            base /= comp;
        if (slash == std::string_view::npos) {
            suffix_start = pattern.size();
            break;
        }
        i = slash + 1;
        suffix_start = i;
    }
    if (base.empty())
        base = ".";

    const std::string_view suffix = pattern.substr(suffix_start);
    if (suffix.find("**") != std::string_view::npos)
        return {std::move(base), std::nullopt};

    /* `*` and `?` never cross `/`: matching entries live at a fixed depth
     * below `base`, equal to (non-empty components in suffix) - 1. */
    int components = 0;
    std::size_t pos = 0;
    while (pos < suffix.size()) {
        const std::size_t slash = suffix.find('/', pos);
        const std::size_t end = slash == std::string_view::npos ? suffix.size() : slash;
        if (end > pos)
            ++components;
        if (slash == std::string_view::npos)
            break;
        pos = slash + 1;
    }
    return {std::move(base), components > 0 ? components - 1 : 0};
}

std::optional<int> merge_depth(std::optional<int> a, std::optional<int> b) {
    if (!a || !b)
        return std::nullopt;
    return std::max(*a, *b);
}

std::filesystem::path norm_abs(const std::filesystem::path& p, const std::filesystem::path& cwd) {
    std::filesystem::path out = (p.is_absolute() ? p : (cwd / p)).lexically_normal();
    /* lexically_normal() on "/a/b/." leaves a trailing empty component ("/a/b/");
     * strip it unless out is the root "/". */
    if (out.has_relative_path() && out.filename().empty())
        out = out.parent_path();
    return out;
}

/* If `target` is strictly inside `ancestor` (both normalized absolute paths),
 * returns the relative path from `ancestor` to `target`; otherwise empty. */
std::filesystem::path strict_subpath(const std::filesystem::path& ancestor,
                                     const std::filesystem::path& target) {
    const std::filesystem::path rel = target.lexically_relative(ancestor);
    if (rel.empty() || rel == "." || rel.is_absolute())
        return {};
    if (*rel.begin() == "..")
        return {};
    return rel;
}

/* Returns the relative path from `anc_base` to `child_base` if walking
 * `anc_base` yields byte-for-byte identical match strings for entries under
 * `child_base`, without skipping any component as hidden or backtracking. */
std::optional<std::pair<std::filesystem::path, int>> syntactic_child_rel(
    const std::filesystem::path& anc_base,
    const std::filesystem::path& child_base) {
    if (anc_base == child_base || anc_base.is_absolute() != child_base.is_absolute())
        return std::nullopt;
    const std::string anc_str = path_str(anc_base);
    const std::string child_str = path_str(child_base);
    std::string rel_str;
    if (anc_base == ".") {
        if (child_str.empty() || child_str == "." || child_str == ".."
            || child_str.starts_with("./") || child_str.starts_with("../"))
            return std::nullopt;
        rel_str = child_str;
    } else if (anc_base == "/") {
        if (!child_str.starts_with('/') || child_str.size() <= 1)
            return std::nullopt;
        rel_str = child_str.substr(1);
    } else {
        if (child_str.size() <= anc_str.size() + 1 || !child_str.starts_with(anc_str)
            || child_str[anc_str.size()] != '/')
            return std::nullopt;
        rel_str = child_str.substr(anc_str.size() + 1);
    }
    const std::filesystem::path rel(rel_str);
    int depth_diff = 0;
    for (const auto& part : rel) {
        const auto& s = part.native();
        if (s.empty() || s == "." || s == ".." || s.front() == '.')
            return std::nullopt;
        ++depth_diff;
    }
    if (depth_diff == 0)
        return std::nullopt;
    return std::pair {rel, depth_diff};
}

Result<std::vector<std::vector<std::filesystem::path>>> glob_groups_impl(
    std::span<const std::span<const std::string>> groups,
    const std::filesystem::path& exclude_dir) {
    std::vector<std::vector<std::filesystem::path>> results(groups.size());

    struct Consumer {
        std::size_t group_idx {0};
        std::string sub_prefix; // non-empty when subsumed from a child root
        std::vector<std::string> patterns;
    };
    struct WalkRoot {
        std::filesystem::path base;
        std::filesystem::path abs_base;
        std::filesystem::path skip_rel;
        std::optional<int> max_depth;
        std::vector<Consumer> consumers;
        bool subsumed {false};
    };

    std::vector<WalkRoot> walks;
    std::map<std::string, std::size_t> root_index;
    std::error_code ec;

    for (std::size_t g = 0; g < groups.size(); ++g) {
        std::vector<std::string> expanded;
        for (const auto& p : groups[g])
            expand_braces(p, expanded);

        for (const auto& p : expanded) {
            if (!has_glob_magic(p)) {
                const std::filesystem::path fp(p);
                if (std::filesystem::is_regular_file(fp, ec) || std::filesystem::is_symlink(fp, ec))
                    results[g].push_back(fp);
                continue;
            }
            auto plan = analyze_pattern(p);
            const std::string key = path_str(plan.base);
            auto [it, inserted] = root_index.emplace(key, walks.size());
            if (inserted) {
                WalkRoot wr;
                wr.base = std::move(plan.base);
                wr.max_depth = plan.max_depth;
                walks.push_back(std::move(wr));
            } else {
                walks[it->second].max_depth = merge_depth(walks[it->second].max_depth, plan.max_depth);
            }
            auto& consumers = walks[it->second].consumers;
            if (!consumers.empty() && consumers.back().group_idx == g && consumers.back().sub_prefix.empty()) {
                consumers.back().patterns.push_back(p);
            } else {
                consumers.push_back(Consumer {g, {}, {p}});
            }
        }
    }

    if (!walks.empty()) {
        std::filesystem::path abs_exclude;
        if (!exclude_dir.empty()) {
            std::error_code cwd_ec;
            const std::filesystem::path cwd = std::filesystem::current_path(cwd_ec);
            if (!cwd_ec) {
                abs_exclude = norm_abs(exclude_dir, cwd);
                for (auto& w : walks) {
                    w.abs_base = norm_abs(w.base, cwd);
                    w.skip_rel = strict_subpath(w.abs_base, abs_exclude);
                }
            }
        }

        /* Sort shortest roots first (with "." leading) so ancestors are active
         * before descendants during subsumption. */
        std::ranges::sort(walks, [](const WalkRoot& a, const WalkRoot& b) {
            const bool a_dot = a.base == ".";
            const bool b_dot = b.base == ".";
            if (a_dot != b_dot)
                return a_dot;
            const std::string as = path_str(a.base);
            const std::string bs = path_str(b.base);
            if (as.size() != bs.size())
                return as.size() < bs.size();
            return as < bs;
        });

        /* Subsume child roots into active ancestor walks when the ancestor
         * already reaches deep enough, does not skip the child via exclude_dir,
         * and crosses no symlinked directory on the way down. */
        for (std::size_t j = 1; j < walks.size(); ++j) {
            auto& child = walks[j];
            for (std::size_t i = 0; i < j; ++i) {
                auto& anc = walks[i];
                if (anc.subsumed)
                    continue;
                const auto rel_info = syntactic_child_rel(anc.base, child.base);
                if (!rel_info)
                    continue;
                const auto& [rel_from_anc, depth_diff] = *rel_info;
                if (anc.max_depth.has_value()) {
                    if (!child.max_depth.has_value() || *anc.max_depth < depth_diff + *child.max_depth)
                        continue;
                }
                if (!anc.skip_rel.empty() && !abs_exclude.empty()) {
                    if (child.abs_base == abs_exclude || !strict_subpath(abs_exclude, child.abs_base).empty())
                        continue;
                }
                /* recursive_directory_iterator does not follow directory symlinks,
                 * whereas walking child.base directly opens through a symlink. */
                std::filesystem::path step = anc.base == "." ? std::filesystem::path {} : anc.base;
                bool crosses_symlink = false;
                for (const auto& part : rel_from_anc) {
                    step = step.empty() ? part : (step / part);
                    std::error_code sec;
                    if (std::filesystem::is_symlink(step, sec)) {
                        crosses_symlink = true;
                        break;
                    }
                }
                if (crosses_symlink)
                    continue;

                std::string child_prefix = path_str(child.base);
                if (!child_prefix.empty() && child_prefix.back() != '/')
                    child_prefix.push_back('/');
                for (auto& c : child.consumers) {
                    if (c.sub_prefix.empty())
                        c.sub_prefix = child_prefix;
                    anc.consumers.push_back(std::move(c));
                }
                child.consumers.clear();
                child.subsumed = true;
                break;
            }
        }

        for (const auto& w : walks) {
            if (w.subsumed)
                continue;
            std::error_code dir_ec;
            if (!std::filesystem::is_directory(w.base, dir_ec))
                continue; // a missing (or non-directory) base simply matches nothing
            std::error_code iter_ec;
            auto it = std::filesystem::recursive_directory_iterator(
                w.base, std::filesystem::directory_options::skip_permission_denied, iter_ec);
            const std::filesystem::recursive_directory_iterator end {};
            const bool base_is_dot = w.base == ".";
            for (; it != end; it.increment(iter_ec)) {
                if (iter_ec)
                    break;
                std::error_code entry_ec;
                const bool is_sym = it->is_symlink(entry_ec);
                const bool is_dir = !is_sym && it->is_directory(entry_ec);

                // check hiddenness only on components BELOW the literal prefix:
                // explicit ../vendor/*.cpp or .gen/*.cpp prefixes stay valid
                // while wildcard-discovered dotfiles/dotdirs are skipped
                const std::filesystem::path under_base = it->path().lexically_relative(w.base);
                if (is_hidden(under_base) || (!w.skip_rel.empty() && under_base == w.skip_rel)) {
                    if (is_dir)
                        it.disable_recursion_pending();
                    continue;
                }
                if (is_dir) {
                    if (w.max_depth && it.depth() >= *w.max_depth)
                        it.disable_recursion_pending();
                    continue;
                }
                if (!is_sym && !it->is_regular_file(entry_ec))
                    continue;

                const std::filesystem::path& rel = base_is_dot ? under_base : it->path();
                if (rel.empty())
                    continue;
                const std::string rel_str = path_str(rel);
                for (const auto& consumer : w.consumers) {
                    if (!consumer.sub_prefix.empty() && !rel_str.starts_with(consumer.sub_prefix))
                        continue;
                    for (const auto& pat : consumer.patterns) {
                        if (glob_match(pat, rel_str)) {
                            results[consumer.group_idx].push_back(rel);
                            break;
                        }
                    }
                }
            }
            /* an increment error (an entry deleted by a concurrent process mid
             * walk) degrades to the matches collected so far — like fs::walk —
             * instead of failing the whole glob */
        }
    }

    for (auto& matches : results) {
        std::ranges::sort(matches);
        matches.erase(std::ranges::unique(matches).begin(), matches.end()); /* overlapping roots */
    }
    return results;
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
    const std::array<std::span<const std::string>, 1> groups {patterns};
    auto r = glob_groups_impl(groups, {});
    if (!r)
        return std::unexpected(r.error());
    return std::move((*r)[0]);
}

Result<std::vector<std::vector<std::filesystem::path>>> glob_batch(
    std::span<const std::vector<std::string>> groups,
    const std::filesystem::path& exclude_dir) {
    std::vector<std::span<const std::string>> views;
    views.reserve(groups.size());
    for (const auto& g : groups)
        views.emplace_back(g);
    return glob_groups_impl(views, exclude_dir);
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
    } else { // empty file or unseekable / zero-reported pseudo-file (/proc, /sys)
        in.clear();
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
     * atomic within a filesystem). The temp is created exclusively (O_EXCL)
     * under a pid+counter name: concurrent writers stay apart and a
     * pre-placed symlink at a guessed name is never followed. */
    static std::atomic<unsigned> tmp_seq {0};
    std::filesystem::path tmp;
    int fd = -1;
    for (int attempt = 0; attempt < 64; ++attempt) {
        tmp = p;
        tmp += ".truss-tmp." + std::to_string(getpid()) + "."
             + std::to_string(tmp_seq.fetch_add(1, std::memory_order_relaxed));
        fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0666);
        if (fd != -1 || errno != EEXIST)
            break;
    }
    if (fd == -1)
        return std::unexpected(Error {Error::Kind::Io, "failed to open file for writing", p});
    {
        size_t written = 0;
        while (written < data.size()) {
            const ssize_t n = ::write(fd, data.data() + written, data.size() - written);
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                ::close(fd);
                std::filesystem::remove(tmp, ec);
                return std::unexpected(Error {Error::Kind::Io, "failed to write file", p});
            }
            written += static_cast<std::size_t>(n);
        }
        if (::close(fd) != 0) {
            std::filesystem::remove(tmp, ec);
            return std::unexpected(Error {Error::Kind::Io, "failed to close file after writing", p});
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

std::string sha256_bytes(std::string_view data) {
    Sha256Ctx ctx;
    ctx.update(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
    return ctx.final_hex();
}

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

/* The path is taken BY VALUE: a coroutine frame stores reference parameters
 * as references, and the lazy generator first touches `dir` only when the
 * caller advances begin() — a temporary argument would dangle by then. */
generator<std::filesystem::path> walk(std::filesystem::path dir, WalkOptions opts) {
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
            const auto fname = it->path().filename().native();
            if (!fname.empty() && fname.front() == '.') {
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
/* the stream is MOVED into the frame: one open, no re-open TOCTOU between
 * the validation open and the read */
generator<std::string> lines_of(std::ifstream in) {
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') /* CRLF input */
            line.pop_back();
        co_yield line; // stream errors mid-file simply end the iteration
    }
}
} // namespace

Result<generator<std::string>> read_lines(const std::filesystem::path& p) {
    std::ifstream in(p);
    if (!in)
        return std::unexpected(Error {Error::Kind::Io, "failed to open file for reading", p});
    return lines_of(std::move(in));
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
