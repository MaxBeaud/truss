module;
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

module truss;
import :api;
import :impl;

using namespace truss;

TEST_CASE("fs: write_file creates parents and round-trips") {
    const auto p = std::filesystem::temp_directory_path() / "truss-test-fs/nested/out.txt";
    std::filesystem::remove_all(p.parent_path().parent_path());
    auto w = detail::write_file(p, "hello truss");
    REQUIRE(w.has_value());
    auto r = detail::read_file(p);
    REQUIRE(r.has_value());
    CHECK(*r == "hello truss");
    std::filesystem::remove_all(p.parent_path().parent_path());
}

TEST_CASE("fs: read_file on missing path yields Io error") {
    auto r = detail::read_file("/definitely/not/here/truss-test.txt");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Io);
}

TEST_CASE("fs: make_dirs and remove_tree") {
    const auto p = std::filesystem::temp_directory_path() / "truss-test-dirs/a/b/c";
    auto m = detail::make_dirs(p);
    REQUIRE(m.has_value());
    CHECK(std::filesystem::exists(p));
    auto rm = detail::remove_tree(p.parent_path().parent_path());
    REQUIRE(rm.has_value());
    CHECK_FALSE(std::filesystem::exists(p));
}

TEST_CASE("proc: run_process captures stdout+stderr and exit code") {
    const std::array<std::string, 3> argv {"sh", "-c", "echo out; echo err >&2; exit 3"};
    auto r = detail::run_process(argv, true);
    REQUIRE(r.has_value());
    CHECK(r->exit_code == 3);
    CHECK(r->output.find("out") != std::string::npos);
    CHECK(r->output.find("err") != std::string::npos);
}

TEST_CASE("proc: spawn failure is a Command error") {
    const std::array<std::string, 1> argv {"/nonexistent/truss-binary"};
    auto r = detail::run_process(argv, false);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Command);
}

TEST_CASE("proc: program_exists") {
    CHECK(detail::program_exists("sh"));
    CHECK_FALSE(detail::program_exists("definitely-not-a-command-xyzzy"));
}

TEST_CASE("triple: host detection is populated") {
    const TargetTriple t = host_triple();
    CHECK(t.os != Os::Unknown);
    CHECK(t.arch != Arch::Unknown);
    CHECK(t.family != CompilerFamily::Unknown);
    CHECK_FALSE(t.compiler.empty());
    CHECK_FALSE(t.ar.empty());
#if defined(__linux__) && !defined(__ANDROID__)
    CHECK(t.os == Os::Linux);
#endif
}

TEST_CASE("triple: triple_dir format") {
    TargetTriple t;
    t.arch = Arch::X64;
    t.os = Os::Linux;
    t.family = CompilerFamily::Clang;
    CHECK(detail::triple_dir(t) == "x64-linux-clang");
}
