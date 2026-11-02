// shared — example: shared library + rpath linking + user-defined task.
#include <cstdio>

import truss;

int main(int argc, char** argv) {
    truss::Project proj("shared", argc, argv);

    auto& greet = proj.add_shared_library("greet")
                      .include_dirs("include")
                      .sources_in("src");

    proj.add_executable("app")
        .sources_in("app")
        .include_dirs("include")
        .depends_on(greet);

    proj.add_executable("test_greet")
        .sources_in("tests")
        .include_dirs("include")
        .depends_on(greet)
        .is_test();

    // User-defined task: `./truss task hello`
    proj.task("hello", "print the task context", [](const truss::TaskContext& ctx) -> truss::Result<void> {
        std::printf("[TASK] project=%s mode=%s build_dir=%s\n", ctx.project.c_str(),
                    ctx.mode == truss::BuildMode::Debug ? "debug" : "release",
                    ctx.build_dir.string().c_str());
        return {};
    });

    auto result = proj.build();
    return result ? 0 : 1;
}
