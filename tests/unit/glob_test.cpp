module;
#include <catch2/catch_test_macros.hpp>

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
