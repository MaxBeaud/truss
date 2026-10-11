module;
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

module truss;
import :api;
import :impl;

using namespace truss;

TEST_CASE("os: consteval platform predicates") {
    static_assert(os::host_os() != Os::Unknown);
    static_assert(os::host_arch() != Arch::Unknown);
    static_assert(os::host_family() != CompilerFamily::Unknown);
    static_assert(os::is_linux() || os::is_windows() || os::is_macos() || os::host_os() == Os::Unknown);

    bool compiled_branch = false;
    if constexpr (os::host_os() == Os::Linux)
        compiled_branch = true; // dead branches on other hosts don't even compile
    CHECK(compiled_branch);

    /* host_triple() shares the detection core */
    const TargetTriple t = host_triple();
    CHECK(t.os == os::host_os());
    CHECK(t.arch == os::host_arch());
    CHECK(t.family == os::host_family());
}

TEST_CASE("os: find_program PATH walk") {
    const auto sh = os::find_program("sh");
    REQUIRE(sh.has_value());
    CHECK(sh->is_absolute());
    std::error_code ec;
    CHECK(std::filesystem::is_regular_file(*sh, ec));

    CHECK_FALSE(os::find_program("definitely-not-a-program-xyzzy").has_value());
    CHECK(os::program_exists("sh"));
    CHECK_FALSE(os::program_exists("definitely-not-a-program-xyzzy"));

    /* first-hit overload: misses before the hit are fine */
    const auto first = os::find_program({"definitely-not-a-program-xyzzy", "sh"});
    REQUIRE(first.has_value());
    CHECK(*first == *sh);
    CHECK_FALSE(os::find_program({"nope-xyzzy-a", "nope-xyzzy-b"}).has_value());

    /* names containing '/' are checked as paths directly */
    CHECK(os::find_program("/bin/sh").has_value());
    CHECK_FALSE(os::find_program("/nonexistent-dir-xyzzy/sh").has_value());
    /* non-executable existing files are not programs */
    const auto not_exec = std::filesystem::temp_directory_path() / "truss-test-os-not-exec";
    detail::write_file(not_exec, "#!/bin/sh\n");
    std::filesystem::permissions(not_exec, std::filesystem::perms::owner_read,
                                 std::filesystem::perm_options::replace, ec);
    CHECK_FALSE(os::find_program(not_exec.generic_string()).has_value());
    std::filesystem::remove(not_exec, ec);
}

TEST_CASE("os: env lenient parsing") {
    CHECK(os::env("TRUSS_OS_TEST_UNSET_VAR").has_value() == false);
    CHECK(os::env("TRUSS_OS_TEST_UNSET_VAR", 42) == 42);
    CHECK(os::env("TRUSS_OS_TEST_UNSET_VAR", false) == false);

    setenv("TRUSS_OS_TEST_NUM", "12", 1);
    CHECK(os::env("TRUSS_OS_TEST_NUM", 8) == 12);
    setenv("TRUSS_OS_TEST_NUM", "12x", 1); // trailing junk -> default (whole match required)
    CHECK(os::env("TRUSS_OS_TEST_NUM", 8) == 8);
    setenv("TRUSS_OS_TEST_NUM", "", 1); // empty -> default
    CHECK(os::env("TRUSS_OS_TEST_NUM", 8) == 8);

    setenv("TRUSS_OS_TEST_BOOL", "YES", 1);
    CHECK(os::env("TRUSS_OS_TEST_BOOL", false) == true);
    setenv("TRUSS_OS_TEST_BOOL", "off", 1);
    CHECK(os::env("TRUSS_OS_TEST_BOOL", true) == false);
    setenv("TRUSS_OS_TEST_BOOL", "banana", 1);
    CHECK(os::env("TRUSS_OS_TEST_BOOL", true) == true); // unparseable -> default

    setenv("TRUSS_OS_TEST_STR", "clang++", 1);
    CHECK(os::env("TRUSS_OS_TEST_STR", "g++") == "clang++"); // const char* sugar deduces string
    const std::string def = "g++";
    CHECK(os::env("TRUSS_OS_TEST_STR", def) == "clang++");

    setenv("TRUSS_OS_TEST_DBL", "2.5", 1);
    CHECK(os::env("TRUSS_OS_TEST_DBL", 1.0) == 2.5);

    unsetenv("TRUSS_OS_TEST_NUM");
    unsetenv("TRUSS_OS_TEST_BOOL");
    unsetenv("TRUSS_OS_TEST_STR");
    unsetenv("TRUSS_OS_TEST_DBL");
}

TEST_CASE("os: all_programs / any_program") {
    CHECK(os::all_programs({"sh", "cat"}));
    CHECK_FALSE(os::all_programs({"sh", "definitely-not-a-program-xyzzy"}));
    CHECK(os::any_program({"definitely-not-a-program-xyzzy", "sh"}));
    CHECK_FALSE(os::any_program({"nope-xyzzy-a", "nope-xyzzy-b"}));
    CHECK(os::all_programs({})); // vacuous truth
    CHECK_FALSE(os::any_program({}));
}

TEST_CASE("os: machine specs") {
    CHECK(os::cpu_count() >= 1);
    if constexpr (os::is_linux())
        CHECK(os::memory_bytes() > 0);
    auto ver = os::os_version();
    REQUIRE(ver.has_value());
    CHECK_FALSE(ver->empty());
    auto host = os::hostname();
    REQUIRE(host.has_value());
    CHECK_FALSE(host->empty());
    std::error_code ec;
    CHECK(std::filesystem::is_directory(os::temp_dir(), ec));
}

TEST_CASE("os: temp_scope RAII") {
    std::error_code ec;
    std::filesystem::path dir;
    {
        os::temp_scope scope;
        REQUIRE(scope.valid());
        dir = scope.path();
        CHECK(std::filesystem::is_directory(dir, ec));
        detail::write_file(dir / "inner.txt", "x");
        {
            os::temp_scope moved {std::move(scope)};
            CHECK(moved.path() == dir);
            CHECK_FALSE(scope.valid());
        } // moved-to scope's destructor removes the directory
        CHECK(std::filesystem::exists(dir, ec) == false);
    }
    {
        os::temp_scope scope;
        REQUIRE(scope.valid());
        dir = scope.path();
        scope.release();
    }
    CHECK(std::filesystem::is_directory(dir, ec)); // release() kept it
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("os: run / capture") {
    const std::array<std::string, 3> echo {"sh", "-c", "echo hi"};
    auto cap = os::capture(echo);
    REQUIRE(cap.has_value());
    CHECK(cap->exit_code == 0);
    CHECK(cap->output == "hi\n");

    const std::array<std::string, 3> fail {"sh", "-c", "exit 3"};
    auto run = os::run(fail);
    REQUIRE(run.has_value());
    CHECK(run->exit_code == 3); // non-zero exit is a result, not an Error

    const std::array<std::string, 3> pwd {"sh", "-c", "pwd"};
    os::temp_scope scope;
    REQUIRE(scope.valid());
    auto in_dir = os::capture(pwd, {.cwd = scope.path()});
    REQUIRE(in_dir.has_value());
    auto trimmed = in_dir->output;
    trimmed.erase(trimmed.find_last_not_of("\n") + 1);
    CHECK(fs::generic_str(std::filesystem::weakly_canonical(trimmed))
          == fs::generic_str(std::filesystem::weakly_canonical(scope.path())));

    const std::array<std::string, 1> nope {"/nonexistent/truss-binary-xyzzy"};
    auto err = os::run(nope);
    REQUIRE_FALSE(err.has_value());
    CHECK(err.error().kind == Error::Kind::Command);
}

TEST_CASE("log: public channel smoke test") {
    log::info("os_test: info line");
    log::warn("os_test: warn line");
    log::success("os_test: success line");
    log::error("os_test: error line");
    log::error(Error {Error::Kind::Config, "os_test: error with path", std::filesystem::path {"/some/path"}});
    CHECK(true); // no crash + visible in the run output
}
