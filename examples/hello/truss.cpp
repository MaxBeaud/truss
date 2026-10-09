// hello — example build script for the hello project.
//
// Compile (from the repo root):  xmake -y hello-truss
// Run (from examples/hello):     ./truss debug | ./truss run | ./truss test
import truss;

int main(int argc, char** argv) {
    truss::Project proj("hello", argc, argv);

    // Chaining style.
    auto& core = proj.add_static_library("core")
                     .include_dirs("include")
                     .sources_in("src/core");

    // Pipe-composition style.
    auto& app = proj.add_executable("app")
        | truss::with_sources_in("src/app")
        | truss::with_include_dirs({"include"})
        | truss::with_depends_on(core);
    (void)app;

    proj.add_executable("test_core")
        .sources_in("tests")
        .include_dirs("include")
        .depends_on(core)
        .is_test();

    auto result = proj.build();
    return result ? 0 : 1;
}
