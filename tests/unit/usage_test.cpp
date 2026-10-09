module;
#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include <unistd.h>

module truss;
import :api;
import :impl;

using namespace truss;

namespace {

/* RAII chdir: Project takes cwd() at construction, so scratch projects need it */
struct ScratchDir {
    std::filesystem::path prev;
    explicit ScratchDir(const std::string& name) {
        prev = std::filesystem::current_path();
        const auto dir = std::filesystem::temp_directory_path() / ("truss-usage-test-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir / "iface/include");
        std::filesystem::create_directories(dir / "mylib");
        std::filesystem::create_directories(dir / "app");
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~ScratchDir() { chdir(prev.c_str()); }
};

} // namespace

TEST_CASE("brace-list builder overloads collect every element") {
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("overloads", 1, argv);
    auto& t = proj.add_static_library("x")
                  .defines({"TRUSS_OVL_A", "TRUSS_OVL_B"})
                  .include_dirs({"i1", "i2"})
                  .link_libs({"l1", "l2"})
                  .link_paths({"p1", "p2"});
    CHECK(t.defines().size() == 2);
    CHECK(t.include_dirs().size() == 2);
    CHECK(t.link_libs().size() == 2);
    CHECK(t.link_paths().size() == 2);
}

TEST_CASE("link: transitive static-lib deps are linked in order") {
    ScratchDir scratch {"transitive"};
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("transitive", 1, argv);

    REQUIRE(detail::write_file("b/leaf.cpp", "int leaf() { return 7; }\n").has_value());
    auto& b = proj.add_static_library("b").sources("b/leaf.cpp");

    REQUIRE(detail::write_file("a/use.cpp", "int leaf();\nint use_leaf() { return leaf(); }\n").has_value());
    auto& a = proj.add_static_library("a").sources("a/use.cpp").depends_on(b);

    /* exe depends only on 'a'; 'b' must still reach the link line (to the
     * right of 'a') for ld to resolve a's reference */
    REQUIRE(detail::write_file("app/main.cpp",
                               "int use_leaf();\n"
                               "int main() { return use_leaf() == 7 ? 0 : 1; }\n")
                  .has_value());
    proj.add_executable("app").sources("app/main.cpp").depends_on(a);

    auto r = proj.build();
    INFO((r ? "build ok" : r.error().message));
    REQUIRE(r.has_value());
}

TEST_CASE("usage: Interface defines/includes propagate; user lib defines do not") {
    ScratchDir scratch {"propagate"};
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("usage-test", 1, argv);

    /* header visible through the Interface target's include dir */
    REQUIRE(detail::write_file("iface/include/iface.h", "#pragma once\n").has_value());
    /* Interface carries a public define + include dir */
    auto& iface = proj.add_interface_library("iface")
                      .include_dirs("iface/include")
                      .defines("TRUSS_USAGE_PUBLIC");

    /* a user static lib with a PRIVATE define that must NOT leak to consumers */
    REQUIRE(detail::write_file("mylib/lib.cpp", "int secret() { return 1; }\n").has_value());
    auto& mylib = proj.add_static_library("mylib")
                      .sources("mylib/lib.cpp")
                      .defines("TRUSS_USAGE_SECRET");

    /* consumer: requires the Interface define + header; must not see the lib define */
    REQUIRE(detail::write_file("app/main.cpp",
                       "#include <iface.h>\n"
                       "#ifdef TRUSS_USAGE_PUBLIC\n"
                       "#ifndef TRUSS_USAGE_SECRET\n"
                       "int main() { return 0; }\n"
                       "#else\n"
                       "#error \"user lib define leaked into consumer\"\n"
                       "#endif\n"
                       "#else\n"
                       "#error \"Interface define did not propagate\"\n"
                       "#endif\n").has_value());

    proj.add_executable("app").sources("app/main.cpp").depends_on(iface).depends_on(mylib);
    auto r = proj.build();
    INFO((r ? "build ok" : r.error().message));
    REQUIRE(r.has_value());
}

TEST_CASE("config: family detection from compiler names") {
    CHECK(detail::family_from_compiler_name("x86_64-w64-mingw32-g++") == CompilerFamily::Gcc);
    CHECK(detail::family_from_compiler_name("/usr/bin/clang++-21") == CompilerFamily::Clang);
    CHECK(detail::family_from_compiler_name("clang-cl") == CompilerFamily::Msvc);
    CHECK(detail::family_from_compiler_name("cl.exe") == CompilerFamily::Msvc);
    CHECK(detail::family_from_compiler_name("cc") == CompilerFamily::Unknown);
}

TEST_CASE("config: toolchain(LLVM) expands to clang++ + libc++ + llvm-ar") {
    Config conf;
    conf.toolchain(Toolchain::LLVM);
    TargetTriple base;
    base.os = Os::Linux; base.arch = Arch::X64;
    base.family = CompilerFamily::Gcc; base.compiler = "g++"; base.ar = "ar";
    auto r = detail::apply_config(conf, base);
    REQUIRE(r.has_value());
    CHECK(r->compiler == "clang++");
    CHECK(r->family == CompilerFamily::Clang);
    CHECK(r->stdlib == StdLib::Libcxx);
    CHECK(r->ar == "llvm-ar");
}

TEST_CASE("config: explicit calls after toolchain() win") {
    Config conf;
    conf.toolchain(Toolchain::LLVM).compiler("/opt/llvm/bin/clang++").ar("llvm-ar-21");
    TargetTriple base = host_triple();
    auto r = detail::apply_config(conf, base);
    REQUIRE(r.has_value());
    CHECK(r->compiler == "/opt/llvm/bin/clang++");
    CHECK(r->family == CompilerFamily::Clang);
    CHECK(r->ar == "llvm-ar-21");
}

TEST_CASE("config: g++ + libc++ is a Config error") {
    Config conf;
    conf.compiler("g++").stdlib(StdLib::Libcxx);
    auto r = detail::apply_config(conf, host_triple());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
}

TEST_CASE("config: MSVC + stdlib() is a Config error") {
    Config conf;
    conf.compiler("clang-cl").stdlib(StdLib::Libcxx);
    auto r = detail::apply_config(conf, host_triple());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
}

TEST_CASE("config: configure() end-to-end (clang++ + libc++ scratch build)") {
    ScratchDir scratch {"config-llvm"};
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("config-llvm", 1, argv);

    REQUIRE(detail::write_file("app/main.cpp", "int main() { return 0; }\n").has_value());
    proj.add_executable("app").sources("app/main.cpp");

    Config conf;
    conf.toolchain(Toolchain::LLVM);
    proj.configure(conf);
    auto r = proj.build();
    REQUIRE(r.has_value());
}
