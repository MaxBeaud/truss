// Cross-compilation: Config::target + Config::runner (execute cross-compiled
// test/run exes through a command prefix). Per-triple subgraphs live in
// subgraph_test.cpp.
module;
#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

module truss;
import :api;
import :impl;

using namespace truss;

namespace {

struct ScratchDir {
    std::filesystem::path prev;
    explicit ScratchDir(const std::string& name) {
        prev = std::filesystem::current_path();
        const auto dir = std::filesystem::temp_directory_path() / ("truss-cross-test-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~ScratchDir() { chdir(prev.c_str()); }
};

bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

/* a trivial logging wrapper used as the runner: records its argv, then execs */
void write_runner_script() {
    REQUIRE(detail::write_file("runlog.sh",
                               "#!/bin/sh\n"
                               "printf '%s\\n' \"$@\" >> runner.log\n"
                               "exec \"$@\"\n")
                .has_value());
    std::error_code ec;
    std::filesystem::permissions("runlog.sh", std::filesystem::perms::owner_all, ec);
    REQUIRE_FALSE(ec);
}

} // namespace

TEST_CASE("target: a malformed triple in the script's Config is a Config error from build()") {
    ScratchDir scratch {"cross-bad"};
    REQUIRE(detail::write_file("app/main.cpp", "int main() { return 0; }\n").has_value());
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("cross-bad", 1, argv);
    proj.configure(Config().toolchain(Toolchain::Zig).target("linux-x64"));
    proj.add_executable("app").sources("app/main.cpp");
    auto r = proj.build(); /* fails in apply_config, before anything is compiled */
    INFO((r ? "unexpectedly ok" : r.error().message));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(contains(r.error().message, "unrecognized arch or OS"));
}

TEST_CASE("runner: empty and '-'-prefixed values are Config errors reported by build()") {
    char prog[] = "truss-tests";
    {
        ScratchDir scratch {"runner-empty"};
        REQUIRE(detail::write_file("app/main.cpp", "int main() { return 0; }\n").has_value());
        char* argv[] = {prog};
        Project proj("runner-empty", 1, argv);
        proj.configure(Config().toolchain(Toolchain::Zig).runner(""));
        proj.add_executable("app").sources("app/main.cpp");
        auto r = proj.build();
        INFO((r ? "unexpectedly ok" : r.error().message));
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
        CHECK(contains(r.error().message, "runner(\"\")"));
    }
    {
        ScratchDir scratch {"runner-dash"};
        REQUIRE(detail::write_file("app/main.cpp", "int main() { return 0; }\n").has_value());
        char* argv[] = {prog};
        Project proj("runner-dash", 1, argv);
        proj.configure(Config().toolchain(Toolchain::Zig).runner("-qemu-aarch64"));
        proj.add_executable("app").sources("app/main.cpp");
        auto r = proj.build();
        INFO((r ? "unexpectedly ok" : r.error().message));
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
        CHECK(contains(r.error().message, "-qemu-aarch64"));
        CHECK(contains(r.error().message, "must be a command, not a flag"));
    }
}

TEST_CASE("runner: `run` on a cross exe without a runner stays a Config error") {
    ScratchDir scratch {"run-no-runner"};
    REQUIRE(detail::write_file("app/main.cpp", "int main() { return 0; }\n").has_value());
    char prog[] = "truss-tests";
    char run[] = "run";
    char* argv[] = {prog, run};
    Project proj("run-no-runner", 2, argv);
    proj.configure(Config().toolchain(Toolchain::Zig).target("aarch64-linux-musl"));
    proj.add_executable("app").sources("app/main.cpp");
    auto r = proj.build(); /* the error fires before the exe is compiled */
    INFO((r ? "unexpectedly ok" : r.error().message));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(contains(r.error().message, "configure a runner"));
}

/* --- real zig end-to-end (x86_64-linux-musl exes run natively on an x64 host) --- */

TEST_CASE("zig e2e: a cross subgraph lands outputs in the per-triple dir") {
    if (!os::program_exists("zig")) { MESSAGE("zig not installed, skipping"); return; }
    ScratchDir scratch {"cross-flag-e2e"};
    REQUIRE(detail::write_file("app/main.cpp", "int main() { return 0; }\n").has_value());
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("cross-flag-e2e", 1, argv);
    proj.targets({{"musl", Config().toolchain(Toolchain::Zig).target("x86_64-linux-musl")}});
    proj.add_executable("app").sources("app/main.cpp");
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const auto bld = std::filesystem::current_path() / "build/debug";
    CHECK(std::filesystem::exists(bld / "bin/x64-linux-musl-zig/app"));
    CHECK_FALSE(std::filesystem::exists(bld / "bin/x64-linux-zig/app"));
}

TEST_CASE("zig e2e: a cross test exe executes through Config::runner (logging wrapper)") {
    if (!os::program_exists("zig")) { MESSAGE("zig not installed, skipping"); return; }
    ScratchDir scratch {"runner-test-e2e"};
    REQUIRE(detail::write_file("app/main.cpp", "int main() { return 0; }\n").has_value());
    write_runner_script();
    char prog[] = "truss-tests";
    char test[] = "test";
    char* argv[] = {prog, test};
    Project proj("runner-test-e2e", 2, argv);
    proj.configure(Config().toolchain(Toolchain::Zig).target("x86_64-linux-musl").runner("./runlog.sh"));
    proj.add_executable("t").sources("app/main.cpp").is_test();
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const auto exe = std::filesystem::current_path() / "build/debug/bin/x64-linux-musl-zig/t";
    REQUIRE(std::filesystem::exists(exe));
    /* the wrapper logged its argv: the cross exe went through the runner */
    auto log = detail::read_file("runner.log");
    REQUIRE(log.has_value());
    INFO(*log);
    CHECK(contains(*log, "x64-linux-musl-zig"));
    CHECK(contains(*log, exe.generic_string()));
}

TEST_CASE("zig e2e: without a runner a cross test is built but skipped (build still succeeds)") {
    if (!os::program_exists("zig")) { MESSAGE("zig not installed, skipping"); return; }
    ScratchDir scratch {"skip-no-runner"};
    REQUIRE(detail::write_file("app/main.cpp", "int main() { return 0; }\n").has_value());
    char prog[] = "truss-tests";
    char test[] = "test";
    char* argv[] = {prog, test};
    Project proj("skip-no-runner", 2, argv);
    proj.configure(Config().toolchain(Toolchain::Zig).target("x86_64-linux-musl"));
    proj.add_executable("t").sources("app/main.cpp").is_test();
    auto r = proj.build(); /* skipped with a warning, never silently "passed" */
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());
    CHECK(std::filesystem::exists(
        std::filesystem::current_path() / "build/debug/bin/x64-linux-musl-zig/t"));
    /* no runner was configured, so nothing executed */
    std::error_code ec;
    CHECK_FALSE(std::filesystem::exists("runner.log", ec));
}

TEST_CASE("zig e2e: `run` executes a cross exe through Config::runner") {
    if (!os::program_exists("zig")) { MESSAGE("zig not installed, skipping"); return; }
    ScratchDir scratch {"runner-run-e2e"};
    REQUIRE(detail::write_file("app/main.cpp", "int main() { return 0; }\n").has_value());
    write_runner_script();
    char prog[] = "truss-tests";
    char run[] = "run";
    char* argv[] = {prog, run};
    Project proj("runner-run-e2e", 2, argv);
    proj.configure(Config().toolchain(Toolchain::Zig).target("x86_64-linux-musl").runner("./runlog.sh"));
    proj.add_executable("app").sources("app/main.cpp");
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const auto exe = std::filesystem::current_path() / "build/debug/bin/x64-linux-musl-zig/app";
    REQUIRE(std::filesystem::exists(exe));
    auto log = detail::read_file("runner.log");
    REQUIRE(log.has_value());
    INFO(*log);
    CHECK(contains(*log, exe.generic_string()));
}
