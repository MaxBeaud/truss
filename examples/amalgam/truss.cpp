// amalgam — example: truss as a single-header drop-in (PLAN.MD phase 9).
//
// No modules, no vendored tree, no link step against libtruss: just
//   clang++ -std=c++26 -I<repo>/dist truss.cpp -o truss
// The whole library (truss + samurai + shim) lives in truss_all.hpp.
#include "truss_all.hpp"

#include <cstdio>

int main(int argc, char** argv) {
    truss::Project proj("amalgam", argc, argv);

    auto& core = proj.add_static_library("core")
                     .sources_in("src");

    proj.add_executable("app")
        .sources_in("app")
        .depends_on(core);

    proj.add_executable("test_core")
        .sources_in("tests")
        .depends_on(core)
        .is_test();

    auto result = proj.build();
    if (!result)
        std::fprintf(stderr, "truss: %s\n", result.error().message.c_str());
    return result ? 0 : 1;
}
