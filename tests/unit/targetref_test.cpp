/* TargetRef ergonomics: depends_on()/merge() accept handles by value —
 * `depends_on(proj.conan({...}))`, `depends_on(proj.add_subdirectory(dir))`,
 * `merge(proj.add_subdirectory(dir))` — with no `*` at the call site. */
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

struct ScratchDir {
    std::filesystem::path prev;
    explicit ScratchDir(const std::string& name) {
        prev = std::filesystem::current_path();
        const auto dir = std::filesystem::temp_directory_path() / ("truss-noderef-test-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~ScratchDir() { chdir(prev.c_str()); }
};

bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

} // namespace

TEST_CASE("depends_on: add_subdirectory() handle wires the same graph with or without deref") {
    ScratchDir scratch {"subdir"};
    REQUIRE(detail::write_file("dep/src/dep.cpp", "int dep() { return 1; }\n").has_value());
    REQUIRE(detail::write_file("dep2/src/dep2.cpp", "int dep2() { return 2; }\n").has_value());
    REQUIRE(detail::write_file("app/main.cpp", "int main() { return 0; }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("noderef-subdir", 2, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));

    auto r = proj.add_subdirectory("dep");
    REQUIRE(r.has_value());

    auto& a = proj.add_executable("a").sources("app/main.cpp");
    auto& b = proj.add_executable("b").sources("app/main.cpp");
    a.depends_on(r);  /* Result<TargetRef> by value — no deref */
    b.depends_on(*r); /* legacy deref form still compiles */
    REQUIRE(a.deps().size() == 1);
    CHECK(a.deps()[0] == b.deps()[0]); /* identical graph edge */

    TargetRef handle = *r;
    auto& c = proj.add_executable("c").sources("app/main.cpp");
    c.depends_on(handle); /* TargetRef by value — no deref */
    CHECK(c.deps()[0] == b.deps()[0]);

    /* the literal inline call style: depends_on(proj.add_subdirectory(...)) */
    auto& d = proj.add_executable("d").sources("app/main.cpp");
    d.depends_on(proj.add_subdirectory("dep2"));
    REQUIRE(d.deps().size() == 1);
    CHECK(d.deps()[0]->name() == "dep2");

    auto res = proj.build(); /* dry run: the graph emits cleanly */
    INFO((res ? "ok" : res.error().message));
    CHECK(res.has_value());
}

TEST_CASE("depends_on: conan() result without deref wires the same graph") {
    if (!os::program_exists("conan")) {
        MESSAGE("conan not installed, skipping");
        return;
    }
    ScratchDir scratch {"conan"};
    REQUIRE(detail::write_file("app/main.cpp", "int main() { return 0; }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("noderef-conan", 2, argv);

    auto z = proj.conan({"zlib/1.3.1"});
    REQUIRE(z.has_value());

    auto& a = proj.add_executable("a").sources("app/main.cpp");
    auto& b = proj.add_executable("b").sources("app/main.cpp");
    a.depends_on(z);  /* no deref: depends_on(proj.conan(...)) shape */
    b.depends_on(*z); /* legacy deref form */
    REQUIRE(a.deps().size() == 1);
    CHECK(a.deps()[0] == b.deps()[0]);
    CHECK(a.deps()[0]->name() == "conan");

    auto res = proj.build(); /* dry run */
    INFO((res ? "ok" : res.error().message));
    CHECK(res.has_value());
}

TEST_CASE("merge: add_subdirectory() handle merges without deref, same as the deref form") {
    ScratchDir scratch {"merge"};
    REQUIRE(detail::write_file("a/a.cpp", "int a() { return 1; }\n").has_value());
    REQUIRE(detail::write_file("b/b.cpp", "int b() { return 2; }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("noderef-merge", 2, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));

    auto& combo = proj.add_static_library("combo");
    combo.merge(proj.add_subdirectory("a")); /* Result<TargetRef> — no deref */
    REQUIRE(combo.merged_libraries().size() == 1);
    CHECK(combo.merged_libraries()[0]->name() == "a");

    auto rb = proj.add_subdirectory("b");
    REQUIRE(rb.has_value());
    auto& legacy = proj.add_static_library("legacy");
    legacy.merge(*rb); /* legacy deref form still compiles */
    REQUIRE(legacy.merged_libraries().size() == 1);
    CHECK(legacy.merged_libraries()[0]->name() == "b");

    auto res = proj.build(); /* dry run: the ar -M MRI edges emit cleanly */
    INFO((res ? "ok" : res.error().message));
    CHECK(res.has_value());
}

TEST_CASE("depends_on/merge with a failed handle Result defer the error to build()") {
    ScratchDir scratch {"err"};
    REQUIRE(detail::write_file("app/main.cpp", "int main() {}\n").has_value());
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("noderef-err", 1, argv);

    auto& app = proj.add_executable("app").sources("app/main.cpp");
    app.depends_on(proj.add_subdirectory("no/such/dir")); /* no deref, failing Result */
    CHECK(app.deferred_error().has_value());

    auto& combo = proj.add_static_library("combo");
    combo.merge(proj.add_subdirectory("no/such/dir2")); /* merge error path, no deref */
    CHECK(combo.deferred_error().has_value());

    auto& e = proj.add_executable("e").sources("app/main.cpp");
    e.depends_on(proj.vcpkg({})); /* vcpkg shape: no vcpkg.json -> Config error, no deref */
    CHECK(e.deferred_error().has_value());

    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(contains(r.error().message, "depends_on()/merge()"));
    CHECK(contains(r.error().message, "not a directory"));
}
