module;
#include <array>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>

module truss;
import :api;
import :impl;

namespace truss::detail {
namespace {

void add(std::string& out, std::string_view flag) {
    if (flag.empty()) return;
    if (!out.empty()) out += ' ';
    out += flag;
}

/* GCC/Clang tables, indexed by the public enums (Default = 0 means "no flag"). */
constexpr std::array<std::string_view, 7> kStdFlags {
    "", "-std=c++11", "-std=c++14", "-std=c++17", "-std=c++20", "-std=c++23", "-std=c++26",
};
constexpr std::array<std::string_view, 5> kWarningFlags {
    "", "-w", "-Wall", "-Wall -Wextra", "-Wall -Wextra -Wpedantic",
};
constexpr std::array<std::string_view, 5> kDebugFlags {
    "", "-g0", "-g1", "-g2", "-g3",
};
constexpr std::array<std::string_view, 6> kOptimizationFlags {
    "", "-O0", "-O1", "-O2", "-Os", "-O3",
};
constexpr std::array<std::string_view, 5> kSanitizerCompileFlags {
    "", "", "-fsanitize=address -fno-omit-frame-pointer", "-fsanitize=undefined",
    "-fsanitize=address,undefined -fno-omit-frame-pointer",
};
constexpr std::array<std::string_view, 5> kSanitizerLinkFlags {
    "", "", "-fsanitize=address", "-fsanitize=undefined", "-fsanitize=address,undefined",
};
constexpr std::array<std::string_view, 4> kLtoCompileFlags {"", "", "-flto=full", "-flto=thin"};
constexpr std::array<std::string_view, 4> kLtoLinkFlags {"", "", "-flto=full", "-flto=thin"};

/* MSVC tables (validated against the clang-cl driver, which truss requires for
 * MSVC targets — see build.cpp; the flags are also accepted by cl.exe).
 * Notes: no C++11 mode exists (maps to C++14); C++23/26 spell /std:c++latest;
 * /Z7 embeds debug info in the .obj so parallel ninja never races on a PDB;
 * /O3 has no MSVC equivalent (maps to /O2); ThinLTO has no equivalent (/GL). */
constexpr std::array<std::string_view, 7> kStdFlagsMsvc {
    "/std:c++latest", "/std:c++14", "/std:c++14", "/std:c++17", "/std:c++20", "/std:c++latest",
    "/std:c++latest",
};
constexpr std::array<std::string_view, 5> kWarningFlagsMsvc {
    "", "/W0", "/W3", "/W4", "/W4",
};
constexpr std::array<std::string_view, 5> kDebugFlagsMsvc {
    "", "", "/Z7", "/Z7", "/Z7",
};
constexpr std::array<std::string_view, 6> kOptimizationFlagsMsvc {
    "", "/Od", "/O1", "/O2", "/Os", "/O2",
};
constexpr std::array<std::string_view, 5> kSanitizerCompileFlagsMsvc {
    "", "", "/fsanitize=address", "/fsanitize=undefined", "/fsanitize=address,undefined",
};
/* driver-side (before /link) and linker-side (after /link) halves */
constexpr std::array<std::string_view, 5> kSanitizerLinkDriverFlagsMsvc {
    "", "", "/fsanitize=address", "/fsanitize=undefined", "/fsanitize=address,undefined",
};
constexpr std::array<std::string_view, 5> kSanitizerLinkerFlagsMsvc {
    "", "", "/INCREMENTAL:NO", "", "/INCREMENTAL:NO",
};
constexpr std::array<std::string_view, 4> kLtoCompileFlagsMsvc {"", "", "/GL", "/GL"};
constexpr std::array<std::string_view, 4> kLtoLinkerFlagsMsvc {"", "", "/LTCG", "/LTCG"};

constexpr std::string_view os_name(Os os) {
    switch (os) {
    case Os::Linux: return "linux";
    case Os::Windows: return "windows";
    case Os::MacOS: return "macos";
    case Os::FreeBSD: return "freebsd";
    case Os::Android: return "android";
    case Os::Emscripten: return "emscripten";
    default: return "unknown";
    }
}

constexpr std::string_view arch_name(Arch arch) {
    switch (arch) {
    case Arch::X64: return "x64";
    case Arch::X86: return "x86";
    case Arch::Arm64: return "arm64";
    case Arch::Arm32: return "arm32";
    case Arch::Riscv64: return "riscv64";
    case Arch::Ppc64: return "ppc64";
    case Arch::S390x: return "s390x";
    case Arch::Wasm32: return "wasm32";
    default: return "unknown";
    }
}

constexpr std::string_view family_name(CompilerFamily f) {
    switch (f) {
    case CompilerFamily::Gcc: return "gcc";
    case CompilerFamily::Clang: return "clang";
    case CompilerFamily::Msvc: return "msvc";
    case CompilerFamily::Tcc: return "tcc";
    default: return "unknown";
    }
}

Result<void> require_supported(const TargetTriple& t) {
    if (t.family != CompilerFamily::Gcc && t.family != CompilerFamily::Clang &&
        t.family != CompilerFamily::Msvc)
        return std::unexpected(Error {Error::Kind::Toolchain, "unknown compiler family for target '" + t.compiler + "'"});
    return {};
}

} // namespace

std::string triple_dir(const TargetTriple& t) {
    return std::string(arch_name(t.arch)) + "-" + std::string(os_name(t.os)) + "-" + std::string(family_name(t.family));
}

DebugInfo resolve_debug(BuildMode mode, DebugInfo debug) {
    return debug == DebugInfo::Default
               ? (mode == BuildMode::Debug ? DebugInfo::Full : DebugInfo::None)
               : debug;
}

Result<FlagSet> build_flags(const TargetTriple& triple, BuildMode mode,
                            CppStandard std_opt, Warnings warnings, DebugInfo debug,
                            Optimization optimization, Sanitizer sanitizer, Lto lto,
                            bool pic, std::span<const std::string> defines,
                            std::span<const std::string> includes,
                            std::string_view user_flags) {
    if (auto r = require_supported(triple); !r)
        return std::unexpected(r.error());

    const bool msvc = triple.family == CompilerFamily::Msvc;
    FlagSet fs;
    /* C++ and C variants share everything except the -std flag */
    const auto stdi = static_cast<std::size_t>(std_opt == CppStandard::Default ? CppStandard::Cpp23 : std_opt);
    add(fs.compile, msvc ? kStdFlagsMsvc[stdi] : kStdFlags[stdi]);
    add(fs.compile_c, msvc ? "/std:c17" : "-std=gnu17");
    auto add_both = [&](std::string_view f) {
        add(fs.compile, f);
        add(fs.compile_c, f);
    };
    if (msvc) add_both("/utf-8 /EHsc /nologo");
    add_both(msvc ? kWarningFlagsMsvc[static_cast<std::size_t>(warnings)]
                  : kWarningFlags[static_cast<std::size_t>(warnings)]);
    add_both(msvc ? kDebugFlagsMsvc[static_cast<std::size_t>(resolve_debug(mode, debug))]
                  : kDebugFlags[static_cast<std::size_t>(resolve_debug(mode, debug))]);

    const Optimization opt = optimization == Optimization::Default
                                 ? (mode == BuildMode::Debug ? Optimization::None : Optimization::O2)
                                 : optimization;
    add_both(msvc ? kOptimizationFlagsMsvc[static_cast<std::size_t>(opt)]
                  : kOptimizationFlags[static_cast<std::size_t>(opt)]);

    add_both(msvc ? kSanitizerCompileFlagsMsvc[static_cast<std::size_t>(sanitizer)]
                  : kSanitizerCompileFlags[static_cast<std::size_t>(sanitizer)]);
    add_both(msvc ? kLtoCompileFlagsMsvc[static_cast<std::size_t>(lto)]
                  : kLtoCompileFlags[static_cast<std::size_t>(lto)]);
    if (pic && !msvc) add_both("-fPIC"); /* Windows has no position-independent code */
    if (mode == BuildMode::Release) add_both(msvc ? "/DNDEBUG" : "-DNDEBUG");

    for (const auto& d : defines) add_both((msvc ? "/D" : "-D") + d);
    for (const auto& i : includes) add_both(msvc ? "/I" + i : "-I\"" + i + "\"");

    add_both(user_flags);
    return fs;
}

Result<LinkFlags> link_flags(const TargetTriple& triple, BuildMode mode, DebugInfo debug,
                             Sanitizer sanitizer, Lto lto,
                             std::span<const std::string> link_paths,
                             std::span<const std::string> libs,
                             std::span<const std::string> frameworks,
                             std::span<const std::string> framework_paths,
                             std::string_view user_linker_flags) {
    if (auto r = require_supported(triple); !r)
        return std::unexpected(r.error());

    LinkFlags lf;
    if (!triple.linker.empty())
        add(lf.driver, "-fuse-ld=" + triple.linker);
    if (triple.family == CompilerFamily::Msvc) {
        /* driver-side flags go before `/link`, linker-side after it */
        add(lf.driver, kSanitizerLinkDriverFlagsMsvc[static_cast<std::size_t>(sanitizer)]);
        add(lf.linker, kSanitizerLinkerFlagsMsvc[static_cast<std::size_t>(sanitizer)]);
        add(lf.linker, kLtoLinkerFlagsMsvc[static_cast<std::size_t>(lto)]);
        if (resolve_debug(mode, debug) != DebugInfo::None)
            add(lf.linker, "/DEBUG");
        for (const auto& p : link_paths) add(lf.linker, "/LIBPATH:" + p);
        for (const auto& l : libs) add(lf.linker, l + ".lib");
        if (!frameworks.empty() || !framework_paths.empty())
            return std::unexpected(Error {
                Error::Kind::Config, "link_frameworks/framework_paths are Apple-only and cannot be used on MSVC targets",
            });
    } else {
        add(lf.driver, kSanitizerLinkFlags[static_cast<std::size_t>(sanitizer)]);
        add(lf.driver, kLtoLinkFlags[static_cast<std::size_t>(lto)]);
        for (const auto& p : link_paths) add(lf.linker, "-L\"" + p + "\"");
        for (const auto& p : framework_paths) add(lf.linker, "-F\"" + p + "\"");
        for (const auto& l : libs) add(lf.linker, "-l" + l);
        for (const auto& f : frameworks) add(lf.linker, "-framework " + f);
    }
    add(lf.linker, user_linker_flags);
    return lf;
}

} // namespace truss::detail
