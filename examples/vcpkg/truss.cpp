// vcpkg — example: vcpkg manifest-mode integration.
//
// proj.vcpkg({"zlib"}) generates vcpkg.json (none exists in this fixture),
// installs under build/{mode}/vcpkg_installed and returns an Interface target
// carrying the triplet's include/ + lib/ usage.
import truss;

#include <cstdio>

int main(int argc, char** argv) {
    truss::Project proj("vcpkgdemo", argc, argv);

    auto z = proj.vcpkg({"zlib"});
    if (!z) {
        std::fprintf(stderr, "truss: %s\n", z.error().message.c_str());
        return 1;
    }

    proj.add_executable("app")
        .sources("app/main.cpp")
        .depends_on(**z);

    proj.add_executable("test_zlib")
        .sources("tests/test_zlib.cpp")
        .depends_on(**z)
        .is_test();

    auto result = proj.build();
    return result ? 0 : 1;
}
