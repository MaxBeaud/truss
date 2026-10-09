module;
#include <cstdlib>

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
    else
        t.ar = "ar";

    return t;
}

} // namespace truss
