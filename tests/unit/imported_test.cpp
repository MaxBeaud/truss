// 1.0 roadmap P1 items:
// - for_host(): pin targets to the build host inside a cross project, and the
//   host↔target depends_on ABI-mismatch Config error (cross-compilation phase 2)
// - import_library(): first-class imported prebuilt-library target (backlog #8)
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
        const auto dir = std::filesystem::temp_directory_path() / ("truss-imported-test-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~ScratchDir() { chdir(prev.c_str()); }
};

bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

std::string manifest_text() {
    auto m = detail::read_file("build/debug/ninja/build.ninja");
    REQUIRE(m.has_value());
    return *m;
}

} // namespace

/* --- for_host() -------------------------------------------------------------- */

TEST_CASE("for_host: pins tools to the host triple in a cross project") {
    ScratchDir scratch {"for-host"};
    REQUIRE(detail::write_file("gen/gen.cpp", "int gen() { return 1; }\n").has_value());
    REQUIRE(detail::write_file("app/main.cpp", "int main() { return 0; }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n"; /* manifest only: a cross-compiled app need not run */
    char* argv[] = {prog, dry};
    Project proj("forhost", 2, argv);
    proj.configure(Config().toolchain(Toolchain::Zig).target("aarch64-linux-musl"));
    proj.add_executable("gen").sources("gen/gen.cpp").for_host();
    proj.add_executable("app").sources("app/main.cpp");
    auto r = proj.build();
    INFO((r ? "build ok" : r.error().message));
    REQUIRE(r.has_value());

    const std::string m = manifest_text();
    /* the pinned host tools compile with the Config's toolchain (zig), while
     * the cross app lands in its own aarch64 triple dir */
    const auto zig_host = detail::apply_config(Config().toolchain(Toolchain::Zig), host_triple());
    REQUIRE(zig_host.has_value());
    CHECK(contains(m, "bin/" + detail::triple_dir(*zig_host) + "/gen"));
    CHECK(contains(m, "bin/arm64-linux-musl-zig/app"));
    CHECK(contains(m, "-target aarch64-linux-musl"));
}

TEST_CASE("for_host: depends_on across the host/target boundary is a Config error") {
    ScratchDir scratch {"for-host-mix"};
    REQUIRE(detail::write_file("l/lib.cpp", "int lib() { return 1; }\n").has_value());
    REQUIRE(detail::write_file("app/main.cpp", "int lib();\nint main() { return lib(); }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n"; /* the error is raised at resolve time: nothing compiles */
    char* argv[] = {prog, dry};
    Project proj("for-host-mix", 2, argv);
    proj.configure(Config().toolchain(Toolchain::Zig).target("aarch64-linux-musl"));

    SUBCASE("host tool depends on a cross library") {
        auto& lib = proj.add_static_library("crosslib").sources("l/lib.cpp");
        proj.add_executable("gen").sources("app/main.cpp").for_host().depends_on(lib);
    }
    SUBCASE("cross app depends on a host library") {
        auto& lib = proj.add_static_library("hostlib").sources("l/lib.cpp").for_host();
        proj.add_executable("app").sources("app/main.cpp").depends_on(lib);
    }
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(contains(r.error().message, "same machine"));
}

TEST_CASE("cross build: `test` builds but skips cross test exes with a warning") {
    if (!os::program_exists("zig"))
        return; /* the cross compile needs the zig toolchain */
    ScratchDir scratch {"cross-test-skip"};
    REQUIRE(detail::write_file("t/t.cpp", "int main() { return 0; }\n").has_value());
    char prog[] = "truss-tests";
    char test[] = "test";
    char* argv[] = {prog, test};
    Project proj("cross-test-skip", 2, argv);
    proj.configure(Config().toolchain(Toolchain::Zig).target("aarch64-linux-musl"));
    proj.add_executable("t").sources("t/t.cpp").is_test();
    /* must succeed: the cross exe is built, then skipped — never run, never "passed" */
    auto r = proj.build();
    INFO((r ? "build ok" : r.error().message));
    REQUIRE(r.has_value());
    CHECK(std::filesystem::exists(
        "build/debug/bin/arm64-linux-musl-zig/t")); /* built for the cross triple */
}

TEST_CASE("for_host: host-to-host edges stay fine in a cross project") {    ScratchDir scratch {"for-host-ok"};
    REQUIRE(detail::write_file("l/lib.cpp", "int lib() { return 1; }\n").has_value());
    REQUIRE(detail::write_file("app/main.cpp", "int lib();\nint main() { return lib(); }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("for-host-ok", 2, argv);
    proj.configure(Config().toolchain(Toolchain::Zig).target("aarch64-linux-musl"));
    auto& lib = proj.add_static_library("hostlib").sources("l/lib.cpp").for_host();
    proj.add_executable("gen").sources("app/main.cpp").for_host().depends_on(lib);
    auto r = proj.build();
    INFO((r ? "build ok" : r.error().message));
    REQUIRE(r.has_value());
}

/* --- import_library() --------------------------------------------------------- */

TEST_CASE("imported library: static prebuilt is linked and runs") {
    ScratchDir scratch {"imported-static"};
    char prog[] = "truss-tests";
    char* argv[] = {prog};

    /* project 1 produces the prebuilt artifact */
    REQUIRE(detail::write_file("pre/pre.cpp", "int pre() { return 41; }\n").has_value());
    Project build1("producer", 1, argv);
    build1.add_static_library("prelib").sources("pre/pre.cpp");
    auto b1 = build1.build();
    INFO((b1 ? "producer ok" : b1.error().message));
    REQUIRE(b1.has_value());
    const std::string archive =
        (std::filesystem::current_path() / "build/debug/lib" / detail::triple_dir(host_triple()) / "libprelib.a")
            .generic_string();
    REQUIRE(std::filesystem::exists(archive));

    /* project 2 consumes it without building it */
    REQUIRE(detail::write_file("app/main.cpp", "int pre();\nint main() { return pre() == 41 ? 0 : 1; }\n")
                .has_value());
    char test[] = "test";
    char* test_argv[] = {prog, test}; /* `./truss test` builds + runs is_test() targets */
    Project proj("consumer", 2, test_argv);
    auto& useme = proj.import_library("prebuilt")
                      .import_archive(archive)
                      .system_include_dirs("pre/include");
    /* truss's own testing infra: is_test() + the built-in `test` task runs
     * the executable; build() returning success means the test passed */
    proj.add_executable("app").sources("app/main.cpp").depends_on(useme).is_test();
    auto r = proj.build();
    INFO((r ? "consumer ok" : r.error().message));
    REQUIRE(r.has_value());

    const std::string m = manifest_text();
    CHECK(contains(m, archive)); /* the prebuilt file is a link input */
}

TEST_CASE("imported library: shared prebuilt gets an rpath and runs") {
    ScratchDir scratch {"imported-shared"};
    char prog[] = "truss-tests";
    char* argv[] = {prog};

    REQUIRE(detail::write_file("pre/pre.cpp", "int pre() { return 42; }\n").has_value());
    Project build1("producer", 1, argv);
    build1.add_shared_library("prelib").sources("pre/pre.cpp");
    auto b1 = build1.build();
    INFO((b1 ? "producer ok" : b1.error().message));
    REQUIRE(b1.has_value());
    const std::string lib =
        (std::filesystem::current_path() / "build/debug/lib" / detail::triple_dir(host_triple()) / "libprelib.so")
            .generic_string();
    REQUIRE(std::filesystem::exists(lib));

    REQUIRE(detail::write_file("app/main.cpp", "int pre();\nint main() { return pre() == 42 ? 0 : 1; }\n")
                .has_value());
    char test[] = "test";
    char* test_argv[] = {prog, test};
    Project proj("consumer", 2, test_argv);
    auto& useme = proj.import_library("prebuilt").import_archive(lib);
    proj.add_executable("app").sources("app/main.cpp").depends_on(useme).is_test();
    auto r = proj.build();
    INFO((r ? "consumer ok" : r.error().message));
    REQUIRE(r.has_value());

    const std::string m = manifest_text();
    CHECK(contains(m, "-Wl,-rpath," + std::filesystem::path(lib).parent_path().generic_string()));
}

TEST_CASE("imported library: missing file is a Config error with the fix") {
    ScratchDir scratch {"imported-missing"};
    REQUIRE(detail::write_file("app/main.cpp", "int main() { return 0; }\n").has_value());
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("imported-missing", 1, argv);
    auto& useme = proj.import_library("ghost").import_archive("vendor/nope/libghost.a");
    proj.add_executable("app").sources("app/main.cpp").depends_on(useme);
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(contains(r.error().message, "not found"));
}

TEST_CASE("imported library: import_archive() outside import_library() is a Config error") {
    ScratchDir scratch {"imported-kind"};
    REQUIRE(detail::write_file("app/main.cpp", "int main() { return 0; }\n").has_value());
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("imported-kind", 1, argv);
    proj.add_executable("app").sources("app/main.cpp").import_archive("libx.a");
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(contains(r.error().message, "import_library"));
}
