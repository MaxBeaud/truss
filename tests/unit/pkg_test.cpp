module;
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <map>
#include <string>

module truss;
import :api;
import :impl;

using namespace truss;

TEST_CASE("lockfile: parse comments, whitespace, empty values") {
    const auto p = std::filesystem::temp_directory_path() / "truss-lock-parse-test.lock";
    auto w = detail::write_file(p,
                                "# truss.lock\n"
                                "pkg.a = v1\n"
                                "  pkg.b   =  sha256value  \n"
                                "noequals\n"
                                "pkg.c = # empty value\n");
    REQUIRE(w.has_value());
    auto m = detail::read_lockfile(p);
    CHECK(m["pkg.a"] == "v1");
    CHECK(m["pkg.b"] == "sha256value");
    CHECK(m.count("noequals") == 0);
    CHECK(m["pkg.c"].empty());
    std::filesystem::remove(p);
}

TEST_CASE("lockfile: write/read round trip") {
    const auto p = std::filesystem::temp_directory_path() / "truss-lock-roundtrip-test.lock";
    const std::map<std::string, std::string> entries {
        {"pkg.one", "v1.0.0"},
        {"pkg.two", "deadbeef"},
    };
    auto w = detail::write_lockfile(p, entries);
    REQUIRE(w.has_value());
    auto m = detail::read_lockfile(p);
    CHECK(m == entries);
    std::filesystem::remove(p);
}

TEST_CASE("fetch: option validation errors before any network access") {
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("pkg-test", 1, argv);

    /* both sources set */
    FetchOptions both;
    both.git = "u";
    both.tag = "t";
    both.url = "v";
    both.sha256 = "w";
    auto r1 = proj.fetch("x", both);
    REQUIRE_FALSE(r1.has_value());
    CHECK(r1.error().kind == Error::Kind::Config);

    /* neither source set */
    auto r2 = proj.fetch("x", FetchOptions {});
    REQUIRE_FALSE(r2.has_value());
    CHECK(r2.error().kind == Error::Kind::Config);

    /* git without tag */
    FetchOptions git;
    git.git = "https://example.invalid/repo";
    auto r3 = proj.fetch("x", git);
    REQUIRE_FALSE(r3.has_value());
    CHECK(r3.error().kind == Error::Kind::Config);

    /* tarball without sha256 */
    FetchOptions tar;
    tar.url = "https://example.invalid/a.tar.gz";
    auto r4 = proj.fetch("x", tar);
    REQUIRE_FALSE(r4.has_value());
    CHECK(r4.error().kind == Error::Kind::Config);
}
