module;
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <expected>
#include <string>
#include <string_view>

module truss;
import :api;
import :impl;

namespace truss {

TargetTriple host_triple() {
    TargetTriple t;

#if defined(__EMSCRIPTEN__)
    t.os = Os::Emscripten;
#elif defined(__ANDROID__)
    t.os = Os::Android;
#elif defined(__linux__) || defined(__gnu_linux__)
    t.os = Os::Linux;
#elif defined(_WIN32) || defined(__WIN32__)
    t.os = Os::Windows;
#elif defined(__APPLE__) || defined(__MACH__)
    t.os = Os::MacOS;
#elif defined(__FreeBSD__)
    t.os = Os::FreeBSD;
#else
    t.os = Os::Unknown;
#endif

#if defined(__x86_64__) || defined(_M_X64)
    t.arch = Arch::X64;
#elif defined(__aarch64__) || defined(_M_ARM64)
    t.arch = Arch::Arm64;
#elif defined(__i386__) || defined(_M_IX86)
    t.arch = Arch::X86;
#elif defined(__arm__) || defined(_M_ARM)
    t.arch = Arch::Arm32;
#elif defined(__riscv) && (__riscv_xlen == 64)
    t.arch = Arch::Riscv64;
#elif defined(__powerpc64__) || defined(__ppc64__)
    t.arch = Arch::Ppc64;
#elif defined(__s390x__)
    t.arch = Arch::S390x;
#elif defined(__wasm32__)
    t.arch = Arch::Wasm32;
#else
    t.arch = Arch::Unknown;
#endif

#if defined(__clang__)
    t.family = CompilerFamily::Clang;
#elif defined(__GNUC__)
    t.family = CompilerFamily::Gcc;
#elif defined(_MSC_VER)
    t.family = CompilerFamily::Msvc;
#else
    t.family = CompilerFamily::Unknown;
#endif

    if (const char* cxx = std::getenv("CXX"); cxx != nullptr && *cxx != '\0')
        t.compiler = cxx;
    else {
        switch (t.family) {
        case CompilerFamily::Clang: t.compiler = "clang++"; break;
        case CompilerFamily::Gcc: t.compiler = "g++"; break;
        case CompilerFamily::Msvc: t.compiler = "cl.exe"; break;
        default: t.compiler = "c++"; break;
        }
    }

    if (t.family == CompilerFamily::Clang && detail::program_exists("llvm-ar"))
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
                t.ar = detail::program_exists("llvm-ar") ? "llvm-ar" : "ar";
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

} // namespace detail

} // namespace truss
