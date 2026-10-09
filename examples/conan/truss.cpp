// conan — example: conan 2 integration.
//
// proj.conan() runs `conan install`, parses the graph JSON and returns an
// Interface target carrying zlib's includes/libs; depends_on() wires it up.
import truss;

#include <cstdio>

int main(int argc, char** argv) {
    truss::Project proj("conan", argc, argv);

    auto z = proj.conan("conanfile.txt");
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
