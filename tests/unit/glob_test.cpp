module;
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

module truss;
import :api;
import :impl;

using namespace truss;

namespace {

/* RAII scratch directory; glob() is cwd-relative, so we chdir into it. */
class ScratchDir {
public:
    explicit ScratchDir(const std::string& name) {
        saved_ = std::filesystem::current_path();
        dir_ = std::filesystem::temp_directory_path() / ("truss-test-" + name);
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
        ::chdir(dir_.c_str());
    }
    ~ScratchDir() {
        ::chdir(saved_.c_str());
        std::filesystem::remove_all(dir_);
    }
    void touch(const std::string& rel) {
        const std::filesystem::path p(rel);
        if (p.has_parent_path())
            std::filesystem::create_directories(p.parent_path());
        std::ofstream(p) << "// x\n";
    }
    std::vector<std::string> glob_strs(std::string_view pattern) {
        auto r = detail::glob(pattern);
        REQUIRE(r.has_value());
        std::vector<std::string> out;
        for (const auto& p : *r)
            out.push_back(p.generic_string());
        return out;
    }

private:
    std::filesystem::path saved_;
    std::filesystem::path dir_;
};

} // namespace

TEST_CASE("glob: * does not cross path separators") {
    ScratchDir s("glob1");
    s.touch("sub/a.cpp");
    s.touch("sub/b.txt");
    s.touch("sub/deep/c.cpp");
    CHECK(s.glob_strs("sub/*.cpp") == std::vector<std::string>{"sub/a.cpp"});
}

TEST_CASE("glob: ** crosses separators") {
    ScratchDir s("glob2");
    s.touch("sub/a.cpp");
    s.touch("sub/deep/c.cpp");
    s.touch("sub/deep/more/d.cpp");
    CHECK(s.glob_strs("sub/**.cpp") ==
          std::vector<std::string>{"sub/a.cpp", "sub/deep/c.cpp", "sub/deep/more/d.cpp"});
}

TEST_CASE("glob: a full **/ component matches whole components only") {
    ScratchDir s("glob-starstar");
    s.touch("a/b.cpp");
    s.touch("a/x/b.cpp");
    s.touch("fox.cpp");
    s.touch("a/xb.cpp");
    /* a whole double-star component: zero or more complete components */
    CHECK(s.glob_strs("**/b.cpp") == std::vector<std::string>{"a/b.cpp", "a/x/b.cpp"});
    CHECK(s.glob_strs("a/**/b.cpp") == std::vector<std::string>{"a/b.cpp", "a/x/b.cpp"});
    /* a partial `**` (not a whole component) keeps its old meaning */
    CHECK(s.glob_strs("f**.cpp") == std::vector<std::string>{"fox.cpp"});
}

TEST_CASE("glob: a non-directory base matches nothing instead of erroring") {
    ScratchDir s("glob-filebase");
    s.touch("plain.txt");
    CHECK(s.glob_strs("plain.txt/*.cpp").empty());
}

TEST_CASE("glob: ? matches exactly one char") {
    ScratchDir s("glob3");
    s.touch("a.cpp");
    s.touch("ab.cpp");
    CHECK(s.glob_strs("?.cpp") == std::vector<std::string>{"a.cpp"});
}

TEST_CASE("glob: {a,b} alternation") {
    ScratchDir s("glob4");
    s.touch("x.cpp");
    s.touch("x.h");
    s.touch("x.txt");
    CHECK(s.glob_strs("x.{cpp,h}") == std::vector<std::string>{"x.cpp", "x.h"});
}

TEST_CASE("glob: hidden entries are skipped") {
    ScratchDir s("glob5");
    s.touch(".hidden.cpp");
    s.touch(".dir/a.cpp");
    s.touch("visible.cpp");
    CHECK(s.glob_strs("**.cpp") == std::vector<std::string>{"visible.cpp"});
}

TEST_CASE("glob: missing base matches nothing, not an error") {
    ScratchDir s("glob6");
    CHECK(s.glob_strs("nope/**.cpp").empty());
}

TEST_CASE("glob_multi: one call, several patterns, deduped union") {
    ScratchDir s("glob8");
    s.touch("src/a.cpp");
    s.touch("src/b.c");
    s.touch("src/sub/c.cppm");
    s.touch("src/notes.txt");
    const std::vector<std::string> patterns {
        "src/**.cpp", "src/**.c", "src/**.cppm", "src/**.cc", "src/**.cxx",
    };
    auto r = detail::glob_multi(patterns);
    REQUIRE(r.has_value());
    std::vector<std::string> out;
    for (const auto& p : *r)
        out.push_back(p.generic_string());
    CHECK(out == std::vector<std::string>{"src/a.cpp", "src/b.c", "src/sub/c.cppm"});
}

TEST_CASE("glob: regex metacharacters in directory names are literal") {
    ScratchDir s("glob7");
    s.touch("a.b+c/e.cpp");
    CHECK(s.glob_strs("a.b+c/*.cpp") == std::vector<std::string>{"a.b+c/e.cpp"});
}

TEST_CASE("has_glob_magic") {
    CHECK(detail::has_glob_magic("a*b"));
    CHECK(detail::has_glob_magic("a?b"));
    CHECK(detail::has_glob_magic("a{b,c}"));
    CHECK_FALSE(detail::has_glob_magic("plain/path.cpp"));
}

TEST_CASE("glob: explicit parent (..) and hidden prefixes work; unbalanced brace does not throw") {
    ScratchDir s("glob-edge");
    s.touch("sibling/a.cpp");
    s.touch("sibling/.secret.cpp");
    s.touch("work/.gen/g.cpp");
    s.touch("work/.gen/.hidden.cpp");
    s.touch("work/foo{2.cpp");
    s.touch("work/lit.cpp");
    REQUIRE(::chdir("work") == 0);
    /* ../ sibling path is not swallowed as a hidden component */
    CHECK(s.glob_strs("../sibling/*.cpp") == std::vector<std::string>{"../sibling/a.cpp"});
    /* explicit hidden prefix dir works while dotfiles underneath stay skipped */
    CHECK(s.glob_strs(".gen/*.cpp") == std::vector<std::string>{".gen/g.cpp"});
    /* unbalanced '{' is matched literally without throwing regex_error */
    CHECK(s.glob_strs("foo{2.cpp") == std::vector<std::string>{"foo{2.cpp"});
    /* literal path fast-path */
    CHECK(s.glob_strs("lit.cpp") == std::vector<std::string>{"lit.cpp"});
}

TEST_CASE("glob: depth capping matches exact levels and mixes with **") {
    ScratchDir s("glob-depth");
    s.touch("top.cpp");
    s.touch("a/d0.cpp");
    s.touch("a/b/d1.cpp");
    s.touch("a/b/c/d2.cpp");
    CHECK(s.glob_strs("*.cpp") == std::vector<std::string>{"top.cpp"});
    CHECK(s.glob_strs("a/*.cpp") == std::vector<std::string>{"a/d0.cpp"});
    CHECK(s.glob_strs("a/*/*.cpp") == std::vector<std::string>{"a/b/d1.cpp"});
    CHECK(s.glob_strs("a/*/*/*.cpp") == std::vector<std::string>{"a/b/c/d2.cpp"});

    const std::vector<std::string> mixed {"a/*.cpp", "a/*/*.cpp"};
    auto r = detail::glob_multi(mixed);
    REQUIRE(r.has_value());
    std::vector<std::string> out;
    for (const auto& p : *r)
        out.push_back(p.generic_string());
    CHECK(out == std::vector<std::string>{"a/b/d1.cpp", "a/d0.cpp"});
}

TEST_CASE("glob_batch: shared walks, subsumption, exclude_dir, and symlinked subdirs") {
    ScratchDir s("glob-batch");
    s.touch("main.cpp");
    s.touch("src/root.cpp");
    s.touch("src/core/c.cpp");
    s.touch("src/core/deep/d.cpp");
    s.touch("src/cli/u.cpp");
    s.touch(".gen/g.cpp");
    s.touch("build/debug/ignored.cpp");
    s.touch("build/gen/wanted.cpp");
    s.touch("ext/from_link.cpp");
    std::filesystem::create_directory_symlink(
        std::filesystem::current_path() / "ext",
        std::filesystem::current_path() / "src/linked");

    const std::vector<std::vector<std::string>> groups {
        {"**.cpp"},            // 0: root walk skipping build/
        {"src/**.cpp"},        // 1: subsumed by 0
        {"src/core/*.cpp"},    // 2: shallow child subsumed by 0/1
        {".gen/*.cpp"},        // 3: hidden prefix must NOT be subsumed
        {"./src/cli/*.cpp"},   // 4: ./ spelling must NOT be subsumed
        {"build/gen/*.cpp"},   // 5: explicitly inside exclude_dir -> NOT excluded
        {"src/linked/*.cpp"},  // 6: crosses directory symlink -> NOT subsumed
    };
    auto r = detail::glob_batch(groups, std::filesystem::current_path() / "build");
    REQUIRE(r.has_value());
    REQUIRE(r->size() == groups.size());
    const auto to_strs = [](const std::vector<std::filesystem::path>& ps) {
        std::vector<std::string> out;
        for (const auto& p : ps)
            out.push_back(p.generic_string());
        return out;
    };
    CHECK(to_strs((*r)[0]) == std::vector<std::string>{
        "ext/from_link.cpp", "main.cpp", "src/cli/u.cpp",
        "src/core/c.cpp", "src/core/deep/d.cpp", "src/root.cpp",
    });
    CHECK(to_strs((*r)[1]) == std::vector<std::string>{
        "src/cli/u.cpp", "src/core/c.cpp", "src/core/deep/d.cpp", "src/root.cpp",
    });
    CHECK(to_strs((*r)[2]) == std::vector<std::string>{"src/core/c.cpp"});
    CHECK(to_strs((*r)[3]) == std::vector<std::string>{".gen/g.cpp"});
    CHECK(to_strs((*r)[4]) == std::vector<std::string>{"./src/cli/u.cpp"});
    CHECK(to_strs((*r)[5]) == std::vector<std::string>{"build/gen/wanted.cpp"});
    CHECK(to_strs((*r)[6]) == std::vector<std::string>{"src/linked/from_link.cpp"});
}

TEST_CASE("glob_batch: nested custom exclude_dir preserves sibling files") {
    ScratchDir s("glob-nested-excl");
    s.touch("out/keep.cpp");
    s.touch("out/build/skip.cpp");
    const std::vector<std::vector<std::string>> groups {{"**.cpp"}};
    auto r = detail::glob_batch(groups, "out/build");
    REQUIRE(r.has_value());
    REQUIRE(r->size() == 1);
    REQUIRE((*r)[0].size() == 1);
    CHECK((*r)[0][0].generic_string() == "out/keep.cpp");
}
