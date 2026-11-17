module;
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

module truss;
import :api;
import :impl;

using namespace truss;

namespace {
bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}
} // namespace

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

TEST_CASE("fs: lockfile comments are line-leading only (pins may contain #)") {
    const auto p = std::filesystem::temp_directory_path() / "truss-test-lock/truss.lock";
    std::filesystem::remove_all(p.parent_path());
    REQUIRE(detail::write_file(p,
                               "# comment line\n"
                               "pkg.zlib = v1.2#not-a-comment\n"
                               "   # indented comment\n"
                               "pkg.fmt = 11.0.2\n"
                               "\n"
                               "no-key-line\n")
                .has_value());
    const auto lock = detail::read_lockfile(p);
    CHECK(lock.size() == 2);
    CHECK(lock.at("pkg.zlib") == "v1.2#not-a-comment");
    CHECK(lock.at("pkg.fmt") == "11.0.2");
    std::filesystem::remove_all(p.parent_path());
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
    CHECK(os::program_exists("sh"));
    CHECK_FALSE(os::program_exists("definitely-not-a-command-xyzzy"));
}

TEST_CASE("proc: capture timeout kills the process group") {
    const auto t0 = std::chrono::steady_clock::now();
    auto r = os::capture({"sh", "-c", "sleep 30; echo done"}, {.timeout = 1});
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Command);
    CHECK(contains(r.error().message, "timed out"));
    /* the kill must be prompt, not a 30 s wait for the child */
    CHECK(elapsed < std::chrono::seconds(10));
}

TEST_CASE("proc: a daemonizing grandchild cannot wedge the capture") {
    /* the backgrounded sleep inherits the capture pipe and outlives sh:
     * without the process-group kill this capture would hang for 30 s */
    const auto t0 = std::chrono::steady_clock::now();
    auto r = os::capture({"sh", "-c", "sleep 30 & echo started"}, {.timeout = 2});
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    REQUIRE_FALSE(r.has_value());
    CHECK(contains(r.error().message, "timed out"));
    CHECK(elapsed < std::chrono::seconds(10));
}

TEST_CASE("proc: a detached grandchild releases the pipe (no false timeout)") {
    auto r = os::capture({"sh", "-c", "sleep 30 >/dev/null 2>&1 & echo hi"}, {.timeout = 5});
    REQUIRE(r.has_value());
    CHECK(contains(r->output, "hi"));
}

TEST_CASE("proc: run() without a timeout is unbounded (0 = none)") {
    auto r = os::run({"sh", "-c", "sleep 1"}, {.timeout = 0});
    REQUIRE(r.has_value());
    CHECK(r->exit_code == 0);
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

/* --- phase 13: public truss::fs ------------------------------------------- */

namespace {
const std::filesystem::path kFsRoot {"/tmp/truss-test-fs-helpers"};

void make_fs_tree() {
    std::error_code ec;
    std::filesystem::remove_all(kFsRoot, ec);
    detail::write_file(kFsRoot / "a/b/c.txt", "c\n");
    detail::write_file(kFsRoot / "a/top.txt", "top\n");
    detail::write_file(kFsRoot / "a/.hidden/x.txt", "hx\n");
    detail::write_file(kFsRoot / "a/b/.dotfile", "dot\n");
    detail::write_file(kFsRoot / "libs/libfoo.a", "archive");
    detail::write_file(kFsRoot / "libs/bar.lib", "msvc import lib");
}
} // namespace

TEST_CASE("fs: walk depth + hidden policy") {
    make_fs_tree();
    std::vector<std::string> all;
    for (const auto& p : fs::walk(kFsRoot / "a"))
        all.push_back(fs::generic_str(p));
    std::ranges::sort(all);
    /* hidden entry + no descent into hidden dir; dirs are yielded too */
    CHECK(all == std::vector<std::string> {
        "/tmp/truss-test-fs-helpers/a/b",
        "/tmp/truss-test-fs-helpers/a/b/c.txt",
        "/tmp/truss-test-fs-helpers/a/top.txt",
    });

    /* depth 0 = plain listing (b yielded but not descended) */
    std::vector<std::string> flat;
    for (const auto& p : fs::walk(kFsRoot / "a", {.depth = 0}))
        flat.push_back(p.filename().string());
    std::ranges::sort(flat);
    CHECK(flat == std::vector<std::string> {"b", "top.txt"});

    /* depth 1 reaches b/c.txt but not deeper */
    std::vector<std::string> d1;
    for (const auto& p : fs::walk(kFsRoot / "a", {.depth = 1}))
        d1.push_back(fs::generic_str(p));
    CHECK(std::ranges::find(d1, "/tmp/truss-test-fs-helpers/a/b/c.txt") != d1.end());

    /* skip_hidden=false includes dot entries */
    std::vector<std::string> with_hidden;
    for (const auto& p : fs::walk(kFsRoot / "a", {.skip_hidden = false}))
        with_hidden.push_back(p.filename().string());
    CHECK(std::ranges::find(with_hidden, ".dotfile") != with_hidden.end());
    CHECK(std::ranges::find(with_hidden, ".hidden") != with_hidden.end());

    /* missing root yields nothing (forgiving) */
    int count = 0;
    for ([[maybe_unused]] const auto& p : fs::walk(kFsRoot / "nope-xyzzy"))
        ++count;
    CHECK(count == 0);
    std::filesystem::remove_all(kFsRoot);
}

TEST_CASE("fs: find_upward / first_existing / current_dir") {
    make_fs_tree();
    const auto hit = fs::find_upward("marker-xyzzy.txt", kFsRoot / "a/b");
    CHECK_FALSE(hit.has_value());
    detail::write_file("/tmp/truss-test-fs-helpers/marker-xyzzy.txt", "x");
    const auto found = fs::find_upward("marker-xyzzy.txt", kFsRoot / "a/b");
    REQUIRE(found.has_value());
    CHECK(*found == "/tmp/truss-test-fs-helpers/marker-xyzzy.txt");

    const auto first = fs::first_existing({kFsRoot / "nope", kFsRoot / "a/top.txt", kFsRoot / "also-nope"});
    REQUIRE(first.has_value());
    CHECK(*first == kFsRoot / "a/top.txt");
    CHECK_FALSE(fs::first_existing({"/nope-1", "/nope-2"}).has_value());

    auto cwd = fs::current_dir();
    REQUIRE(cwd.has_value());
    CHECK(*cwd == std::filesystem::current_path());
    std::filesystem::remove_all(kFsRoot);
}

TEST_CASE("fs: find_library") {
    make_fs_tree();
    const std::array dirs {kFsRoot / "libs", kFsRoot / "a"};
    const auto foo = fs::find_library("foo", dirs);
    REQUIRE(foo.has_value());
    CHECK(*foo == kFsRoot / "libs/libfoo.a");
    const auto bar = fs::find_library("bar", dirs); /* bare MSVC-style name */
    REQUIRE(bar.has_value());
    CHECK(*bar == kFsRoot / "libs/bar.lib");
    CHECK_FALSE(fs::find_library("nope-xyzzy", dirs).has_value());

    /* default system paths: pick something that exists on any Linux box */
    if constexpr (os::is_linux()) {
        const auto c = fs::find_library("c"); /* libc.so lives in a system dir */
        CHECK(c.has_value());
    }
    CHECK_FALSE(fs::find_library("definitely-not-a-system-lib-xyzzy").has_value());
    std::filesystem::remove_all(kFsRoot);
}

TEST_CASE("fs: glob / read_lines / sha256_file / generic_str promotions") {
    auto matches = fs::glob({"src/*.cppm"});
    REQUIRE(matches.has_value());
    CHECK(std::ranges::find(*matches, std::filesystem::path {"src/api.cppm"}) != matches->end());
    auto none = fs::glob("nope-xyzzy/**/*.txt");
    REQUIRE(none.has_value());
    CHECK(none->empty());

    const auto p = kFsRoot / "lines.txt";
    detail::write_file(p, "one\ntwo\nthree\n");
    auto lines = fs::read_lines(p);
    REQUIRE(lines.has_value());
    std::vector<std::string> got;
    for (auto& line : *lines)
        got.push_back(line);
    CHECK(got == std::vector<std::string> {"one", "two", "three"});
    auto missing = fs::read_lines(kFsRoot / "nope.txt");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().kind == Error::Kind::Io);

    /* FIPS 180-4 known vectors */
    detail::write_file(kFsRoot / "abc.txt", "abc");
    auto abc = fs::sha256_file(kFsRoot / "abc.txt");
    REQUIRE(abc.has_value());
    CHECK(*abc == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    detail::write_file(kFsRoot / "empty.txt", "");
    auto empty = fs::sha256_file(kFsRoot / "empty.txt");
    REQUIRE(empty.has_value());
    CHECK(*empty == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    /* long input crosses the 56..63 byte padding edge */
    detail::write_file(kFsRoot / "long.txt", std::string(100, 'x'));
    auto long_hash = fs::sha256_file(kFsRoot / "long.txt");
    REQUIRE(long_hash.has_value());
    CHECK(*long_hash == "09ecb6ebc8bcefc733f6f2ec44f791abeed6a99edf0cc31519637898aebd52d8");

    CHECK(fs::generic_str(std::filesystem::path {"a/b/c"}) == "a/b/c");
    CHECK(fs::generic_str(p) == "/tmp/truss-test-fs-helpers/lines.txt");
    std::filesystem::remove_all(kFsRoot);
}

TEST_CASE("fs: write_text / write_text_if_changed / copy_tree") {
    const auto root = std::filesystem::temp_directory_path() / "truss-test-text-copy";
    std::filesystem::remove_all(root);
    std::error_code ec;

    auto w = fs::write_text(root / "deep/nested/file.txt", "v1");
    REQUIRE(w.has_value());
    auto r = fs::read_text(root / "deep/nested/file.txt");
    REQUIRE(r.has_value());
    CHECK(*r == "v1");

    const auto mtime1 = std::filesystem::last_write_time(root / "deep/nested/file.txt", ec);
    auto same = fs::write_text_if_changed(root / "deep/nested/file.txt", "v1");
    REQUIRE(same.has_value());
    const auto mtime2 = std::filesystem::last_write_time(root / "deep/nested/file.txt", ec);
    CHECK(mtime1 == mtime2); // identical content: file untouched

    auto diff = fs::write_text_if_changed(root / "deep/nested/file.txt", "v2");
    REQUIRE(diff.has_value());
    r = fs::read_text(root / "deep/nested/file.txt");
    REQUIRE(r.has_value());
    CHECK(*r == "v2");

    // copy_tree: nested content copied, .git skipped, hardlink materialization
    detail::write_file(root / "src/a.cpp", "int a;");
    detail::write_file(root / "src/sub/b.cpp", "int b;");
    detail::write_file(root / "src/.git/config", "never copied");
    auto cp = fs::copy_tree(root / "src", root / "out");
    REQUIRE(cp.has_value());
    auto a = fs::read_text(root / "out/a.cpp");
    REQUIRE(a.has_value());
    CHECK(*a == "int a;");
    auto b = fs::read_text(root / "out/sub/b.cpp");
    REQUIRE(b.has_value());
    CHECK(*b == "int b;");
    CHECK_FALSE(std::filesystem::exists(root / "out/.git", ec));
    CHECK(std::filesystem::equivalent(root / "out/a.cpp", root / "src/a.cpp", ec)); // hardlink

    std::filesystem::remove_all(root, ec);
}

TEST_CASE("fs: sha256_file streams big files (chunk boundaries)") {
    /* deterministic content; digests pinned from coreutils sha256sum /
     * hashlib — covers the streaming context across 64 KiB chunk boundaries:
     * exact chunk, chunk+1 byte, and a partial final block */
    const auto p = std::filesystem::temp_directory_path() / "truss-test-sha-big.bin";
    std::string content;
    content.reserve(200000);
    for (int i = 0; i < 200000; ++i)
        content.push_back(static_cast<char>((i * 7 + (i >> 8)) & 0xff));

    struct Case { int size; const char* hash; };
    for (const Case& c : {Case {65536, "5fce37f3129150ce7ec3939b54016d9c1fd01364e27b0a788dc634064aec76b1"},
                          Case {65537, "662bcb0c13ee4da585a79aceace59f1755da8a51f2da8a1d5726ee3622bb0457"},
                          Case {200000, "df0f6306fcfb22cadc19e9868af0728ea9444c363c55ac205c4782a827176d9e"}}) {
        detail::write_file(p, std::string_view(content).substr(0, c.size));
        auto h = fs::sha256_file(p);
        REQUIRE(h.has_value());
        CHECK(*h == c.hash);
    }
    std::error_code ec;
    std::filesystem::remove(p, ec);
}

TEST_CASE("fs: parse_depfile reads prerequisites") {
    using V = std::vector<std::string>;
    CHECK(detail::parse_depfile("t.o: t.cpp a.h b.h\n") == V {"t.cpp", "a.h", "b.h"});
    /* wrapped lines (` \` + 2-space continuation) and a second rule */
    CHECK(detail::parse_depfile("t.o: t.cpp a.h \\\n  b.h \\\n  c.h\ntruss: x.pcm y.a\n")
          == V {"t.cpp", "a.h", "b.h", "c.h", "x.pcm", "y.a"});
    /* backslash-space is a space inside one path */
    CHECK(detail::parse_depfile("t.o: t.cpp dir\\ with\\ space/h.h\n") == V {"t.cpp", "dir with space/h.h"});
    CHECK(detail::parse_depfile("").empty());
}
