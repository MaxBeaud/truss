/* Per-triple subgraphs (proj.targets()): one graph per named configuration in
 * a single manifest, per-subgraph runners, --for/--target selection, and the
 * validation errors. */
module;
#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

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
        const auto dir = std::filesystem::temp_directory_path() / ("truss-subgraph-test-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~ScratchDir() { chdir(prev.c_str()); }
};

bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

void write_project() {
    REQUIRE(detail::write_file("lib/core.cpp", "int core() { return 7; }\n").has_value());
    REQUIRE(detail::write_file("app/main.cpp", "int core();\nint main() { return core() == 7 ? 0 : 1; }\n").has_value());
    REQUIRE(detail::write_file("tests/core_test.cpp", "int core();\nint main() { return core() == 7 ? 0 : 1; }\n").has_value());
}

} // namespace

TEST_CASE("subgraphs: configure() + targets() is a Config error") {
    ScratchDir scratch {"both"};
    write_project();
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("sub-both", 1, argv);
    proj.configure(Config{}.toolchain(Toolchain::LLVM));
    proj.targets({{"linux", Config{}.toolchain(Toolchain::LLVM)}});
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(contains(r.error().message, "mutually exclusive"));
}

TEST_CASE("subgraphs: invalid, duplicate names and colliding triples are Config errors") {
    ScratchDir scratch {"names"};
    write_project();
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    {
        Project proj("sub-dup", 1, argv);
        proj.targets({{"linux", Config{}}, {"linux", Config{}}});
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(contains(r.error().message, "declared twice"));
    }
    {
        Project proj("sub-bad", 1, argv);
        proj.targets({{"all", Config{}}});
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(contains(r.error().message, "is invalid"));
    }
    {
        Project proj("sub-collide", 1, argv);
        proj.targets({{"a", Config{}.toolchain(Toolchain::LLVM)},
                      {"b", Config{}.toolchain(Toolchain::LLVM)}});
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(contains(r.error().message, "distinct triples"));
    }
}

TEST_CASE("subgraphs: one manifest, per-triple dirs, per-subgraph phonies") {
    if (!os::program_exists("zig")) {
        MESSAGE("zig not installed, skipping the cross subgraph");
        return;
    }
    ScratchDir scratch {"e2e"};
    write_project();
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("sub-e2e", 1, argv);
    proj.targets({
        {"linux", Config{}.toolchain(Toolchain::LLVM)},
        {"musl", Config{}.toolchain(Toolchain::Zig).target("aarch64-linux-musl")},
    });
    auto& lib = proj.add_static_library("core").sources("lib/core.cpp");
    proj.add_executable("app").sources("app/main.cpp").depends_on(lib);
    proj.add_executable("test_core").sources("tests/core_test.cpp").depends_on(lib).is_test();
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const std::string m = *detail::read_file("build/debug/ninja/build.ninja");
    /* both subgraphs emitted into one manifest, each in its own triple dir */
    CHECK(contains(m, "obj/x64-linux-clang/"));
    CHECK(contains(m, "obj/arm64-linux-musl-zig/"));
    CHECK(contains(m, "bin/x64-linux-clang/app"));
    CHECK(contains(m, "bin/arm64-linux-musl-zig/app"));
    /* per-subgraph phonies + the union (':' is escaped '$:' in ninja output) */
    CHECK(contains(m, "linux$:all"));
    CHECK(contains(m, "musl$:all"));
    CHECK(contains(m, "build all: phony"));
}

TEST_CASE("subgraphs: --for and compound --target select subgraphs") {
    if (!os::program_exists("zig")) {
        MESSAGE("zig not installed, skipping");
        return;
    }
    ScratchDir scratch {"select"};
    write_project();
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("sub-select", 2, argv);
    proj.targets({
        {"linux", Config{}.toolchain(Toolchain::LLVM)},
        {"musl", Config{}.toolchain(Toolchain::Zig).target("aarch64-linux-musl")},
    });
    auto& lib = proj.add_static_library("core").sources("lib/core.cpp");
    proj.add_executable("app").sources("app/main.cpp").depends_on(lib);
    proj.add_executable("test_core").sources("tests/core_test.cpp").depends_on(lib).is_test();

    { /* compound --target: one subgraph's app only */
        char t[] = "--target";
        char sel[] = "musl:app";
        char* argv2[] = {prog, dry, t, sel};
        Project p("sub-select", 4, argv2);
        p.targets({
            {"linux", Config{}.toolchain(Toolchain::LLVM)},
            {"musl", Config{}.toolchain(Toolchain::Zig).target("aarch64-linux-musl")},
        });
        p.add_static_library("core").sources("lib/core.cpp");
        p.add_executable("app").sources("app/main.cpp");
        CHECK(p.build().has_value());
        const std::string m = *detail::read_file("build/debug/ninja/build.ninja");
        /* the default statement names only the selected subgraph's exe
         * (paths in the manifest are absolute) */
        const auto dpos = m.find("default ");
        REQUIRE(dpos != std::string::npos);
        const std::string default_line = m.substr(dpos, m.find('\n', dpos) - dpos);
        CHECK(contains(default_line, "arm64-linux-musl-zig/app"));
        CHECK_FALSE(contains(default_line, "x64-linux-clang"));
    }
    { /* unknown --for */
        char f[] = "--for";
        char sel[] = "nope";
        char* argv3[] = {prog, dry, f, sel};
        Project p("sub-select", 4, argv3);
        p.targets({
            {"linux", Config{}.toolchain(Toolchain::LLVM)},
            {"musl", Config{}.toolchain(Toolchain::Zig).target("aarch64-linux-musl")},
        });
        p.add_executable("app").sources("app/main.cpp");
        auto r = p.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(contains(r.error().message, "unknown --for"));
    }
}

TEST_CASE("subgraphs: --for narrows a plain build to one subgraph") {
    if (!os::program_exists("zig")) {
        MESSAGE("zig not installed, skipping");
        return;
    }
    ScratchDir scratch {"for-build"};
    write_project();
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char f[] = "--for";
    char sel[] = "linux";
    char* argv[] = {prog, dry, f, sel};
    Project proj("sub-for-build", 4, argv);
    proj.targets({
        {"linux", Config{}.toolchain(Toolchain::LLVM)},
        {"musl", Config{}.toolchain(Toolchain::Zig).target("aarch64-linux-musl")},
    });
    proj.add_static_library("core").sources("lib/core.cpp");
    proj.add_executable("app").sources("app/main.cpp");
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());
    const std::string m = *detail::read_file("build/debug/ninja/build.ninja");
    /* ninja escapes ':' in phony names */
    const auto at = m.find("default ");
    REQUIRE(at != std::string::npos);
    const std::string line = m.substr(at, m.find('\n', at) - at);
    CHECK(contains(line, "linux$:all"));
    CHECK_FALSE(contains(line, "all all"));
}
