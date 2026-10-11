// hello — example build script for the hello project.
//
// Compile (from the repo root):  ./setup.sh examples
// Run (from examples/hello):     ./truss debug | ./truss run | ./truss test
import truss;

#include <cstdint>
#include <filesystem>
#include <string>

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

    // Scripting-helper showcase: ./truss stats
    proj.task("stats", [](const truss::TaskContext&) -> truss::Result<void> {
        std::size_t files = 0;
        std::uint64_t bytes = 0;
        for (const auto& f : truss::fs::walk("src")) {
            ++files;
            std::error_code ec;
            bytes += std::filesystem::file_size(f, ec);
        }
        truss::log::info("src: " + std::to_string(files) + " files, " +
                         std::to_string(bytes) + " bytes");

        if (truss::os::program_exists("git")) {
            auto head = truss::os::capture({"git", "rev-parse", "--short", "HEAD"});
            if (head && head->exit_code == 0) {
                auto& out = head->output;
                out.erase(out.find_last_not_of('\n') + 1);
                truss::log::success("git HEAD is " + out);
            }
        }
        return {};
    });

    auto result = proj.build();
    return result ? 0 : 1;
}
