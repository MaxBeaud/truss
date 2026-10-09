// hello — example build script for the hello project.
//
// Compile (from the repo root):  ./setup.sh examples
// Run (from examples/hello):     ./truss debug | ./truss run | ./truss test
import truss;

int main(int argc, char** argv) {
    truss::Project proj("hello", argc, argv);

    auto& core = proj.add_static_library("core")
                       .include_dirs("include")
                       .sources_in("src/core");

    proj.add_executable("app")
        .sources_in("src/app")
        .include_dirs("include")
        .depends_on(core);

    proj.add_executable("test_core")
        .sources_in("tests")
        .include_dirs("include")
        .depends_on(core)
        .is_test();

    auto result = proj.build();
    return result ? 0 : 1;
}
