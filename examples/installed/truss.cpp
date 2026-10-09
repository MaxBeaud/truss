// installed — example: truss as an installed tool (PLAN.MD phase 8).
//
// This fixture has NO vendored truss source. The build script below is
// compiled against the staged artifacts from `./bootstrap.sh --install`:
//
//   clang++ -std=c++26 -stdlib=libc++ \
//       -fmodule-file=truss=<prefix>/lib/truss/truss.pcm \
//       -fmodule-file=truss:api=<prefix>/lib/truss/truss_api.pcm \
//       truss.cpp <prefix>/lib/truss/libtruss.a -o truss
//
// (libtruss.a + the BMIs are always built on your machine — no prebuilts.)
import truss;

#include <cstdio>

int main(int argc, char** argv) {
    truss::Project proj("installed", argc, argv);

    auto& core = proj.add_static_library("core")
                     .sources_in("src");

    proj.add_executable("app")
        .sources_in("app")
        .depends_on(core);

    proj.add_executable("test_app")
        .sources_in("tests")
        .depends_on(core)
        .is_test();

    auto result = proj.build();
    if (!result)
        std::fprintf(stderr, "truss: %s\n", result.error().message.c_str());
    return result ? 0 : 1;
}
