// truss's own build script — truss building truss (PLAN.MD phase 8).
//
// Build this script once (xmake `truss-cli` target, or bootstrap.sh artifacts),
// then `./truss debug` produces build/debug/bin/<triple>/truss — truss compiled
// by itself. Unit tests need Catch2 (an xmake package for now), so they are
// not part of the self-host graph yet.
import truss;

int main(int argc, char** argv) {
    truss::Project proj("truss", argc, argv);

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

    auto result = proj.build();
    return result ? 0 : 1;
}
