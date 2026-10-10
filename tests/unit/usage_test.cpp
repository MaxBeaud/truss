module;
#include <doctest/doctest.h>

#include <filesystem>
#include <chrono>
#include <expected>
#include <string>
#include <vector>

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

TEST_CASE("hooks: pre_build/post_build run in order around the build") {
    ScratchDir scratch {"hooks-order"};
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj {"hooks-order", 1, argv};
    REQUIRE(detail::write_file("hk/app.cpp", "int main() { return 0; }\n").has_value());
    proj.add_executable("app").sources("hk/app.cpp");

    std::vector<std::string> log;
    proj.pre_build([&](const TaskContext&) { log.push_back("pre1"); return Result<void> {}; });
    proj.pre_build([&](const TaskContext&) { log.push_back("pre2"); return Result<void> {}; });
    proj.post_build([&](const TaskContext&) { log.push_back("post"); return Result<void> {}; });

    auto r = proj.build();
    REQUIRE(r.has_value());
    CHECK(log == std::vector<std::string> {"pre1", "pre2", "post"});
}

TEST_CASE("hooks: failing pre_build aborts before compiling") {
    ScratchDir scratch {"hooks-abort"};
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj {"hooks-abort", 1, argv};
    REQUIRE(detail::write_file("hk2/app.cpp", "int main() { return 0; }\n").has_value());
    proj.add_executable("app").sources("hk2/app.cpp");

    bool post_ran = false;
    proj.pre_build([](const TaskContext&) -> Result<void> {
        return std::unexpected(Error {Error::Kind::Config, "pre_build hook said no"});
    });
    proj.post_build([&](const TaskContext&) { post_ran = true; return Result<void> {}; });

    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().message.find("pre_build") != std::string::npos);
    CHECK_FALSE(post_ran);
    CHECK_FALSE(std::filesystem::exists("hk2-build-marker")); /* nothing compiled */
}

TEST_CASE("hooks: failing post_build aborts the build result") {
    ScratchDir scratch {"hooks-post-fail"};
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj {"hooks-post-fail", 1, argv};
    REQUIRE(detail::write_file("hk3/app.cpp", "int main() { return 0; }\n").has_value());
    proj.add_executable("app").sources("hk3/app.cpp");

    proj.post_build([](const TaskContext&) -> Result<void> {
        return std::unexpected(Error {Error::Kind::Config, "post_build hook failed"});
    });
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().message.find("post_build") != std::string::npos);
}

TEST_CASE("launcher: resolve_launcher rules") {
    auto auto_r = detail::resolve_launcher(Config {});
    REQUIRE(auto_r.has_value());
    CHECK((auto_r->empty() || *auto_r == "ccache" || *auto_r == "sccache"));

    CHECK(detail::resolve_launcher(Config().launcher("")).value().empty());

    auto bad = detail::resolve_launcher(Config().launcher("truss-no-such-launcher"));
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().kind == Error::Kind::Config);
}

TEST_CASE("launcher: explicit launcher prefixes compile commands and runs") {
    ScratchDir scratch {"launcher"};
    REQUIRE(detail::write_file("lk/launcher.sh",
                               "#!/bin/sh\necho hit >> lk/launcher-hits.txt\nexec \"$@\"\n").has_value());
    std::filesystem::permissions("lk/launcher.sh",
                                 std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read,
                                 std::filesystem::perm_options::add);
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("launcher", 1, argv);
    proj.configure(Config().launcher("./lk/launcher.sh"));
    REQUIRE(detail::write_file("lk/app.cpp", "int main() { return 0; }\n").has_value());
    proj.add_executable("app").sources("lk/app.cpp");
    REQUIRE(proj.build().has_value());

    auto manifest = detail::read_file("build/debug/ninja/build.ninja");
    REQUIRE(manifest.has_value());
    CHECK(manifest->find("command = ./lk/launcher.sh $cxx") != std::string::npos);
    auto hits = detail::read_file("lk/launcher-hits.txt");
    CHECK((hits && hits->find("hit") != std::string::npos));
}

TEST_CASE("launcher: launcher(\"\") keeps compile commands raw") {
    ScratchDir scratch {"launcher-off"};
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("launcher-off", 1, argv);
    proj.configure(Config().launcher(""));
    REQUIRE(detail::write_file("lo/app.cpp", "int main() { return 0; }\n").has_value());
    proj.add_executable("app").sources("lo/app.cpp");
    REQUIRE(proj.build().has_value());
    auto manifest = detail::read_file("build/debug/ninja/build.ninja");
    REQUIRE(manifest.has_value());
    CHECK(manifest->find("command = $cxx") != std::string::npos);
}

TEST_CASE("pkg: conan configure-skip caches a spec keyed by conanfile hash") {
    if (!detail::program_exists("conan")) {
        MESSAGE("conan not installed, skipping");
        return;
    }
    ScratchDir scratch {"conan-skip"};
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    REQUIRE(detail::write_file("cs/conanfile.txt", "[requires]\nzlib/1.3.1\n").has_value());

    {
        Project proj("conan-skip", 1, argv);
        auto z = proj.conan("cs/conanfile.txt");
        REQUIRE(z.has_value());
    }
    const std::filesystem::path spec = "build/debug/packages/conan/truss-spec.json";
    REQUIRE(std::filesystem::exists(spec));
    const auto mtime1 = std::filesystem::last_write_time(spec);

    { /* same conanfile: spec hit, conan install skipped (spec not rewritten) */
        Project proj("conan-skip", 1, argv);
        auto z = proj.conan("cs/conanfile.txt");
        REQUIRE(z.has_value());
        CHECK(std::filesystem::last_write_time(spec) == mtime1);
    }
    { /* changed conanfile: fingerprint miss, conan re-runs and rewrites the spec */
        REQUIRE(detail::write_file("cs/conanfile.txt", "[requires]\nzlib/1.3.1\n# touched\n").has_value());
        Project proj("conan-skip", 1, argv);
        auto z = proj.conan("cs/conanfile.txt");
        REQUIRE(z.has_value());
        CHECK(std::filesystem::last_write_time(spec) != mtime1);
    }
}

TEST_CASE("pch: clang -include-pch edge + rebuild minimality") {
    ScratchDir scratch {"pch-clang"};
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    REQUIRE(detail::write_file("pc/pch.hpp", "#pragma once\n#include <string>\ninline int pf() { return 42; }\n").has_value());
    REQUIRE(detail::write_file("pc/a.cpp", "int pf();\nint a() { return pf(); }\n").has_value());
    REQUIRE(detail::write_file("pc/b.cpp", "int pf();\nint b() { return pf() + 1; }\nint main() { return b() - 43; }\n").has_value());
    Project proj("pch-clang", 1, argv);
    proj.add_executable("app").pch("pc/pch.hpp").sources({"pc/a.cpp", "pc/b.cpp"});
    REQUIRE(proj.build().has_value());

    auto manifest = detail::read_file("build/debug/ninja/build.ninja");
    REQUIRE(manifest.has_value());
    CHECK(manifest->find("rule compile_pch") != std::string::npos);
    CHECK(manifest->find("-include-pch") != std::string::npos);
    CHECK(std::filesystem::exists("build/debug/pch/app/pch.hpp.pch"));

    /* touch the PCH header: both objects rebuild */
    const auto oa = std::filesystem::path("build/debug/obj/x64-linux-clang/app/pc/a.cpp.o");
    const auto ob = std::filesystem::path("build/debug/obj/x64-linux-clang/app/pc/b.cpp.o");
    const auto ma = std::filesystem::last_write_time(oa);
    const auto mb = std::filesystem::last_write_time(ob);
    std::filesystem::last_write_time("pc/pch.hpp", std::filesystem::file_time_type::clock::now());
    Project proj2("pch-clang", 1, argv);
    proj2.add_executable("app").pch("pc/pch.hpp").sources({"pc/a.cpp", "pc/b.cpp"});
    REQUIRE(proj2.build().has_value());
    CHECK(std::filesystem::last_write_time(oa) != ma);
    CHECK(std::filesystem::last_write_time(ob) != mb);

    /* touch one TU: only that object rebuilds */
    const auto ma2 = std::filesystem::last_write_time(oa);
    const auto mb2 = std::filesystem::last_write_time(ob);
    std::filesystem::last_write_time("pc/b.cpp", std::filesystem::file_time_type::clock::now());
    Project proj3("pch-clang", 1, argv);
    proj3.add_executable("app").pch("pc/pch.hpp").sources({"pc/a.cpp", "pc/b.cpp"});
    REQUIRE(proj3.build().has_value());
    CHECK(std::filesystem::last_write_time(oa) == ma2);
    CHECK(std::filesystem::last_write_time(ob) != mb2);
}

TEST_CASE("pch: missing header is a Config error") {
    ScratchDir scratch {"pch-missing"};
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    REQUIRE(detail::write_file("pm/a.cpp", "int main() { return 0; }\n").has_value());
    Project proj("pch-missing", 1, argv);
    proj.add_executable("app").pch("pm/nope.hpp").sources("pm/a.cpp");
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(r.error().message.find("pch") != std::string::npos);
}

TEST_CASE("pch: GCC family uses .gch + -I/-include") {
    if (!detail::program_exists("g++")) {
        MESSAGE("g++ not installed, skipping");
        return;
    }
    ScratchDir scratch {"pch-gcc"};
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    REQUIRE(detail::write_file("pg/pch.hpp", "#pragma once\n#include <string>\ninline int pf() { return 42; }\n").has_value());
    REQUIRE(detail::write_file("pg/a.cpp", "int pf();\nint main() { return pf() - 42; }\n").has_value());
    Project proj("pch-gcc", 1, argv);
    proj.configure(Config().toolchain(Toolchain::GCC));
    proj.add_executable("app").pch("pg/pch.hpp").sources("pg/a.cpp");
    REQUIRE(proj.build().has_value());
    CHECK(std::filesystem::exists("build/debug/pch/app/pch.hpp.gch"));
    auto manifest = detail::read_file("build/debug/ninja/build.ninja");
    REQUIRE(manifest.has_value());
    CHECK(manifest->find("-include pch.hpp") != std::string::npos);
}

TEST_CASE("pch: MSVC family is a clear Config error") {
    ScratchDir scratch {"pch-msvc"};
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    REQUIRE(detail::write_file("pv/pch.hpp", "#pragma once\n").has_value());
    REQUIRE(detail::write_file("pv/a.cpp", "int main() { return 0; }\n").has_value());
    Project proj("pch-msvc", 1, argv);
    TargetTriple t;
    t.os = Os::Windows;
    t.arch = Arch::X64;
    t.family = CompilerFamily::Msvc;
    t.compiler = "clang-cl";
    t.ar = "llvm-lib";
    proj.add_executable("app").pch("pv/pch.hpp").sources("pv/a.cpp").cross_target(t);
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(r.error().message.find("MSVC") != std::string::npos);
}
