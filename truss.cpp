// truss's own build script — truss building truss (PLAN.MD phase 8).
//
// Build this script once (xmake `truss-cli` target, or bootstrap.sh artifacts),
// then `./truss debug` produces build/debug/bin/<triple>/truss — truss compiled
// by itself. Unit tests (doctest) run through ./setup.sh unit and are not part
// of the self-host graph.
import truss;

#ifdef TRUSS_INSTALLED_BIN
#include <cstdio>
#include <filesystem>
#include <span>
#include <string_view>

#include "cli/init.hpp"
#endif

int main(int argc, char** argv) {
#ifdef TRUSS_INSTALLED_BIN
    /* installed binary: CLI verbs first; outside a truss checkout the
     * self-build below has nothing to build */
    const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
    if (args.size() >= 2 && std::string_view(args[1]) == "init")
        return truss::cli::init(args.subspan(2));
    const bool want_help = args.size() >= 2
        && (std::string_view(args[1]) == "-h" || std::string_view(args[1]) == "--help"
            || std::string_view(args[1]) == "help");
    if (std::error_code ec; !std::filesystem::exists("src/api.cppm", ec)) {
        std::fputs("usage: truss init <project-name>   scaffold a new truss project\n"
                   "       truss [debug|release] ...   (inside a truss checkout) rebuild truss\n",
                   want_help ? stdout : stderr);
        return args.size() >= 2 && !want_help ? 2 : 0;
    }
#endif
    truss::Project proj{"truss", argc, argv};

    /* vendored samurai (C17): samu.c excluded, its main() is replaced by
     * src/samu/shim.c; warnings off since upstream code is unmodified */
    auto& samu = proj.add_static_library("samu")
                     .warnings(truss::Warnings::None)
                     .defines({"_DEFAULT_SOURCE", "_POSIX_C_SOURCE=200809L"})
                     .sources_in("vendor/samurai")
                     .remove_source("vendor/samurai/samu.c");

    /* truss itself: module interface units (.cppm in src/), implementation
     * units (.cpp in src/core/) and the samurai shim (.c in src/samu/) */
    auto& lib = proj.add_static_library("truss-lib")
                    .sources_in("src")
                    .include_dirs({"vendor/samurai", "vendor"})
                    .defines({"_DEFAULT_SOURCE", "_POSIX_C_SOURCE=200809L"})
                    .cpp_standard(truss::CppStandard::Cpp26)
                    .depends_on(samu);

    /* the CLI: this very file, compiled against the truss module built above */
    proj.add_executable("truss")
        .sources("truss.cpp")
        .cpp_standard(truss::CppStandard::Cpp26)
        .depends_on(lib);

    /* the shippable binary: `truss init` compiled in, every dependency and
     * the C++ runtime (libstdc++/libgcc) linked statically -> needs only
     * glibc at runtime. Build just this one: ./truss release --target truss-dist */
    proj.add_executable("truss-dist")
        .sources("truss.cpp")
        .cpp_standard(truss::CppStandard::Cpp26)
        .defines("TRUSS_INSTALLED_BIN")
        .static_deps()
        .static_stdlib(truss::StaticStdlib::Gcc)
        .depends_on(lib);
    
    /* compdb failures must not silently exit 0 */
    if (auto cc = proj.compile_commands(); !cc) {
        truss::log::error(cc.error());
        return 1;
    }

    auto result = proj.build();
    return result ? 0 : 1;
}
