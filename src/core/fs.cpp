module;
#include <algorithm>
#include <array>
#include <cctype>
#include <expected>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

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
                r += ".*";
                i += 2;
                if (i < pattern.size() && pattern[i] == '/') ++i; // "**/" consumes separator
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
        if (!std::filesystem::exists(base, ec))
            continue; // a missing base simply matches nothing
        ec.clear();
        auto it = std::filesystem::recursive_directory_iterator(
            base, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end {};
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            const std::filesystem::path rel = std::filesystem::relative(it->path(), ".", ec);
            if (ec) continue;
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
        if (ec)
            return std::unexpected(io_error(ec, base));
    }

    std::ranges::sort(matches);
    matches.erase(std::ranges::unique(matches).begin(), matches.end()); /* overlapping roots */
    return matches;
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

    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out)
        return std::unexpected(Error {Error::Kind::Io, "failed to open file for writing", p});
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
    if (!out)
        return std::unexpected(Error {Error::Kind::Io, "failed to write file", p});
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
        const auto hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);
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

} // namespace truss::detail
