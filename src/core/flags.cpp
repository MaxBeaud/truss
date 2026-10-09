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

/* Tables indexed by the public enums (Default = 0 means "no flag"). */
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
    if (t.family == CompilerFamily::Msvc)
        return std::unexpected(Error {
            Error::Kind::Toolchain,
            "MSVC targets are not supported yet (GCC and Clang are implemented; "
            "MSVC mapping is planned — see PLAN.MD phase 7)",
        });
    if (t.family != CompilerFamily::Gcc && t.family != CompilerFamily::Clang)
        return std::unexpected(Error {Error::Kind::Toolchain, "unknown compiler family for target '" + t.compiler + "'"});
    return {};
}

} // namespace

std::string triple_dir(const TargetTriple& t) {
    return std::string(arch_name(t.arch)) + "-" + std::string(os_name(t.os)) + "-" + std::string(family_name(t.family));
}

Result<FlagSet> build_flags(const TargetTriple& triple, BuildMode mode,
                            CppStandard std_opt, Warnings warnings, DebugInfo debug,
                            Optimization optimization, Sanitizer sanitizer, Lto lto,
                            bool pic, std::span<const std::string> defines,
                            std::span<const std::string> includes,
                            std::string_view user_flags) {
    if (auto r = require_supported(triple); !r)
        return std::unexpected(r.error());

    FlagSet fs;
    add(fs.compile, kStdFlags[static_cast<std::size_t>(std_opt == CppStandard::Default ? CppStandard::Cpp23 : std_opt)]);
    add(fs.compile, kWarningFlags[static_cast<std::size_t>(warnings)]);

    const DebugInfo dbg = debug == DebugInfo::Default
                              ? (mode == BuildMode::Debug ? DebugInfo::Full : DebugInfo::None)
                              : debug;
    add(fs.compile, kDebugFlags[static_cast<std::size_t>(dbg)]);

    const Optimization opt = optimization == Optimization::Default
                                 ? (mode == BuildMode::Debug ? Optimization::None : Optimization::O2)
                                 : optimization;
    add(fs.compile, kOptimizationFlags[static_cast<std::size_t>(opt)]);

    add(fs.compile, kSanitizerCompileFlags[static_cast<std::size_t>(sanitizer)]);
    add(fs.compile, kLtoCompileFlags[static_cast<std::size_t>(lto)]);
    if (pic) add(fs.compile, "-fPIC");
    if (mode == BuildMode::Release) add(fs.compile, "-DNDEBUG");

    for (const auto& d : defines) add(fs.compile, "-D" + d);
    for (const auto& i : includes) add(fs.compile, "-I\"" + i + "\"");

    add(fs.compile, user_flags);
    return fs;
}

Result<std::string> link_flags(const TargetTriple& triple, Sanitizer sanitizer, Lto lto,
                               std::span<const std::string> link_paths,
                               std::span<const std::string> libs,
                               std::span<const std::string> frameworks,
                               std::span<const std::string> framework_paths,
                               std::string_view user_linker_flags) {
    if (auto r = require_supported(triple); !r)
        return std::unexpected(r.error());

    std::string out;
    add(out, kSanitizerLinkFlags[static_cast<std::size_t>(sanitizer)]);
    add(out, kLtoLinkFlags[static_cast<std::size_t>(lto)]);
    for (const auto& p : link_paths) add(out, "-L\"" + p + "\"");
    for (const auto& p : framework_paths) add(out, "-F\"" + p + "\"");
    for (const auto& l : libs) add(out, "-l" + l);
    for (const auto& f : frameworks) add(out, "-framework " + f);
    add(out, user_linker_flags);
    return out;
}

} // namespace truss::detail
