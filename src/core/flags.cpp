module;
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>

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

/* Quote one value for /bin/sh, escaping everything that stays special inside
 * double quotes ($ and ` would still substitute, " and \ would terminate or
 * escape). Paths legitimately contain spaces, so they are quoted rather than
 * rejected; this makes an include dir or link path that came from package
 * metadata unable to smuggle a command into the build line. MSVC command
 * lines are parsed by the CRT (no $/backtick expansion), so they keep the
 * plain `"value"` form. */
std::string sh_dq(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (const char c : value) {
        if (c == '"' || c == '\\' || c == '$' || c == '`')
            out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

/* -lNAME / -framework NAME are emitted unquoted (they are linker names, not
 * paths): reject anything a shell would interpret instead of trying to quote
 * it. A leading '-' is refused because it would become a linker option. */
Result<std::string> link_name(std::string_view value, std::string_view what) {
    if (value.empty() || value.front() == '-')
        return std::unexpected(Error {
            Error::Kind::Config,
            std::string(what) + " '" + std::string(value) + "' is not a valid linker name",
        });
    for (const char c : value)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '+'
              || c == '-' || c == '/'))
            return std::unexpected(Error {
                Error::Kind::Config,
                std::string(what) + " '" + std::string(value) +
                    "' contains characters the shell would interpret (letters, digits and . _ + - / only)",
            });
    return std::string(value);
}

/* GCC/Clang tables, indexed by the public enums (Default = 0 means "no flag"). */
constexpr std::array<std::string_view, 7> kStdFlags {
    "", "-std=c++11", "-std=c++14", "-std=c++17", "-std=c++20", "-std=c++23", "-std=c++26",
};
/* C standards (index = CStandard; Default keeps the historical gnu17). */
constexpr std::array<std::string_view, 5> kCStdFlags {
    "-std=gnu17", "-std=c99", "-std=c11", "-std=c17", "-std=c23",
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

/* Hardening tiers (index = resolved Hardening: None, Light, Medium, Max).
 * Default resolves to Light — truss's default hardening stack.
 * - Light: stack protector + hardened link (relro/full-bind/non-exec stack).
 * - Medium: + pattern-filled uninitialised locals, CFI + stack-clash probing
 *   (x86-64 only — clang errors elsewhere), libc++ hardened hardening mode.
 * - Max: Medium + libc++ debug hardening mode + libstdc++ assertions.
 * The stdlib defines are C++-only and inert against the other stdlib, so they
 * are emitted without knowing which one the target links. */
constexpr std::array<std::string_view, 5> kHardeningCompileFlags {
    "", "", "-fstack-protector-strong",
    "-fstack-protector-strong -ftrivial-auto-var-init=pattern",
    "-fstack-protector-strong -ftrivial-auto-var-init=pattern",
};
constexpr std::array<std::string_view, 5> kHardeningArchCompileFlags {
    "", "", "", "-fcf-protection=full -fstack-clash-protection",
    "-fcf-protection=full -fstack-clash-protection",
};
constexpr std::array<std::string_view, 5> kHardeningStdlibDefines {
    "", "", "",
    "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_HARDENED",
    "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG -D_GLIBCXX_ASSERTIONS",
};
constexpr std::array<std::string_view, 5> kHardeningLinkFlags {
    "", "", "-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,--as-needed",
    "-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,--as-needed",
    "-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,--as-needed",
};

/* MSVC tables (validated against the clang-cl driver, which truss requires for
 * MSVC targets — see build.cpp; the flags are also accepted by cl.exe).
 * Notes: no C++11 mode exists (maps to C++14); C++23/26 spell /std:c++latest;
 * /Z7 embeds debug info in the .obj so parallel ninja never races on a PDB;
 * /O3 has no MSVC equivalent (maps to /O2); ThinLTO has no equivalent (/GL). */
constexpr std::array<std::string_view, 7> kStdFlagsMsvc {
    "/std:c++latest", "/std:c++14", "/std:c++14", "/std:c++17", "/std:c++20", "/std:c++latest",
    "/std:c++latest",
};
/* MSVC has no C99/C23 mode: C11 and C17 only (validated below). */
constexpr std::array<std::string_view, 5> kCStdFlagsMsvc {
    "/std:c17", "", "/std:c11", "/std:c17", "",
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
constexpr std::array<std::string_view, 5> kSanitizerLinkDriverFlagsMsvc =
    kSanitizerCompileFlagsMsvc; /* same flags, driver side of /link */
constexpr std::array<std::string_view, 5> kSanitizerLinkerFlagsMsvc {
    "", "", "/INCREMENTAL:NO", "", "/INCREMENTAL:NO",
};
constexpr std::array<std::string_view, 4> kLtoCompileFlagsMsvc {"", "", "/GL", "/GL"};
constexpr std::array<std::string_view, 4> kLtoLinkerFlagsMsvc {"", "", "/LTCG", "/LTCG"};
/* MSVC hardening: Light = ASLR + non-exec stack (linker, any arch);
 * Medium/Max add CFI (/guard:cf compile+link) and /sdl — /guard:cf and
 * /CETCOMPAT are x86-64, so that half is arch-gated like GCC/Clang's. */
constexpr std::string_view kHardeningCompileCfMsvc {"/guard:cf"};
constexpr std::string_view kHardeningCompileSdlMsvc {"/sdl"};
constexpr std::string_view kHardeningLinkBaseMsvc {"/NXCOMPAT /DYNAMICBASE"};
constexpr std::string_view kHardeningLinkCfMsvc {"/guard:cf"};
constexpr std::string_view kHardeningLinkCetMsvc {"/CETCOMPAT"};

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

Hardening resolve_hardening(Hardening h) {
    /* the default hardening stack is Light */
    return h == Hardening::Default ? Hardening::Light : h;
}

Warnings resolve_warnings(Warnings w) {
    /* the default warning level is All (-Wall -Wextra) */
    return w == Warnings::Default ? Warnings::All : w;
}

Result<void> require_supported(const TargetTriple& t) {
    if (t.family != CompilerFamily::Gcc && t.family != CompilerFamily::Clang &&
        t.family != CompilerFamily::Msvc)
        return std::unexpected(Error {Error::Kind::Toolchain, "unknown compiler family for target '" + t.compiler + "'"});
    if (!t.target.empty() && t.family == CompilerFamily::Gcc) {
        /* a target-prefixed cross driver IS the GCC cross way: allow it when
         * the driver's basename stem matches the triple (x86_64-w64-mingw32-g++
         * for x86_64-w64-mingw32, aarch64-linux-gnu-g++-15, ...) */
        std::string base = std::filesystem::path(t.compiler).filename().generic_string();
        for (const std::string_view suffix : {"-g++", "-gcc", "-c++"})
            if (base.ends_with(suffix)) {
                base.erase(base.size() - suffix.size());
                break;
            }
        if (base != t.target && !base.starts_with(t.target))
            return std::unexpected(Error {
                Error::Kind::Config,
                "target('" + t.target + "'): GCC cross-compiles through a target-prefixed driver, not a flag, e.g:\n"
                "  correct:   Config().compiler(\"aarch64-linux-gnu-g++\")\n"
                "  correct:   Config().toolchain(truss::Toolchain::Zig).target(\"aarch64-linux-gnu\")\n"
                "  correct:   Config().compiler(\"x86_64-w64-mingw32-g++\").target(\"x86_64-w64-mingw32\")\n"
                "  incorrect: Config().toolchain(truss::Toolchain::GCC).target(\"aarch64-linux-gnu\")",
            });
        /* a lax prefix would let target("x86") match x86_64-... drivers: the
         * remainder must start on a separator or a version digit */
        if (base != t.target && base.size() > t.target.size()) {
            const char next = base[t.target.size()];
            if (std::isalnum(static_cast<unsigned char>(next)) && next != '-')
                return std::unexpected(Error {
                    Error::Kind::Config,
                    "target('" + t.target + "'): the driver '" + t.compiler +
                        "' names a different architecture",
                });
        }
        return {};
    }
    return {};
}

/* `-target` is zig's spelling; clang and clang-cl take `--target=`; GCC takes
 * neither — its target selection IS the prefixed driver name */
std::string target_flag(const TargetTriple& t) {
    if (t.target.empty())
        return {};
    if (t.family == CompilerFamily::Gcc)
        return {};
    return is_zig(t) ? "-target " + t.target : "--target=" + t.target;
}

/* Static C++ runtime flags (driver side / linker side, the latter placed
 * after the libraries that need it). Mixing runtimes across objects is an
 * ABI break, so every combination that would need a different stdlib than
 * the one the objects were compiled against is a Config error. */
Result<LinkFlags> static_stdlib_flags(const TargetTriple& t, StaticStdlib s) {
    LinkFlags lf;
    if (s == StaticStdlib::Default)
        return lf;
    if (t.family == CompilerFamily::Msvc || t.os == Os::MacOS)
        return std::unexpected(Error {
            Error::Kind::Config,
            "static_stdlib() is not supported on MSVC/macOS targets yet (MSVC: use flags(\"/MT\"); "
            "macOS ships no static libc++)",
        });
    const bool zig = is_zig(t);
    switch (s) {
    case StaticStdlib::Default: /* unreachable: returned above; keeps -Wswitch quiet */
        break;
    case StaticStdlib::Gcc:
        if (zig || (t.family == CompilerFamily::Clang && t.stdlib == StdLib::Libcxx))
            return std::unexpected(Error {
                Error::Kind::Config,
                "static_stdlib(Gcc) needs libstdc++, but this toolchain uses libc++ "
                "(zig bundles libc++ only); use static_stdlib(truss::StaticStdlib::Llvm)",
            });
        lf.driver = "-static-libstdc++ -static-libgcc";
        break;
    case StaticStdlib::Llvm:
        if (zig)
            break; /* zig always links its libc++/libc++abi statically */
        if (t.family != CompilerFamily::Clang || t.stdlib != StdLib::Libcxx)
            return std::unexpected(Error {
                Error::Kind::Config,
                "static_stdlib(Llvm) needs every object compiled against libc++, e.g:\n"
                "  correct:   proj.configure(Config().toolchain(truss::Toolchain::LLVM))\n"
                "  correct:   proj.configure(Config().stdlib(truss::StdLib::Libcxx))   // clang\n"
                "  incorrect: g++ / clang with its default stdlib",
            });
        /* clang's -static-libstdc++ only swaps libc++ and misses libc++abi
         * (undefined __cxxabiv1 typeinfo): drop the default C++ link and
         * name both archives explicitly, after the user's libraries */
        lf.driver = "-nostdlib++ -static-libgcc";
        lf.linker = "-Wl,-Bstatic -lc++ -lc++abi -Wl,-Bdynamic";
        break;
    case StaticStdlib::Musl:
        if (!t.target.contains("musl") && !t.compiler.contains("musl"))
            return std::unexpected(Error {
                Error::Kind::Config,
                "static_stdlib(Musl) produces a fully static musl binary and needs a musl toolchain, e.g:\n"
                "  correct:   Config().toolchain(truss::Toolchain::Zig).target(\"x86_64-linux-musl\")\n"
                "  correct:   Config().compiler(\"x86_64-linux-musl-g++\")\n"
                "  incorrect: a glibc toolchain (static glibc breaks NSS/dlopen at runtime)",
            });
        lf.driver = "-static";
        break;
    }
    return lf;
}

} // namespace

bool is_elf(Os os) {
    /* unknown hosts are assumed ELF (truss's historical default) */
    return os != Os::MacOS && os != Os::Windows && os != Os::Emscripten;
}

bool is_runtime_lib(std::string_view lib) {
    constexpr std::array<std::string_view, 14> kRuntime {
        "c", "m", "pthread", "dl", "rt", "util", "resolv", "crypt",
        "gcc_s", "gcc", "stdc++", "c++", "c++abi", "unwind",
    };
    return std::ranges::find(kRuntime, lib) != kRuntime.end();
}

std::string triple_dir(const TargetTriple& t) {
    /* cross builds keep a non-default ABI apart (x64-linux-musl-zig vs
     * x64-linux-zig), and zig gets its own family dir: same clang dialect,
     * different bundled libc/libc++ */
    std::string out = std::string(arch_name(t.arch)) + "-" + std::string(os_name(t.os));
    if (!t.target.empty())
        if (const std::string abi = parse_target(t.target).abi; !abi.empty() && abi != "gnu")
            out += "-" + abi;
    out += "-";
    out += is_zig(t) ? std::string_view {"zig"} : family_name(t.family);
    return out;
}

DebugInfo resolve_debug(BuildMode mode, DebugInfo debug) {
    return debug == DebugInfo::Default
               ? (mode == BuildMode::Debug ? DebugInfo::Full : DebugInfo::None)
               : debug;
}

/* The compile flags clang folds into a BMI's configuration check (hardening,
 * sanitizers, LTO): the std module's BMI must be built with the same settings
 * as its consumers, or clang rejects it with a config mismatch (llvm-mingw's
 * clang errors on a stack-protector difference where the Linux build happens
 * to tolerate it). build_flags emits the same strings, so consumer and std
 * module always agree. */
std::string bmi_config_flags(const TargetTriple& triple, Hardening hardening,
                             Sanitizer sanitizer, Lto lto) {
    const auto hi = static_cast<std::size_t>(resolve_hardening(hardening));
    const auto si = static_cast<std::size_t>(sanitizer);
    const auto li = static_cast<std::size_t>(lto);
    std::string out;
    add(out, kHardeningCompileFlags[hi]);
    if (triple.arch == Arch::X64)
        add(out, kHardeningArchCompileFlags[hi]);
    add(out, kHardeningStdlibDefines[hi]);
    add(out, kSanitizerCompileFlags[si]);
    add(out, kLtoCompileFlags[li]);
    return out;
}

Result<FlagSet> build_flags(const TargetTriple& triple, BuildMode mode,
                            CppStandard std_opt, Warnings warnings, DebugInfo debug,
                            Optimization optimization, Sanitizer sanitizer, Lto lto,
                            Hardening hardening,
                            bool pic, std::span<const std::string> defines,
                            std::span<const std::string> includes,
                            std::string_view user_flags,
                            std::span<const std::string> system_includes,
                            CStandard c_std, std::string_view user_c_flags,
                            bool symbols_hidden) {
    if (auto r = require_supported(triple); !r)
        return std::unexpected(r.error());

    const bool msvc = triple.family == CompilerFamily::Msvc;
    FlagSet fs;
    /* C++ and C variants share everything except the -std flag */
    const auto stdi = static_cast<std::size_t>(std_opt == CppStandard::Default ? CppStandard::Cpp23 : std_opt);
    add(fs.compile, msvc ? kStdFlagsMsvc[stdi] : kStdFlags[stdi]);
    const auto ci = static_cast<std::size_t>(c_std);
    if (ci >= kCStdFlags.size())
        return std::unexpected(Error {Error::Kind::Config, "invalid C standard selection"});
    if (msvc && (c_std == CStandard::C99 || c_std == CStandard::C23))
        return std::unexpected(Error {
            Error::Kind::Config,
            std::string("c_standard(") + (c_std == CStandard::C99 ? "C99" : "C23")
                + "): MSVC has no such C mode (C11 and C17 are the supported ones)",
        });
    add(fs.compile_c, msvc ? kCStdFlagsMsvc[ci] : kCStdFlags[ci]);
    auto add_both = [&](std::string_view f) {
        add(fs.compile, f);
        add(fs.compile_c, f);
    };
    /* -stdlib= is a clang driver flag; GCC's default is libstdc++ (no flag)
     * and GCC+libc++ is rejected earlier, so only clang gets it. */
    if (triple.stdlib != StdLib::Default && !msvc && triple.family == CompilerFamily::Clang)
        add_both(triple.stdlib == StdLib::Libcxx ? "-stdlib=libc++" : "-stdlib=libstdc++");
    add_both(target_flag(triple));
    if (msvc) add_both("/utf-8 /EHsc /nologo");
    const auto wi = static_cast<std::size_t>(resolve_warnings(warnings));
    add_both(msvc ? kWarningFlagsMsvc[wi] : kWarningFlags[wi]);
    const auto di = static_cast<std::size_t>(resolve_debug(mode, debug));
    add_both(msvc ? kDebugFlagsMsvc[di] : kDebugFlags[di]);

    const Optimization opt = optimization == Optimization::Default
                                 ? (mode == BuildMode::Debug ? Optimization::None : Optimization::O2)
                                 : optimization;
    add_both(msvc ? kOptimizationFlagsMsvc[static_cast<std::size_t>(opt)]
                  : kOptimizationFlags[static_cast<std::size_t>(opt)]);

    add_both(msvc ? kSanitizerCompileFlagsMsvc[static_cast<std::size_t>(sanitizer)]
                  : kSanitizerCompileFlags[static_cast<std::size_t>(sanitizer)]);
    add_both(msvc ? kLtoCompileFlagsMsvc[static_cast<std::size_t>(lto)]
                  : kLtoCompileFlags[static_cast<std::size_t>(lto)]);

    const Hardening hard = resolve_hardening(hardening);
    const auto hi = static_cast<std::size_t>(hard);
    const bool x64 = triple.arch == Arch::X64;
    if (msvc) {
        if (hi >= static_cast<std::size_t>(Hardening::Medium) && x64)
            add_both(kHardeningCompileCfMsvc);
        if (hard == Hardening::Max) add_both(kHardeningCompileSdlMsvc);
    } else {
        add_both(kHardeningCompileFlags[hi]);
        if (x64) add_both(kHardeningArchCompileFlags[hi]);
        /* stdlib hardening modes are C++-only defines (inert on the other stdlib) */
        if (hi >= static_cast<std::size_t>(Hardening::Medium))
            add(fs.compile, kHardeningStdlibDefines[hi]);
    }
    if (pic && !msvc && triple.os != Os::Windows)
        add_both("-fPIC"); /* Windows has no position-independent code (MinGW ignores -fPIC) */
    if (mode == BuildMode::Release) add_both(msvc ? "/DNDEBUG" : "-DNDEBUG");

    for (const auto& d : defines)
        if (!d.empty()) add_both((msvc ? "/D" : "-D") + d);
    for (const auto& i : includes)
        add_both(msvc ? "/I\"" + i + "\"" : "-I" + sh_dq(i));
    /* system dirs after -I (searched later); MSVC needs /external:W0 to
     * actually silence them */
    if (msvc && !system_includes.empty()) add_both("/external:W0");
    for (const auto& i : system_includes)
        add_both(msvc ? "/external:I\"" + i + "\"" : "-isystem " + sh_dq(i));

    add_both(user_flags);
    /* C-only options last, like flags() for both languages */
    add(fs.compile_c, user_c_flags);
    /* symbols_hidden(): exported symbols opt in (dllexport-style discipline).
     * MSVC exports are the model there, so the knob is a documented no-op. */
    if (symbols_hidden && !msvc) {
        add(fs.compile, "-fvisibility=hidden -fvisibility-inlines-hidden");
        add(fs.compile_c, "-fvisibility=hidden");
    }
    return fs;
}

Result<LinkFlags> link_flags(const TargetTriple& triple, BuildMode mode, DebugInfo debug,
                             Sanitizer sanitizer, Lto lto, Hardening hardening,
                             std::span<const std::string> link_paths,
                             std::span<const std::string> libs,
                             std::span<const std::string> frameworks,
                             std::span<const std::string> framework_paths,
                             std::string_view user_linker_flags,
                             std::span<const std::string> static_libs,
                             StaticStdlib static_stdlib) {
    if (auto r = require_supported(triple); !r)
        return std::unexpected(r.error());

    auto runtime = static_stdlib_flags(triple, static_stdlib);
    if (!runtime)
        return std::unexpected(runtime.error());

    LinkFlags lf;
    add(lf.driver, target_flag(triple));
    if (!triple.linker.empty())
        add(lf.driver, "-fuse-ld=" + triple.linker);
    const Hardening hard = resolve_hardening(hardening);
    const auto hi = static_cast<std::size_t>(hard);
    const auto di = static_cast<std::size_t>(resolve_debug(mode, debug));
    if (triple.family == CompilerFamily::Msvc) {
        /* driver-side flags go before `/link`, linker-side after it */
        add(lf.driver, kSanitizerLinkDriverFlagsMsvc[static_cast<std::size_t>(sanitizer)]);
        add(lf.linker, kSanitizerLinkerFlagsMsvc[static_cast<std::size_t>(sanitizer)]);
        add(lf.linker, kLtoLinkerFlagsMsvc[static_cast<std::size_t>(lto)]);
        if (hard != Hardening::None) add(lf.linker, kHardeningLinkBaseMsvc);
        if (triple.arch == Arch::X64) {
            if (hard >= Hardening::Medium) add(lf.linker, kHardeningLinkCfMsvc);
            if (hard == Hardening::Max) add(lf.linker, kHardeningLinkCetMsvc);
        }
        if (di != static_cast<std::size_t>(DebugInfo::None))
            add(lf.linker, "/DEBUG");
        for (const auto& p : link_paths) add(lf.linker, "/LIBPATH:\"" + p + "\"");
        for (const auto& l : libs) {
            /* link_libs("foo.lib") must not become foo.lib.lib */
            const std::string_view base = std::string_view(l).ends_with(".lib")
                                              ? std::string_view(l).substr(0, l.size() - 4)
                                              : std::string_view(l);
            auto name = link_name(base, "link library");
            if (!name)
                return std::unexpected(name.error());
            add(lf.linker, std::move(*name) + ".lib");
        }
        if (!static_libs.empty())
            return std::unexpected(Error {
                Error::Kind::Config,
                "static_link()/static_deps() are not supported on MSVC targets (a .lib is "
                "already either static or an import lib: pick the file with link_libs)",
            });
        if (!frameworks.empty() || !framework_paths.empty())
            return std::unexpected(Error {
                Error::Kind::Config, "link_frameworks/framework_paths are Apple-only and cannot be used on MSVC targets",
            });
    } else {
        add(lf.driver, kSanitizerLinkFlags[static_cast<std::size_t>(sanitizer)]);
        add(lf.driver, kLtoLinkFlags[static_cast<std::size_t>(lto)]);
        /* -z relro/now/noexecstack + --as-needed are GNU ld (ELF) options:
         * ld64 and the MinGW linker reject them */
        if (is_elf(triple.os))
            add(lf.driver, kHardeningLinkFlags[hi]);
        if (triple.stdlib != StdLib::Default && triple.family == CompilerFamily::Clang)
            add(lf.driver, triple.stdlib == StdLib::Libcxx ? "-stdlib=libc++" : "-stdlib=libstdc++");
        add(lf.driver, runtime->driver);
        for (const auto& p : link_paths) add(lf.linker, "-L" + sh_dq(p));
        for (const auto& p : framework_paths) add(lf.linker, "-F" + sh_dq(p));
        if (!static_libs.empty()) {
            if (triple.os == Os::MacOS)
                return std::unexpected(Error {
                    Error::Kind::Config,
                    "static_link()/static_deps(): macOS ld64 has no -Bstatic; link the .a by path "
                    "instead, e.g. linker_flags(\"/opt/homebrew/lib/libz.a\")",
                });
            /* static archives first: their own -l needs resolve against the
             * dynamic libraries to their right (GNU ld is single-pass) */
            add(lf.linker, "-Wl,-Bstatic");
            for (const auto& l : static_libs) {
                auto name = link_name(l, "static link library");
                if (!name)
                    return std::unexpected(name.error());
                add(lf.linker, "-l" + *name);
            }
            add(lf.linker, "-Wl,-Bdynamic");
        }
        for (const auto& l : libs) {
            auto name = link_name(l, "link library");
            if (!name)
                return std::unexpected(name.error());
            add(lf.linker, "-l" + *name);
        }
        for (const auto& f : frameworks) {
            auto name = link_name(f, "framework");
            if (!name)
                return std::unexpected(name.error());
            add(lf.linker, "-framework " + *name);
        }
        add(lf.linker, runtime->linker);
    }
    add(lf.linker, user_linker_flags);
    return lf;
}

} // namespace truss::detail
