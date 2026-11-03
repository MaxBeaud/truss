module;
#include <algorithm>
#include <expected>
#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

module truss;
import :api;
import :impl;

namespace truss::detail {

Result<ModuleUnitInfo> scan_module_unit(const std::string& path) {
    auto text = read_file(path);
    if (!text)
        return std::unexpected(text.error());

    static const std::regex module_re(
        R"(^[ \t]*(export[ \t]+)?module[ \t]+([A-Za-z_][A-Za-z0-9_:.]*)[ \t]*;)", std::regex::multiline);
    static const std::regex import_re(
        R"(^[ \t]*(export[ \t]+)?import[ \t]+([^;" \t][^;]*);)", std::regex::multiline);

    ModuleUnitInfo info;
    info.path = path;
    std::string s = std::move(*text);
    /* cheap pre-filter: no module/import keyword anywhere means a plain TU
     * (the regexes are line-anchored, so this only skips a superset) */
    if (s.find("module") == std::string::npos && s.find("import") == std::string::npos)
        return info;
    std::smatch m;
    if (std::regex_search(s, m, module_re))
        info.module_name = m[2];
    for (auto it = std::sregex_iterator(s.begin(), s.end(), import_re);
         it != std::sregex_iterator(); ++it) {
        const std::string name = (*it)[2];
        /* split partition lists like `import :a, :b;` — one stream for the whole line */
        std::istringstream in(name);
        for (std::string part; std::getline(in, part, ',');) {
            const auto b = part.find_first_not_of(" \t");
            const auto e = part.find_last_not_of(" \t");
            if (b != std::string::npos)
                info.imports.push_back(part.substr(b, e - b + 1));
        }
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
        if (std::ranges::find(added, kv.first) != added.end())
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
         * so consumers of the primary need every partition BMI too. */
        if (!name.contains(':')) {
            const std::string prefix = std::string(name) + ":";
            for (const auto& kv : bmis)
                if (kv.first.starts_with(prefix))
                    add_one(kv);
        }
        add_one(*it);
        return true;
    };

    /* A module's BMI embeds (or needs to resolve) everything it imports,
     * exported or not: wire the full transitive closure. The unit never
     * consumes its own BMI, so it starts out visited (also ends cycles). */
    std::set<std::string, std::less<>> visited;
    if (u.is_bmi_source)
        visited.insert(u.module_name);
    auto wire = [&](this auto&& self, const std::string& name) -> bool {
        if (!visited.insert(name).second)
            return true;
        const bool known = add_bmi(name);
        const auto visit_imports = [&](const std::string& mod) {
            const auto mi = module_imports.find(mod);
            if (mi == module_imports.end())
                return;
            const std::string mod_primary = mod.substr(0, mod.find(':'));
            for (const auto& raw : mi->second)
                self(raw.starts_with(':') ? mod_primary + raw : raw);
        };
        visit_imports(name);
        /* a primary also carries its partitions' imports */
        if (!name.contains(':')) {
            const std::string prefix = name + ":";
            for (const auto& kv : module_imports)
                if (kv.first.starts_with(prefix))
                    self(kv.first);
        }
        return known;
    };

    /* An implementation unit (module m; in a .cpp) implicitly imports its
     * primary interface — it needs the primary BMI even with no explicit import.
     * A BMI source never consumes its own BMI. */
    if (!u.module_name.empty() && !u.is_bmi_source) {
        if (!wire(primary))
            return std::unexpected(Error {
                Error::Kind::Config,
                "'" + u.path + "' is an implementation unit of module '" + primary +
                    "', but no .cppm exporting it is part of this target (or its depends_on targets)",
                std::filesystem::path(u.path),
            });
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
        if (!wire(name))
            return std::unexpected(Error {
                Error::Kind::Config,
                "'" + u.path + "' imports module '" + name + "', but no .cppm exporting it is part of "
                "this target (or its depends_on targets)",
                std::filesystem::path(u.path),
            });
    }
    return std::make_pair(flags, order);
}

/* Authoritative import list for one TU via clang-scan-deps (p1689 JSON).
 * One TU per invocation (a single-entry compilation database): this clang
 * build leaves "primary-output" empty in multi-TU CDB scans, so rules could
 * not be mapped back to files otherwise. ~10 ms per TU. */
Result<std::vector<std::string>>
scan_deps_requires(const std::string& compiler, std::string_view compile_flags,
                   const std::string& source, const std::filesystem::path& cwd,
                   const std::filesystem::path& scratch) {
    std::string flat = source;
    for (char& c : flat)
        if (c == '/' || c == '.')
            c = '_';
    const std::filesystem::path cdb = scratch / ("cdb_" + flat + ".json");
    const std::string cmd = compiler + " " + std::string(compile_flags) + " -c " + source;
    const nlohmann::json entry {
        {"directory", cwd.generic_string()},
        {"file", source},
        {"command", cmd},
    };
    if (auto r = write_file(cdb, nlohmann::json {entry}.dump()); !r)
        return std::unexpected(r.error());

    const std::vector<std::string> argv {
        "clang-scan-deps", "--format=p1689", "-compilation-database=" + cdb.generic_string(),
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

    const auto j = nlohmann::json::parse(outcome->output, nullptr, false);
    if (!j.is_object() || !j.contains("rules") || !j["rules"].is_array() || j["rules"].empty())
        return std::unexpected(Error {
            Error::Kind::Toolchain, "clang-scan-deps produced no p1689 rules for '" + source + "'",
            std::filesystem::path(source),
        });
    std::vector<std::string> names;
    const auto& rule = j["rules"][0];
    if (rule.contains("requires") && rule["requires"].is_array())
        for (const auto& req : rule["requires"])
            if (req.is_object() && req.contains("logical-name") && req["logical-name"].is_string()
                && !req.value("isHeaderUnit", false))
                names.push_back(req["logical-name"].get<std::string>());
    return names;
}

} // namespace truss::detail
