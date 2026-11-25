// Unit tests for Target::tag() and `./truss test --tag ...` filtering.
module;
#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

module truss;
import :api;
import :impl;

using namespace truss;

namespace {

bool tt_contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

struct TtScratchDir {
    std::filesystem::path prev;
    std::filesystem::path dir;
    explicit TtScratchDir(const std::string& name) {
        prev = std::filesystem::current_path();
        dir = std::filesystem::temp_directory_path() / ("truss-test-tags-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~TtScratchDir() { chdir(prev.c_str()); }
};

void write_marker_test(const std::string& src_path, const std::string& marker_file) {
    const std::string code =
        "#include <cstdio>\n"
        "int main() {\n"
        "    if (FILE* f = std::fopen(\"" + marker_file + "\", \"w\")) { std::fputs(\"ran\", f); std::fclose(f); }\n"
        "    return 0;\n"
        "}\n";
    REQUIRE(detail::write_file(src_path, code).has_value());
}

} // namespace

TEST_CASE("test_tags: non-test target calling tag() produces Config error") {
    TtScratchDir dir {"non-test-err"};
    REQUIRE(detail::write_file("main.cpp", "int main() { return 0; }\n").has_value());

    std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n")};
    Project proj {"app", argv};
    proj.add_executable("app").sources("main.cpp").tag("unit");

    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(tt_contains(r.error().message, "tag()"));
}

TEST_CASE("test_tags: positive tags, !exclusions, combined include+exclude, repeatable --tag") {
    TtScratchDir dir {"filter"};
    write_marker_test("t_unit.cpp", "ran_unit.txt");
    write_marker_test("t_fast_integ.cpp", "ran_fast_integ.txt");
    write_marker_test("t_slow.cpp", "ran_slow.txt");

    auto setup_targets = [](Project& proj) {
        std::vector<std::string> extra_tags {"smoke"};
        proj.add_executable("t_unit").sources("t_unit.cpp").is_test().tag("unit").tag(extra_tags);
        proj.add_executable("t_fast_integ").sources("t_fast_integ.cpp").is_test().tag({"fast", "integration"});
        proj.add_executable("t_slow").sources("t_slow.cpp").is_test().tag({"unit", "slow"});
    };

    SUBCASE("positive tags include matching any") {
        std::vector<char*> argv {
            const_cast<char*>("truss"),
            const_cast<char*>("test"),
            const_cast<char*>("--tag"),
            const_cast<char*>("smoke"),
            const_cast<char*>("fast"),
        };
        Project proj {"app", argv};
        setup_targets(proj);
        REQUIRE(proj.build().has_value());

        CHECK(std::filesystem::exists("ran_unit.txt"));
        CHECK(std::filesystem::exists("ran_fast_integ.txt"));
        CHECK_FALSE(std::filesystem::exists("ran_slow.txt"));
    }

    SUBCASE("negated !tags exclude matching any") {
        std::vector<char*> argv {
            const_cast<char*>("truss"),
            const_cast<char*>("test"),
            const_cast<char*>("--tag"),
            const_cast<char*>("!slow"),
            const_cast<char*>("!integration"),
        };
        Project proj {"app", argv};
        setup_targets(proj);
        REQUIRE(proj.build().has_value());

        CHECK(std::filesystem::exists("ran_unit.txt"));
        CHECK_FALSE(std::filesystem::exists("ran_fast_integ.txt"));
        CHECK_FALSE(std::filesystem::exists("ran_slow.txt"));
    }

    SUBCASE("combined include + exclude with repeatable --tag") {
        std::vector<char*> argv {
            const_cast<char*>("truss"),
            const_cast<char*>("test"),
            const_cast<char*>("--tag"),
            const_cast<char*>("unit"),
            const_cast<char*>("--tag"),
            const_cast<char*>("!slow"),
        };
        Project proj {"app", argv};
        setup_targets(proj);
        REQUIRE(proj.build().has_value());

        CHECK(std::filesystem::exists("ran_unit.txt"));
        CHECK_FALSE(std::filesystem::exists("ran_fast_integ.txt"));
        CHECK_FALSE(std::filesystem::exists("ran_slow.txt"));
    }
}
