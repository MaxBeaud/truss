// truss init — project scaffolding, compiled into every truss binary.
//
// The template files are embedded at compile time with #embed (C++26 /
// P1967) from cli/templates/init/, so the binary is self-contained and the
// templates stay plain files in the repo
// (cli/templates/init/*.in, @NAME@ / @IDENT@ / @TRUSS_LIB@ substituted at
// generation time). Include after `import truss;`.
#pragma once

#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace truss::cli {
namespace init_detail {

/* #embed is standard C++26; clang still flags it under -Wpedantic */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#endif
inline constexpr char kTrussCpp[] = {
#embed "templates/init/truss.cpp.in"
};
inline constexpr char kCoreHpp[] = {
#embed "templates/init/core.hpp.in"
};
inline constexpr char kCoreCpp[] = {
#embed "templates/init/core.cpp.in"
};
inline constexpr char kMainCpp[] = {
#embed "templates/init/main.cpp.in"
};
inline constexpr char kTestCpp[] = {
#embed "templates/init/test_core.cpp.in"
};
inline constexpr char kGitignore[] = {
#embed "templates/init/gitignore.in"
};
inline constexpr char kBootstrap[] = {
#embed "templates/init/bootstrap.sh.in"
};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

struct Template {
    std::string_view path; /* relative to the project dir; @NAME@ expanded */
    std::string_view body;
    bool executable;
};

template <std::size_t N>
consteval std::string_view text(const char (&bytes)[N]) { return {bytes, N}; }

inline constexpr std::array kTemplates {
    Template {"truss.cpp", text(kTrussCpp), false},
    Template {"include/@NAME@/core.hpp", text(kCoreHpp), false},
    Template {"src/core/core.cpp", text(kCoreCpp), false},
    Template {"src/app/main.cpp", text(kMainCpp), false},
    Template {"tests/test_core.cpp", text(kTestCpp), false},
    Template {".gitignore", text(kGitignore), false},
    Template {"bootstrap.sh", text(kBootstrap), true},
};
static_assert(kTemplates[0].body.starts_with("// @NAME@"), "init templates not embedded");

inline std::string expand(std::string_view text, std::span<const std::pair<std::string_view, std::string>> vars) {
    std::string out;
    out.reserve(text.size());
    while (!text.empty()) {
        const auto at = text.find('@');
        out += text.substr(0, at);
        if (at == std::string_view::npos)
            break;
        text.remove_prefix(at);
        bool hit = false;
        for (const auto& [key, value] : vars)
            if (text.starts_with(key)) {
                out += value;
                text.remove_prefix(key.size());
                hit = true;
                break;
            }
        if (!hit) {
            out += '@';
            text.remove_prefix(1);
        }
    }
    return out;
}

/* project names double as directory, target and file names */
inline bool valid_name(std::string_view name) {
    if (name.empty() || name.size() > 64 || name.front() == '-' || name.front() == '.')
        return false;
    for (const char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    return true;
}

/* C++ namespace from the project name: '-' -> '_', leading digit prefixed */
inline std::string identifier(std::string_view name) {
    std::string id(name);
    for (char& c : id)
        if (c == '-') c = '_';
    if (id.front() >= '0' && id.front() <= '9')
        id.insert(0, "_");
    return id;
}

} // namespace init_detail

/**
 * `truss init <name>`: scaffold ./<name>/ (build script, library + app +
 * test, .gitignore, bootstrap.sh wired to the installed truss modules).
 * Refuses to touch an existing non-empty directory.
 * \return process exit code.
 */
inline int init(std::span<char* const> args) {
    using namespace init_detail;
    if (args.size() != 1) {
        log::error("usage: truss init <project-name>");
        return 2;
    }
    const std::string_view name = args[0];
    if (!valid_name(name)) {
        log::error("init: invalid project name '" + std::string(name) + "'\n"
                   "  correct:   truss init my-app      (letters, digits, '-', '_')\n"
                   "  incorrect: truss init ../my-app   (no paths, no leading '-' or '.')");
        return 2;
    }

    const std::filesystem::path dir {name};
    std::error_code ec;
    if (std::filesystem::exists(dir, ec) && !std::filesystem::is_empty(dir, ec)) {
        log::error("init: '" + std::string(name) + "' already exists and is not empty");
        return 1;
    }

    /* the installed modules this binary ships with; bootstrap.sh keeps a
     * TRUSS_LIB override for when the install moves */
    const auto modules = os::find_truss_modules();
    std::string truss_lib = modules.empty() ? std::string {} : fs::generic_str(modules.front());
    if (truss_lib.empty()) {
        truss_lib = "$HOME/.local/lib/truss";
        log::warn("init: no installed truss modules found; bootstrap.sh defaults to " + truss_lib);
    }

    const std::array<std::pair<std::string_view, std::string>, 3> vars {{
        {"@NAME@", std::string(name)},
        {"@IDENT@", identifier(name)},
        {"@TRUSS_LIB@", truss_lib},
    }};
    for (const Template& t : kTemplates) {
        const std::filesystem::path path = dir / expand(t.path, vars);
        if (auto r = fs::write_text(path, expand(t.body, vars)); !r) {
            log::error(r.error());
            return 1;
        }
        if (t.executable) {
            std::filesystem::permissions(path,
                                         std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                                             std::filesystem::perms::others_exec,
                                         std::filesystem::perm_options::add, ec);
            if (ec) {
                log::error(Error {Error::Kind::Io,
                                  "init: cannot make bootstrap.sh executable (run chmod +x on it)",
                                  path});
                return 1;
            }
        }
    }

    log::success("created " + std::string(name) + "/");
    log::info("next steps:\n  cd " + std::string(name) + "\n  ./bootstrap.sh     # compile truss.cpp -> ./truss\n"
              "  ./truss test       # build + run the tests");
    return 0;
}

} // namespace truss::cli
