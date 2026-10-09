// modules — example: C++20/26 modules in a user project.
//
// mathmod is a static library whose interface is a module (primary + partition)
// with a separate implementation unit; app/test_math consume it via `import math;`.
import truss;

int main(int argc, char** argv) {
    truss::Project proj("modules", argc, argv);

    auto& math = proj.add_static_library("mathmod")
                     .sources_in("src");

    proj.add_executable("app")
        .sources_in("app")
        .depends_on(math);

    proj.add_executable("test_math")
        .sources_in("tests")
        .depends_on(math)
        .is_test();

    auto result = proj.build();
    return result ? 0 : 1;
}
