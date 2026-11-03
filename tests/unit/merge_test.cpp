/* merge(): combine several static-library archives into one (ar -M MRI) */
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
        const auto dir = std::filesystem::temp_directory_path() / ("truss-merge-test-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~ScratchDir() { chdir(prev.c_str()); }
};

bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

std::string read(const std::string& p) {
    auto m = detail::read_file(p);
    REQUIRE(m.has_value());
    return *m;
}

void write_three_libs() {
    REQUIRE(detail::write_file("a/a.cpp", "int a() { return 1; }\n").has_value());
    REQUIRE(detail::write_file("b/b.cpp", "int b() { return 2; }\n").has_value());
}

} // namespace

TEST_CASE("merge: dry-run emits an ar -M MRI edge combining the member archives") {
    ScratchDir scratch {"dry"};
    write_three_libs();
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("merge-dry", 2, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));
    auto& la = proj.add_static_library("a").sources("a/a.cpp");
    auto& lb = proj.add_static_library("b").sources("b/b.cpp");
    proj.add_static_library("combo").merge(la).merge(lb);
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const std::string tdir =
        detail::triple_dir(*detail::apply_config(Config().toolchain(Toolchain::LLVM), host_triple()));
    const std::string m = read("build/debug/ninja/build.ninja");
    CHECK(contains(m, "lib/" + tdir + "/libcombo.a: merge_archive"));
    CHECK(contains(m, "$ar -M < $mri"));           /* the rule */
    /* the MRI script combines both members */
    const std::string mri = read("build/debug/mri/" + tdir + "/combo.mri");
    CHECK(contains(mri, "CREATE "));
    CHECK(contains(mri, "ADDLIB "));
    CHECK(contains(mri, "lib/" + tdir + "/liba.a"));
    CHECK(contains(mri, "lib/" + tdir + "/libb.a"));
    CHECK(contains(mri, "SAVE"));
}

TEST_CASE("merge: real build — combined archive is consumable via depends_on") {
    ScratchDir scratch {"real"};
    write_three_libs();
    REQUIRE(detail::write_file("app/main.cpp",
                               "int a();\nint b();\nint main() { return a() + b() == 3 ? 0 : 1; }\n")
                .has_value());
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("merge-real", 1, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));
    auto& la = proj.add_static_library("a").sources("a/a.cpp");
    auto& lb = proj.add_static_library("b").sources("b/b.cpp");
    auto& combo = proj.add_static_library("combo").merge(la).merge(lb);
    proj.add_executable("app").sources("app/main.cpp").depends_on(combo);
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const std::string tdir =
        detail::triple_dir(*detail::apply_config(Config().toolchain(Toolchain::LLVM), host_triple()));
    const auto exe = std::filesystem::current_path() / "build/debug/bin" / tdir / "app";
    REQUIRE(std::filesystem::exists(exe));
    auto run = os::run({exe.string()});
    REQUIRE(run.has_value());
    CHECK(run->exit_code == 0);
}

TEST_CASE("merge: a non-static-library member is a Config error") {
    ScratchDir scratch {"neg"};
    write_three_libs();
    REQUIRE(detail::write_file("x/x.cpp", "int main() { return 0; }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("merge-neg", 2, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));
    auto& la = proj.add_static_library("a").sources("a/a.cpp");
    auto& x = proj.add_executable("x").sources("x/x.cpp");
    proj.add_static_library("combo").merge(la).merge(x); /* x is an executable */
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(contains(r.error().message, "must name a static library"));
}

TEST_CASE("merge: MSVC targets are a Config error") {
    ScratchDir scratch {"msvc"};
    write_three_libs();
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("merge-msvc", 2, argv);
    auto& la = proj.add_static_library("a").sources("a/a.cpp").cross_target([] {
        TargetTriple t; t.os = Os::Windows; t.arch = Arch::X64; t.family = CompilerFamily::Msvc;
        t.compiler = "clang-cl"; t.ar = "llvm-lib"; return t;
    }());
    proj.add_static_library("combo").merge(la).cross_target([] {
        TargetTriple t; t.os = Os::Windows; t.arch = Arch::X64; t.family = CompilerFamily::Msvc;
        t.compiler = "clang-cl"; t.ar = "llvm-lib"; return t;
    }());
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(contains(r.error().message, "MSVC"));
}
