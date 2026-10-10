module;
#include <doctest/doctest.h>

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

TEST_CASE("fetch_all: validation and duplicate names before any network access") {
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("pkg-test", 1, argv);

    FetchOptions git;
    git.git = "https://example.invalid/repo";
    git.tag = "v1";

    /* duplicate names rejected */
    auto dup = proj.fetch_all({{"a", git}, {"a", git}});
    REQUIRE_FALSE(dup.has_value());
    CHECK(dup.error().message.find("duplicate") != std::string::npos);

    /* per-entry option validation still applies */
    auto bad = proj.fetch_all({{"a", git}, {"b", FetchOptions {}}});
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().kind == Error::Kind::Config);

    /* empty batch is a valid no-op */
    auto empty = proj.fetch_all({});
    REQUIRE(empty.has_value());
    CHECK(empty->empty());
}

TEST_CASE("conan list overload: refs must be versioned") {
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj {"conan-list", 1, argv};

    auto bad = proj.conan({"zlib"}); /* missing /version */
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().kind == Error::Kind::Config);
    CHECK(bad.error().message.find("zlib") != std::string::npos);
}

TEST_CASE("conan path overload: missing conanfile is a Config error") {
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj {"conan-path", 1, argv};
    auto r = proj.conan("definitely-not-here.txt");
    if (!r) /* conan absent: toolchain error is fine too */
        CHECK((r.error().kind == Error::Kind::Config || r.error().kind == Error::Kind::Toolchain));
    else
        FAIL("expected failure");
}

TEST_CASE("copy_tree: hardlink materialization, .git skipped") {
    const auto root = std::filesystem::temp_directory_path() / "truss-copytree-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "src/include");
    std::filesystem::create_directories(root / ".git/objects");
    REQUIRE(detail::write_file(root / "src/a.h", "int a();\n").has_value());
    REQUIRE(detail::write_file(root / "src/include/b.h", "int b();\n").has_value());
    REQUIRE(detail::write_file(root / ".git/config", "x\n").has_value());

    const auto dest = root / "dest";
    REQUIRE(detail::copy_tree(root / "src", dest).has_value());

    CHECK(detail::read_file(dest / "a.h").value() == "int a();\n");
    CHECK(detail::read_file(dest / "include/b.h").value() == "int b();\n");
    CHECK(!std::filesystem::exists(dest / ".git"));
    /* same filesystem: files are hardlinks (identical inode) */
    std::error_code ec;
    CHECK(std::filesystem::equivalent(root / "src/a.h", dest / "a.h", ec));

    /* idempotent: skip_existing semantics keep existing dest files */
    REQUIRE(detail::write_file(dest / "a.h", "edited\n").has_value());
    REQUIRE(detail::copy_tree(root / "src", dest).has_value());
    CHECK(detail::read_file(dest / "a.h").value() == "edited\n");
    std::filesystem::remove_all(root);
}
