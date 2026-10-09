module;
#include <algorithm>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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
resolve_module_imports(const ModuleUnitInfo& u, const std::map<std::string, std::string, std::less<>>& bmis) {
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

    /* An implementation unit (module m; in a .cpp) implicitly imports its
     * primary interface — it needs the primary BMI even with no explicit import.
     * A BMI source never consumes its own BMI. */
    if (!u.module_name.empty() && !u.is_bmi_source) {
        if (!add_bmi(primary))
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
        if (name == "std")
            continue; /* import std; handled by the toolchain, not by truss BMIs */
        if (u.is_bmi_source && name == u.module_name)
            continue; /* a BMI source never consumes its own BMI */
        if (!add_bmi(name))
            return std::unexpected(Error {
                Error::Kind::Config,
                "'" + u.path + "' imports module '" + name + "', but no .cppm exporting it is part of "
                "this target (or its depends_on targets)",
                std::filesystem::path(u.path),
            });
    }
    return std::make_pair(flags, order);
}

} // namespace truss::detail
