module;
#include <algorithm>
#include <atomic>
#include <cctype>
#include <expected>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>
#include <nlohmann/json.hpp>

module truss;
import :api;
import :impl;

namespace truss::detail {

namespace {

/* True when the R at s[i] introduces a raw string (R"delim( ), optionally
 * behind an encoding prefix (u8R, uR, UR, LR) — and not the tail of a longer
 * identifier (abcR"x" is an identifier followed by a plain string). */
bool raw_string_at(std::string_view s, std::size_t i) {
    if (i + 1 >= s.size() || s[i + 1] != '"')
        return false;
    const auto ident = [](char c) {
        return static_cast<bool>(std::isalnum(static_cast<unsigned char>(c))) || c == '_';
    };
    std::size_t j = i;
    if (j > 0 && (s[j - 1] == 'u' || s[j - 1] == 'U' || s[j - 1] == 'L'))
        --j;
    else if (j > 1 && s[j - 1] == '8' && s[j - 2] == 'u')
        j -= 2;
    return j == 0 || !ident(s[j - 1]);
}

/* Blank out everything that is not code — comments, string literals (plain and
 * raw, any encoding prefix) and char literals — keeping every newline so the
 * line-anchored regexes still see the original line structure. Without this,
 * test sources embedding TS/HTML in raw strings get their `import ...;` lines
 * scanned as C++ module imports. */
std::string strip_non_code(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    const auto blank = [&](char c) { out += c == '\n' ? '\n' : ' '; };
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') { /* line comment */
            while (i < s.size() && s[i] != '\n')
                blank(s[i++]);
        } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') { /* block comment */
            blank(s[i++]);
            blank(s[i++]);
            while (i < s.size()) {
                if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '/') {
                    blank(s[i++]);
                    blank(s[i++]);
                    break;
                }
                blank(s[i++]);
            }
        } else if (c == 'R' && raw_string_at(s, i)) { /* raw string */
            std::size_t j = i + 2;
            while (j < s.size() && s[j] != '(' && s[j] != ')' && s[j] != '\\'
                   && !std::isspace(static_cast<unsigned char>(s[j])))
                ++j;
            std::size_t stop = s.size();
            if (j < s.size() && s[j] == '(' && j - (i + 2) <= 16) {
                const std::string close = ")" + std::string(s.substr(i + 2, j - (i + 2))) + "\"";
                const std::size_t end = s.find(close, j + 1);
                if (end != std::string_view::npos)
                    stop = end + close.size();
            } else /* malformed: blank the rest of the line like a plain string */
                for (stop = i; stop < s.size() && s[stop] != '\n'; ++stop) {}
            while (i < stop)
                blank(s[i++]);
        } else if (c == '"' || c == '\'') { /* plain string or char literal */
            /* an apostrophe is a digit separator after any hex/dec digit
             * (1'000, 0xdead'beef) — EXCEPT right after a `u8` char-literal
             * prefix (u8'a'), where the '8' is part of the encoding prefix */
            const auto is_ident = [](char ch) {
                return static_cast<bool>(std::isalnum(static_cast<unsigned char>(ch))) || ch == '_';
            };
            const bool is_u8_prefix =
                i >= 2 && s[i - 1] == '8' && s[i - 2] == 'u'
                && (i == 2 || !is_ident(s[i - 3]));
            if (c == '\'' && i > 0 && std::isxdigit(static_cast<unsigned char>(s[i - 1]))
                && !is_u8_prefix) {
                out += c;
                ++i;
                continue;
            }
            /* keep outer '"' quotes on plain strings (blanking their contents)
             * so `import "hdr.h";` remains recognizable as a header-unit import
             * while `import` inside string text is still erased */
            if (c == '"') {
                out += '"';
                ++i;
            } else {
                blank(s[i++]);
            }
            while (i < s.size() && s[i] != c) {
                const bool esc = s[i] == '\\' && i + 1 < s.size();
                blank(s[i++]);
                if (esc && i < s.size())
                    blank(s[i++]);
            }
            if (i < s.size()) {
                if (c == '"') {
                    out += '"';
                    ++i;
                } else {
                    blank(s[i++]); /* closing quote */
                }
            }
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

} // namespace

Result<ModuleUnitInfo> scan_module_unit(const std::string& path) {
    auto text = read_file(path);
    if (!text)
        return std::unexpected(text.error());

    ModuleUnitInfo info;
    info.path = path;
    const std::string raw = std::move(*text);
    /* cheap pre-filter on the RAW text first: a file that never mentions
     * module/import anywhere (the majority of TUs) skips the strip+scan
     * work entirely (the strip can only remove matches, never add them) */
    info.mentions_module_syntax =
        raw.find("module") != std::string::npos || raw.find("import") != std::string::npos;
    if (!info.mentions_module_syntax)
        return info;
    /* comments and string/char/raw literals are blanked (1-to-1 length
     * preserving): module and import statements only count when real code */
    const std::string s = strip_non_code(raw);

    /* Hand-rolled line scan (replaced two std::regex passes — the ECMAScript
     * engine cost microseconds per attempted line on the configure hot path).
     * Semantics preserved: a statement starts at a line whose only leading
     * whitespace may be followed by `export`, then the keyword; the statement
     * runs to the first `;` (import lists may wrap lines). Line-spliced
     * imports (`import \<newline> math;`) are still missed — the authoritative
     * scan for those is clang-scan-deps (ModulesMode::Manual / Auto below). */
    const auto is_kw_at = [&](std::size_t i, std::string_view kw) {
        return i + kw.size() < s.size() && s.compare(i, kw.size(), kw) == 0
            && (s[i + kw.size()] == ' ' || s[i + kw.size()] == '\t');
    };
    std::size_t pos = 0;
    while (pos < s.size()) {
        std::size_t i = pos;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            ++i;
        if (is_kw_at(i, "export")) {
            i += 7;
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
                ++i;
        }
        if (is_kw_at(i, "module")) {
            i += 7;
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
                ++i;
            std::size_t j = i;
            if (j < s.size() && (std::isalpha(static_cast<unsigned char>(s[j])) || s[j] == '_'))
                while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_'
                                        || s[j] == ':' || s[j] == '.'))
                    ++j;
            std::size_t k = j;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t'))
                ++k;
            if (j > i && k < s.size() && s[k] == ';')
                info.module_name = s.substr(i, j - i); /* bare `module;` (GMF) leaves it empty */
        } else if (is_kw_at(i, "import")) {
            i += 7;
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
                ++i;
            const auto semi = s.find(';', i);
            if (semi != std::string::npos) {
                const std::string_view cap(s.data() + i, semi - i);
                /* header units (`import <x>;` / `import "x";`) get no BMI
                 * wiring — but they are recorded so the build can say so
                 * explicitly instead of failing opaquely at compile time */
                if (!cap.empty() && (cap.front() == '<' || cap.front() == '"')) {
                    /* strip_non_code preserves byte offsets 1:1 and blanks the
                     * interior of `"..."`, so read the original header spelling
                     * from `raw` for clear diagnostics */
                    std::string_view orig(raw.data() + i, semi - i);
                    while (!orig.empty() && (orig.back() == ' ' || orig.back() == '\t' || orig.back() == '\r'))
                        orig.remove_suffix(1);
                    info.header_imports.emplace_back(orig);
                } else if (!cap.empty() && cap.front() != ';'
                           && cap.front() != ' ' && cap.front() != '\t') {
                    /* split partition lists like `import :a, :b;` */
                    for (auto part_rng : std::views::split(cap, ',')) {
                        if (part_rng.empty())
                            continue; /* `import a,;` — an empty part between separators */
                        const std::string_view part(&*part_rng.begin(),
                                                    static_cast<std::size_t>(std::ranges::distance(part_rng)));
                        const auto b = part.find_first_not_of(" \t\r\n");
                        const auto e = part.find_last_not_of(" \t\r\n");
                        if (b == std::string_view::npos)
                            continue;
                        const std::string_view name = part.substr(b, e - b + 1);
                        /* a backslash means the import was line-spliced: the
                         * heuristic scan misses it by design (clang-scan-deps
                         * preprocesses and finds it) */
                        if (!name.contains('\\'))
                            info.imports.emplace_back(name);
                    }
                }
                pos = semi; /* keep scanning after the statement */
            }
        }
        const auto eol = s.find('\n', pos);
        pos = eol == std::string::npos ? s.size() : eol + 1;
    }
    return info;
}

Result<std::pair<std::string, std::vector<std::string>>>
resolve_module_imports(const ModuleUnitInfo& u, const std::map<std::string, std::string, std::less<>>& bmis,
                       const std::map<std::string, std::vector<std::string>, std::less<>>& module_imports) {
    std::string flags;
    std::vector<std::string> order;
    std::vector<std::string> added; /* dedupe module names already wired in */
    const std::string primary = u.module_name.substr(0, u.module_name.find(':'));

    auto add_one = [&](const std::pair<const std::string, std::string>& kv) {
        if (std::ranges::contains(added, kv.first))
            return;
        added.push_back(kv.first);
        if (!flags.empty())
            flags += ' ';
        flags += "-fmodule-file=" + kv.first + "=" + kv.second;
        order.push_back(kv.second);
    };

    auto add_bmi = [&](std::string_view name) {
        const auto it = bmis.find(name);
        if (it == bmis.end())
            return false;
        /* A primary interface's BMI references its partitions (export import),
         * so consumers of the primary need every partition BMI too. The map is
         * sorted: start at the prefix and stop at the first non-match. */
        if (!name.contains(':')) {
            const std::string prefix = std::string(name) + ":";
            for (auto p = bmis.lower_bound(prefix); p != bmis.end() && p->first.starts_with(prefix); ++p)
                add_one(*p);
        }
        add_one(*it);
        return true;
    };

    /* A module's BMI embeds (or needs to resolve) everything it imports,
     * exported or not: wire the full transitive closure. The unit never
     * consumes its own BMI, so it starts out visited (also ends cycles).
     * An unresolved module anywhere in the closure fails the walk — a missing
     * transitive BMI would otherwise surface only as an opaque clang error.
     * std / std.compat are exempt: they are wired from the toolchain's std
     * manifest (std_for), never from target BMIs. */
    std::string first_unresolved, unresolved_importer;
    std::set<std::string, std::less<>> visited;
    if (u.is_bmi_source)
        visited.insert(u.module_name);
    auto wire = [&](this auto&& self, const std::string& name,
                    const std::string& importer) -> bool {
        if (!visited.insert(name).second)
            return true;
        const bool known = add_bmi(name);
        bool ok = true;
        if (!known && name != "std" && name != "std.compat") {
            ok = false;
            if (first_unresolved.empty()) {
                first_unresolved = name;
                unresolved_importer = importer;
            }
        }
        const auto visit_imports = [&](const std::string& mod) {
            const auto mi = module_imports.find(mod);
            if (mi == module_imports.end())
                return;
            const std::string mod_primary = mod.substr(0, mod.find(':'));
            for (const auto& raw : mi->second)
                if (!self(raw.starts_with(':') ? mod_primary + raw : raw, mod))
                    ok = false;
        };
        visit_imports(name);
        /* a primary also carries its partitions' imports (sorted map: prefix
         * range instead of a full scan) */
        if (!name.contains(':')) {
            const std::string prefix = name + ":";
            for (auto p = module_imports.lower_bound(prefix);
                 p != module_imports.end() && p->first.starts_with(prefix); ++p)
                if (!self(p->first, name))
                    ok = false;
        }
        return ok;
    };

    /* the error names the module that actually has no BMI: the direct import,
     * or the transitive one the closure stumbled over */
    const auto unresolved = [&](const std::string& name, const std::string& what) -> Error {
        if (first_unresolved.empty() || first_unresolved == name)
            return Error {
                Error::Kind::Config,
                "'" + u.path + "' " + what + " '" + name +
                    "', but no .cppm exporting it is part of this target (or its depends_on targets)",
                std::filesystem::path(u.path),
            };
        return Error {
            Error::Kind::Config,
            "'" + u.path + "' needs module '" + first_unresolved + "' (imported by '" +
                unresolved_importer + "'), but no .cppm exporting it is part of "
                "this target (or its depends_on targets)",
            std::filesystem::path(u.path),
        };
    };

    /* An implementation unit (module m; in a .cpp) implicitly imports its
     * primary interface — it needs the primary BMI even with no explicit import.
     * A BMI source never consumes its own BMI. */
    if (!u.module_name.empty() && !u.is_bmi_source) {
        if (!wire(primary, u.path))
            return std::unexpected(unresolved(
                primary, "is an implementation unit of module"));
    }

    for (const auto& raw : u.imports) {
        std::string name = raw;
        if (name.starts_with(':')) {
            if (primary.empty())
                return std::unexpected(Error {
                    Error::Kind::Config,
                    "partition import '" + raw + "' in '" + u.path + "' but the file has no module declaration",
                    std::filesystem::path(u.path),
                });
            name = primary + name;
        }
        if (u.is_bmi_source && name == u.module_name)
            continue; /* a BMI source never consumes its own BMI */
        if (!wire(name, u.path))
            return std::unexpected(unresolved(name, "imports module"));
    }
    return std::make_pair(flags, order);
}

std::optional<std::filesystem::path> find_clang_scan_deps(std::string_view compiler) {
    /* Extract the first token (driver path/name), then prefer a version-matched
     * clang-scan-deps in the compiler's directory before falling back to PATH. */
    std::string_view driver = compiler.substr(0, compiler.find(' '));
    if (driver.empty())
        return os::find_program("clang-scan-deps");
    const std::filesystem::path p(driver);
    const std::string base = p.filename().generic_string();
    std::string ver_suffix;
    if (const auto dash = base.rfind('-'); dash != std::string::npos && dash + 1 < base.size()) {
        const std::string_view tail = std::string_view(base).substr(dash + 1);
        if (std::ranges::all_of(tail, [](char c) { return c >= '0' && c <= '9'; }))
            ver_suffix = "-" + std::string(tail);
    }
    if (p.has_parent_path()) {
        const std::filesystem::path dir = p.parent_path();
        if (!ver_suffix.empty())
            if (auto found = os::find_program((dir / ("clang-scan-deps" + ver_suffix)).generic_string()))
                return found;
        if (auto found = os::find_program((dir / "clang-scan-deps").generic_string()))
            return found;
    }
    if (!ver_suffix.empty())
        if (auto found = os::find_program("clang-scan-deps" + ver_suffix))
            return found;
    return os::find_program("clang-scan-deps");
}

/* Authoritative import list for one TU via the compiler's p1689 output.
 * clang: clang-scan-deps over a single-entry compilation database (this clang
 * build leaves "primary-output" empty in multi-TU CDB scans, so rules could
 * not be mapped back to files otherwise); GCC: the compiler itself with
 * -fdeps-format=p1689r5 (GCC 14+). ~10 ms per TU. */
Result<std::pair<std::vector<std::string>, std::vector<std::string>>>
scan_deps_requires(const std::string& compiler, std::string_view compile_flags,
                   const std::string& source, const std::filesystem::path& cwd,
                   const std::filesystem::path& scratch) {
    std::string flat = source;
    for (char& c : flat)
        if (c == '/' || c == '.')
            c = '_';
    /* distinct sources can flatten to the same name (src/a/b.cpp vs
     * src/a_b.cpp) and concurrent workers share the scratch dir: a per-call
     * counter keeps each TU's compilation database apart (a hash could
     * collide, and std::hash is not stable across runs) */
    static std::atomic<unsigned> cdb_seq {0};
    const std::string seq = std::to_string(::getpid()) + "_"
                          + std::to_string(cdb_seq.fetch_add(1, std::memory_order_relaxed));

    const bool gcc = family_from_compiler_name(compiler) == CompilerFamily::Gcc;
    nlohmann::json j;
    std::vector<std::string> header_units;
    if (gcc) {
        /* the compiler scans itself: preprocess-only (-M) with p1689 module
         * deps to a scratch file; the imported CMIs need not exist yet */
        const std::filesystem::path deps = scratch / ("deps_" + seq + "_" + flat + ".json");
        const std::filesystem::path hdrs = scratch / ("deps_" + seq + "_" + flat + ".d");
        const auto scrub = [&] { /* both branches clean up their scratch files */
            std::error_code rm_ec;
            std::filesystem::remove(deps, rm_ec);
            std::filesystem::remove(hdrs, rm_ec);
        };
        /* compile_flags is ONE shell-shaped string (build_flags quotes paths
         * because it normally reaches a shell through ninja): tokenize it the
         * way /bin/sh would — double quotes group and are removed, a
         * backslash escapes the next character — so the scan receives exactly
         * the argv a shell would have built, spaces inside quoted paths
         * included */
        const auto shell_tokens = [](std::string_view line) {
            std::vector<std::string> out;
            std::string tok;
            bool in_dquote = false, in_squote = false, have_tok = false;
            for (std::size_t k = 0; k < line.size(); ++k) {
                const char c = line[k];
                if (in_squote) {
                    if (c == '\'')
                        in_squote = false;
                    else
                        tok += c;
                    have_tok = true;
                } else if (in_dquote) {
                    if (c == '"')
                        in_dquote = false;
                    else if (c == '\\' && k + 1 < line.size()) {
                        const char n = line[k + 1];
                        /* POSIX /bin/sh inside double quotes: backslash escapes
                         * only ", \, $, `, and newline; other backslashes stay */
                        if (n == '"' || n == '\\' || n == '$' || n == '`')
                            tok += line[++k];
                        else if (n == '\n')
                            ++k;
                        else
                            tok += c;
                    } else
                        tok += c;
                    have_tok = true;
                } else if (c == '"') {
                    in_dquote = true;
                    have_tok = true;
                } else if (c == '\'') {
                    in_squote = true;
                    have_tok = true;
                } else if (c == '\\') {
                    if (k + 1 < line.size())
                        tok += line[++k];
                    have_tok = true;
                } else if (c == ' ' || c == '\t') {
                    if (have_tok) {
                        out.push_back(std::move(tok));
                        tok.clear();
                        have_tok = false;
                    }
                } else {
                    tok += c;
                    have_tok = true;
                }
            }
            if (have_tok)
                out.push_back(std::move(tok));
            return out;
        };
        std::vector<std::string> argv {compiler};
        for (auto& tok : shell_tokens(compile_flags))
            argv.push_back(std::move(tok));
        argv.insert(argv.end(), {"-fmodules", "-M", "-MP",
            "-fdeps-format=p1689r5", "-fdeps-file=" + deps.generic_string(),
            "-fdeps-target=" + source, "-MF", hdrs.generic_string(), source});
        auto outcome = run_process_stdout(argv);
        if (!outcome) {
            scrub();
            return std::unexpected(outcome.error());
        }
        if (outcome->exit_code != 0) {
            scrub();
            return std::unexpected(Error {
                Error::Kind::Toolchain,
                "module scan failed for '" + source + "' (GCC 14+ required for -fdeps-format)",
                std::filesystem::path(source),
            });
        }
        auto text = read_file(deps);
        scrub();
        if (!text)
            return std::unexpected(Error {
                Error::Kind::Toolchain, "the GCC module scan wrote no p1689 output for '" + source + "'",
                std::filesystem::path(source),
            });
        j = nlohmann::json::parse(*text, nullptr, false);
    } else {
        const std::filesystem::path cdb =
            scratch / ("cdb_" + seq + "_" + flat + ".json");
        const std::string cmd = compiler + " " + std::string(compile_flags) + " -c " + source;
        const nlohmann::json entry {
            {"directory", cwd.generic_string()},
            {"file", source},
            {"command", cmd},
        };
        if (auto r = write_file(cdb, nlohmann::json {entry}.dump()); !r)
            return std::unexpected(r.error());

        const auto scanner = find_clang_scan_deps(compiler);
        const std::string scan_exe = scanner ? scanner->generic_string() : std::string {"clang-scan-deps"};
        const std::vector<std::string> argv {
            scan_exe, "--format=p1689", "-compilation-database=" + cdb.generic_string(),
        };
        auto outcome = run_process_stdout(argv);
        std::error_code rm_ec;
        std::filesystem::remove(cdb, rm_ec); /* best effort */
        if (!outcome)
            return std::unexpected(outcome.error());
        if (outcome->exit_code != 0)
            return std::unexpected(Error {
                Error::Kind::Toolchain,
                "clang-scan-deps failed for '" + source + "' (modules_mode(Manual) requires clang)",
                std::filesystem::path(source),
            });
        j = nlohmann::json::parse(outcome->output, nullptr, false);
    }

    if (!j.is_object() || !j.contains("rules") || !j["rules"].is_array() || j["rules"].empty())
        return std::unexpected(Error {
            Error::Kind::Toolchain, "the module scan produced no p1689 rules for '" + source + "'",
            std::filesystem::path(source),
        });
    std::vector<std::string> names;
    const auto& rule = j["rules"][0];
    if (rule.contains("requires") && rule["requires"].is_array())
        for (const auto& req : rule["requires"]) {
            if (!req.is_object() || !req.contains("logical-name") || !req["logical-name"].is_string())
                continue;
            if (req.value("isHeaderUnit", false)) {
                header_units.push_back(req["logical-name"].get<std::string>());
                continue;
            }
            const std::string name = req["logical-name"].get<std::string>();
            /* no compiler emits header-unit metadata yet (GCC routes direct
             * header imports into requires with the header's flat name, and
             * clang's named-module scan skips them): anything that is not a
             * named module is a header unit — reported, never dropped */
            if (name.contains('/') || name.starts_with('<') || name.starts_with('"')
                || name.ends_with(".h") || name.ends_with(".hpp") || name.ends_with(".hh")
                || name.ends_with(".hxx"))
                header_units.push_back(std::move(name));
            else
                names.push_back(std::move(name));
        }
    return std::make_pair(std::move(names), std::move(header_units));
}

} // namespace truss::detail
