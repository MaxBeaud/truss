module;
#include <algorithm>
#include <cctype>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

module truss;
import :api;
import :impl;

namespace truss {

TargetTriple host_triple() {
    TargetTriple t;
    t.os = os::host_os();
    t.arch = os::host_arch();
    t.family = os::host_family();

    if (auto cxx = os::env("CXX"); cxx) {
        t.compiler = *cxx;
        /* the flag dialect follows $CXX, not the compiler that built the
         * script (CXX=g++ under a clang-built truss must get GCC flags) */
        if (const auto fam = detail::family_from_compiler_name(t.compiler); fam != CompilerFamily::Unknown)
            t.family = fam;
    } else {
        switch (t.family) {
        case CompilerFamily::Clang: t.compiler = "clang++"; break;
        case CompilerFamily::Gcc: t.compiler = "g++"; break;
        case CompilerFamily::Msvc: t.compiler = "cl.exe"; break;
        default: t.compiler = "c++"; break;
        }
    }

    if (detail::is_zig(t))
        t.ar = "zig ar";
    else if (t.family == CompilerFamily::Clang && os::program_exists("llvm-ar"))
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

    /* `zig c++` / `zig cc`: clang underneath, clang flag dialect */
    if (base == "zig" || base.starts_with("zig ") || base.starts_with("zig.exe")) return CompilerFamily::Clang;
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
            if (is_zig(t))
                t.ar = "zig ar";
            else if (t.family == CompilerFamily::Clang)
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
    if (!conf.target().empty()) {
        const ParsedTarget pt = parse_target(conf.target());
        if (pt.arch == Arch::Unknown || pt.os == Os::Unknown)
            return std::unexpected(Error {
                Error::Kind::Config,
                "target('" + conf.target() + "'): unrecognized arch or OS, expected <arch>-<os>[-<abi>], e.g:\n"
                "  correct:   \"aarch64-linux-gnu\", \"x86_64-windows-gnu\", \"x86_64-linux-musl\"\n"
                "  incorrect: \"arm64\", \"linux-x64\"",
            });
        t.target = conf.target();
        t.arch = pt.arch;
        t.os = pt.os;
    }

    if (t.stdlib != StdLib::Default && is_zig(t))
        return std::unexpected(Error {
            Error::Kind::Config,
            "stdlib() cannot be used with zig: `zig c++` always links its bundled libc++",
        });
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

bool is_zig(const TargetTriple& t) {
    /* first word of the driver command, basename only */
    std::string_view cmd = t.compiler;
    cmd = cmd.substr(0, cmd.find(' '));
    if (const auto slash = cmd.find_last_of("/\\"); slash != std::string_view::npos)
        cmd.remove_prefix(slash + 1);
    return cmd == "zig" || cmd == "zig.exe";
}

std::string c_driver(std::string_view cxx) {
    std::string out(cxx);
    /* only the basename's last occurrence is rewritten: "/opt/g++-tools/bin/g++"
     * keeps its directory */
    const auto base = out.find_last_of("/\\") == std::string::npos ? 0 : out.find_last_of("/\\") + 1;
    const auto swap = [&](std::string_view from, std::string_view to) {
        const auto at = out.rfind(from);
        if (at == std::string::npos || at < base)
            return false;
        out.replace(at, from.size(), to);
        return true;
    };
    if (swap("zig c++", "zig cc")) return out;
    if (swap("clang++", "clang")) return out; /* clang++-19 -> clang-19 */
    if (swap("g++", "gcc")) return out;       /* x86_64-linux-musl-g++ -> ...-gcc */
    if (swap("c++", "cc")) return out;
    return out; /* cl.exe / clang-cl / unknown: same driver for C */
}

ParsedTarget parse_target(std::string_view triple) {
    std::vector<std::string_view> parts;
    for (std::string_view rest = triple; !rest.empty();) {
        const auto dash = rest.find('-');
        parts.push_back(rest.substr(0, dash));
        if (dash == std::string_view::npos)
            break;
        rest.remove_prefix(dash + 1);
    }
    ParsedTarget pt;
    if (parts.empty())
        return pt;

    const std::string_view a = parts.front();
    if (a == "x86_64" || a == "amd64") pt.arch = Arch::X64;
    else if (a == "aarch64" || a == "arm64") pt.arch = Arch::Arm64;
    else if (a == "x86" || (a.size() == 4 && a.starts_with('i') && a.ends_with("86"))) pt.arch = Arch::X86;
    else if (a.starts_with("arm") || a.starts_with("thumb")) pt.arch = Arch::Arm32;
    else if (a == "riscv64") pt.arch = Arch::Riscv64;
    else if (a.starts_with("powerpc64") || a.starts_with("ppc64")) pt.arch = Arch::Ppc64;
    else if (a == "s390x") pt.arch = Arch::S390x;
    else if (a == "wasm32") pt.arch = Arch::Wasm32;

    size_t os_at = 0;
    for (size_t i = 1; i < parts.size() && pt.os == Os::Unknown; ++i) {
        const std::string_view c = parts[i];
        os_at = i;
        if (c == "linux") pt.os = Os::Linux;
        else if (c == "windows" || c == "mingw32" || c == "w64") pt.os = Os::Windows;
        else if (c == "macos" || c.starts_with("darwin") || c.starts_with("macosx")) pt.os = Os::MacOS;
        else if (c.starts_with("freebsd")) pt.os = Os::FreeBSD;
        else if (c == "emscripten") pt.os = Os::Emscripten;
    }
    /* the environment component follows the OS (x86_64-linux-musl,
     * aarch64-linux-android -> Android, x86_64-w64-mingw32 has none) */
    if (pt.os != Os::Unknown && os_at + 1 < parts.size()) {
        pt.abi = std::string(parts.back());
        if (pt.os == Os::Linux && pt.abi.starts_with("android"))
            pt.os = Os::Android;
        if (pt.os == Os::Windows && pt.abi == "mingw32")
            pt.abi = "gnu";
    }
    return pt;
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
