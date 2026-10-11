module;
#include <algorithm>
#include <cctype>
#include <expected>
#include <string>
#include <string_view>

module truss;
import :api;
import :impl;

namespace truss {

TargetTriple host_triple() {
    TargetTriple t;
    t.os = os::host_os();
    t.arch = os::host_arch();
    t.family = os::host_family();

    if (auto cxx = os::env("CXX"); cxx)
        t.compiler = *cxx;
    else {
        switch (t.family) {
        case CompilerFamily::Clang: t.compiler = "clang++"; break;
        case CompilerFamily::Gcc: t.compiler = "g++"; break;
        case CompilerFamily::Msvc: t.compiler = "cl.exe"; break;
        default: t.compiler = "c++"; break;
        }
    }

    if (t.family == CompilerFamily::Clang && os::program_exists("llvm-ar"))
        t.ar = "llvm-ar";
    else if (t.family == CompilerFamily::Msvc)
        t.ar = "lib.exe"; /* MSVC archiver (llvm-lib understands the same syntax) */
    else
        t.ar = "ar";

    return t;
}

namespace detail {

CompilerFamily family_from_compiler_name(std::string_view name) {
    /* basename, lowercased; version/target-suffixes tolerated (clang++-21,
     * x86_64-w64-mingw32-g++). Paths are accepted: only the filename counts. */
    std::string base;
    for (auto it = name.rbegin(); it != name.rend(); ++it) {
        if (*it == '/' || *it == '\\') break;
        base.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*it))));
    }
    std::reverse(base.begin(), base.end());

    if (base.find("clang-cl") != std::string::npos) return CompilerFamily::Msvc;
    if (base == "cl" || base == "cl.exe") return CompilerFamily::Msvc;
    if (base.find("clang") != std::string::npos) return CompilerFamily::Clang;
    if (base.find("tcc") != std::string::npos) return CompilerFamily::Tcc;
    if (base.find("gcc") != std::string::npos || base.find("g++") != std::string::npos)
        return CompilerFamily::Gcc;
    return CompilerFamily::Unknown;
}

Result<TargetTriple> apply_config(const Config& conf, const TargetTriple& base) {
    TargetTriple t = base;
    if (!conf.compiler().empty()) {
        t.compiler = conf.compiler();
        if (auto fam = family_from_compiler_name(t.compiler); fam != CompilerFamily::Unknown)
            t.family = fam;
        /* archiver follows the family unless set explicitly */
        if (conf.ar().empty()) {
            if (t.family == CompilerFamily::Clang)
                t.ar = os::program_exists("llvm-ar") ? "llvm-ar" : "ar";
            else if (t.family == CompilerFamily::Msvc)
                t.ar = "lib.exe";
            else
                t.ar = "ar";
        }
    }
    if (!conf.ar().empty()) t.ar = conf.ar();
    if (!conf.linker().empty()) t.linker = conf.linker();
    if (conf.stdlib() != StdLib::Default) t.stdlib = conf.stdlib();

    if (t.stdlib != StdLib::Default) {
        if (t.family == CompilerFamily::Msvc)
            return std::unexpected(Error {
                Error::Kind::Config,
                "stdlib() cannot be used with MSVC targets: cl.exe/clang-cl always use the MSVC STL",
            });
        if (t.family == CompilerFamily::Gcc && t.stdlib == StdLib::Libcxx)
            return std::unexpected(Error {
                Error::Kind::Config,
                "g++ cannot select libc++ (-stdlib= is a clang flag); "
                "use conf.toolchain(truss::Toolchain::LLVM) for clang++ + libc++",
            });
    }
    return t;
}

/* compiler-cache launcher: explicit Config::launcher() wins ("" opts out,
 * a missing program is a config error); otherwise auto-detect ccache then
 * sccache on PATH. */
Result<std::string> resolve_launcher(const Config& conf) {
    if (conf.launcher_set()) {
        if (conf.launcher().empty())
            return std::string {};
        if (!os::program_exists(conf.launcher()))
            return std::unexpected(Error {
                Error::Kind::Config,
                "launcher '" + conf.launcher() + "' not found (install it or use conf.launcher(\"\") to disable)",
            });
        return conf.launcher();
    }
    if (os::program_exists("ccache"))
        return std::string {"ccache"};
    if (os::program_exists("sccache"))
        return std::string {"sccache"};
    return std::string {};
}

} // namespace detail

} // namespace truss
