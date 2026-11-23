// Target-level options: artifact naming, default build membership, default
// run target, runtime-loaded plugins (not_linkable), visibility.
module;
#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <string_view>

#include <unistd.h>

module truss;
import :api;
import :impl;

using namespace truss;

namespace {

bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

struct ScratchDir {
    std::filesystem::path prev;
    explicit ScratchDir(const std::string& name) {
        prev = std::filesystem::current_path();
        const auto dir = std::filesystem::temp_directory_path() / ("truss-targetopts-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~ScratchDir() { chdir(prev.c_str()); }
};

std::string manifest_text() {
    auto m = detail::read_file("build/debug/ninja/build.ninja");
    REQUIRE(m.has_value());
    return std::move(*m);
}

} // namespace

TEST_CASE("targetopts: output_name() renames the artifact, not the target") {
    ScratchDir dir {"output-name"};
    REQUIRE(detail::write_file("src/lib.cpp", "int f() { return 1; }\n").has_value());
    REQUIRE(detail::write_file("src/app.cpp", "int f();\nint main() { return f(); }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("output-name", 2, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));
    auto& lib = proj.add_static_library("core").sources("src/lib.cpp").output_name("netcore");
    proj.add_executable("app").sources("src/app.cpp").depends_on(lib);
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const std::string m = manifest_text();
    CHECK(contains(m, "libnetcore.a"));
    CHECK_FALSE(contains(m, "libcore.a"));
    /* the target is still addressed by its own name in --target */
    char tgt[] = "--target";
    char name[] = "core";
    char* argv2[] = {prog, dry, tgt, name};
    Project proj2("output-name", 4, argv2);
    proj2.configure(Config().toolchain(Toolchain::LLVM));
    auto& lib2 = proj2.add_static_library("core").sources("src/lib.cpp").output_name("netcore");
    (void)lib2;
    auto r2 = proj2.build();
    INFO((r2 ? "ok" : r2.error().message));
    REQUIRE(r2.has_value());
}

TEST_CASE("targetopts: build_by_default(false) keeps a target out of `all`") {
    ScratchDir dir {"opt-in"};
    REQUIRE(detail::write_file("src/lib.cpp", "int f() { return 1; }\n").has_value());
    REQUIRE(detail::write_file("src/app.cpp", "int main() { return 0; }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("opt-in", 2, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));
    proj.add_executable("app").sources("src/app.cpp");
    proj.add_shared_library("extra").sources("src/lib.cpp").build_by_default(false);
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const std::string m = manifest_text();
    const auto all_at = m.find("build all: phony");
    REQUIRE(all_at != std::string::npos);
    const auto all_end = m.find('\n', all_at);
    const std::string all_line = m.substr(all_at, all_end - all_at);
    CHECK(contains(all_line, "app"));
    CHECK_FALSE(contains(all_line, "libextra.so"));
    /* ... but it still has its own build edge and can be selected */
    CHECK(contains(m, "libextra.so"));
}

TEST_CASE("targetopts: as_shared() promotes a library to a shared object") {
    ScratchDir dir {"as-shared"};
    REQUIRE(detail::write_file("src/lib.cpp", "int f() { return 1; }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("as-shared", 2, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));
    proj.add_static_library("core").sources("src/lib.cpp").as_shared();
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());
    const std::string m = manifest_text();
    CHECK(contains(m, "libcore.so"));
    CHECK_FALSE(contains(m, "libcore.a"));
    CHECK(contains(m, "link_shared"));
}

TEST_CASE("targetopts: not_linkable() rejects depends_on() and non-shared targets") {
    ScratchDir dir {"not-linkable"};
    REQUIRE(detail::write_file("src/plug.cpp", "int p() { return 1; }\n").has_value());
    REQUIRE(detail::write_file("src/app.cpp", "int main() { return 0; }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";

    {
        char* argv[] = {prog, dry};
        Project proj("plug-ok", 2, argv);
        proj.configure(Config().toolchain(Toolchain::LLVM));
        auto& plug = proj.add_shared_library("plugin").sources("src/plug.cpp").not_linkable();
        proj.add_executable("app").sources("src/app.cpp").depends_on(plug);
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
        CHECK(contains(r.error().message, "not_linkable"));
        CHECK(contains(r.error().message, "dlopen"));
    }
    {
        char* argv[] = {prog, dry};
        Project proj("plug-static", 2, argv);
        proj.configure(Config().toolchain(Toolchain::LLVM));
        proj.add_static_library("plugin").sources("src/plug.cpp").not_linkable();
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(contains(r.error().message, "add_shared_library"));
    }
}

TEST_CASE("targetopts: default_target() validation (executable, unique per subgraph)") {
    ScratchDir dir {"default-target"};
    REQUIRE(detail::write_file("src/lib.cpp", "int f() { return 1; }\n").has_value());
    REQUIRE(detail::write_file("src/a.cpp", "int main() { return 0; }\n").has_value());
    REQUIRE(detail::write_file("src/b.cpp", "int main() { return 0; }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    {
        char* argv[] = {prog, dry};
        Project proj("def-notexe", 2, argv);
        proj.configure(Config().toolchain(Toolchain::LLVM));
        proj.add_static_library("lib").sources("src/lib.cpp").default_target();
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(contains(r.error().message, "default_target"));
    }
    {
        char* argv[] = {prog, dry};
        Project proj("def-dup", 2, argv);
        proj.configure(Config().toolchain(Toolchain::LLVM));
        proj.add_executable("a").sources("src/a.cpp").default_target();
        proj.add_executable("b").sources("src/b.cpp").default_target();
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(contains(r.error().message, "two executables claim default_target()"));
    }
    {
        char* argv[] = {prog, dry};
        Project proj("def-ok", 2, argv);
        proj.configure(Config().toolchain(Toolchain::LLVM));
        proj.add_executable("a").sources("src/a.cpp");
        proj.add_executable("b").sources("src/b.cpp").default_target();
        auto r = proj.build();
        INFO((r ? "ok" : r.error().message));
        REQUIRE(r.has_value());
    }
}

TEST_CASE("targetopts: output_name() collisions are a Config error") {
    ScratchDir dir {"name-collision"};
    REQUIRE(detail::write_file("src/a.cpp", "int a() { return 1; }\n").has_value());
    REQUIRE(detail::write_file("src/b.cpp", "int b() { return 2; }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("collision", 2, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));
    proj.add_static_library("one").sources("src/a.cpp").output_name("same");
    proj.add_static_library("two").sources("src/b.cpp").output_name("same");
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(contains(r.error().message, "both write"));
}

TEST_CASE("scan cache: gc keeps the newest entries under the cap") {
    const auto dir = std::filesystem::temp_directory_path() / "truss-scan-gc";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    for (int i = 0; i < 6; ++i) {
        const auto p = dir / (std::string("k") + std::to_string(i) + ".scan");
        REQUIRE(detail::write_file(p, "x").has_value());
        /* distinct, increasing mtimes (newest = k5) */
        std::filesystem::last_write_time(
            p, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(i));
    }
    REQUIRE(detail::scan_cache_gc(dir, 4).has_value());
    std::size_t count = 0;
    bool newest_survived = false;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        ++count;
        if (e.path().filename() == "k5.scan")
            newest_survived = true;
    }
    CHECK(count == 3); /* three quarters of the cap stay */
    CHECK(newest_survived);
    std::filesystem::remove_all(dir);
}
