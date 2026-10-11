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
