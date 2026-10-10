module;
#include <doctest/doctest.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <unistd.h>

module truss;
import :api;
import :impl;

using namespace truss;

namespace {

std::string scratch_write(const std::string& name, std::string_view body) {
    static int counter = 0;
    const auto p = std::filesystem::temp_directory_path() /
                   ("truss-modules-test-" + std::to_string(++counter) + "-" + name);
    auto w = detail::write_file(p, body);
    REQUIRE(w.has_value());
    return p.generic_string();
}

} // namespace

TEST_CASE("scan: primary interface with partition re-export") {
    const auto p = scratch_write("prim.cppm",
                                 "export module math;\n\nexport import :part;\n\nexport int f();\n");
    auto r = detail::scan_module_unit(p);
    REQUIRE(r.has_value());
    CHECK(r->module_name == "math");
    CHECK(r->imports == std::vector<std::string>{":part"});
}

TEST_CASE("scan: partition declaration") {
    const auto p = scratch_write("part.cppm", "export module math:part;\nexport int g();\n");
    auto r = detail::scan_module_unit(p);
    REQUIRE(r.has_value());
    CHECK(r->module_name == "math:part");
    CHECK(r->imports.empty());
}

TEST_CASE("scan: implementation unit and GMF") {
    const auto p = scratch_write("impl.cpp", "module;\n#include <cstdio>\n\nmodule math;\nimport :part;\n");
    auto r = detail::scan_module_unit(p);
    REQUIRE(r.has_value());
    CHECK(r->module_name == "math"); /* bare `module;` (GMF) must not match */
    CHECK(r->imports == std::vector<std::string>{":part"});
}

TEST_CASE("scan: plain TU has nothing") {
    const auto p = scratch_write("plain.cpp", "#include <cstdio>\nint main() {}\n");
    auto r = detail::scan_module_unit(p);
    REQUIRE(r.has_value());
    CHECK(r->module_name.empty());
    CHECK(r->imports.empty());
}

TEST_CASE("scan: comma-separated partition imports") {
    const auto p = scratch_write("multi.cppm", "export module m;\nimport :a, :b;\n");
    auto r = detail::scan_module_unit(p);
    REQUIRE(r.has_value());
    CHECK(r->imports == std::vector<std::string>{":a", ":b"});
}

TEST_CASE("scan: header imports and comments are ignored") {
    const auto p = scratch_write("hdr.cpp", "// module fake;\nimport \"widget.hpp\";\nimport real;\n");
    auto r = detail::scan_module_unit(p);
    REQUIRE(r.has_value());
    CHECK(r->module_name.empty());
    CHECK(r->imports == std::vector<std::string>{"real"});
}

TEST_CASE("resolve: importing a primary pulls its partitions") {
    const std::map<std::string, std::string, std::less<>> bmis {
        {"math", "/b/math.pcm"},
        {"math:part", "/b/math_part.pcm"},
    };
    detail::ModuleUnitInfo u;
    u.path = "app.cpp";
    u.imports = {"math"};
    auto r = detail::resolve_module_imports(u, bmis);
    REQUIRE(r.has_value());
    CHECK(r->first.find("-fmodule-file=math=/b/math.pcm") != std::string::npos);
    CHECK(r->first.find("-fmodule-file=math:part=/b/math_part.pcm") != std::string::npos);
    CHECK(r->second.size() == 2);
}

TEST_CASE("resolve: implementation unit gets implicit primary") {
    const std::map<std::string, std::string, std::less<>> bmis {
        {"math", "/b/math.pcm"},
        {"math:part", "/b/math_part.pcm"},
    };
    detail::ModuleUnitInfo u;
    u.path = "impl.cpp";
    u.module_name = "math"; /* implementation unit, not a BMI source */
    auto r = detail::resolve_module_imports(u, bmis);
    REQUIRE(r.has_value());
    CHECK(r->first.find("-fmodule-file=math=/b/math.pcm") != std::string::npos);
}

TEST_CASE("resolve: BMI source never consumes its own BMI") {
    const std::map<std::string, std::string, std::less<>> bmis {
        {"math", "/b/math.pcm"},
        {"math:part", "/b/math_part.pcm"},
    };
    detail::ModuleUnitInfo u;
    u.path = "math.cppm";
    u.module_name = "math";
    u.is_bmi_source = true;
    u.imports = {":part"};
    auto r = detail::resolve_module_imports(u, bmis);
    REQUIRE(r.has_value());
    CHECK(r->first == "-fmodule-file=math:part=/b/math_part.pcm");
    CHECK(r->second == std::vector<std::string>{"/b/math_part.pcm"});
}

TEST_CASE("resolve: import std is skipped, unknown import is a Config error") {
    const std::map<std::string, std::string, std::less<>> bmis {{"math", "/b/math.pcm"}};
    detail::ModuleUnitInfo u;
    u.path = "app.cpp";
    u.imports = {"std"};
    auto ok = detail::resolve_module_imports(u, bmis);
    REQUIRE(ok.has_value());
    CHECK(ok->first.empty());

    u.imports = {"nope"};
    auto bad = detail::resolve_module_imports(u, bmis);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().kind == Error::Kind::Config);
}

TEST_CASE("resolve: partition import without module declaration is an error") {
    const std::map<std::string, std::string, std::less<>> bmis {{"m:p", "/b/p.pcm"}};
    detail::ModuleUnitInfo u;
    u.path = "weird.cpp";
    u.imports = {":p"};
    auto r = detail::resolve_module_imports(u, bmis);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
}

TEST_CASE("scan-deps: authoritative closures replace the regex heuristic") {
    if (!detail::program_exists("clang-scan-deps")) {
        MESSAGE("clang-scan-deps not installed, skipping");
        return;
    }
    const auto prev = std::filesystem::current_path();
    const auto dir = std::filesystem::temp_directory_path() / "truss-scandeps-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    REQUIRE(chdir(dir.c_str()) == 0);

    REQUIRE(detail::write_file("math_part.cppm", "export module math:part;\nexport int p();\n").has_value());
    REQUIRE(detail::write_file("math.cppm", "export module math;\nexport import :part;\nexport int f();\n").has_value());
    REQUIRE(detail::write_file("math_impl.cpp", "module math;\nint f() { return p(); }\n").has_value());
    REQUIRE(detail::write_file("app.cpp",
                               "import \\\n  math;\n// import bogus; in a comment\nint main() { return f(); }\n").has_value());

    const auto scan = [&](const char* f) {
        auto r = detail::scan_deps_requires("clang++", "-std=c++23", f, dir, dir);
        REQUIRE(r.has_value());
        return *r;
    };
    CHECK(scan("math_part.cppm") == std::vector<std::string> {});
    CHECK(scan("math.cppm") == std::vector<std::string> {"math:part"});
    CHECK(scan("math_impl.cpp") == std::vector<std::string> {"math"});
    /* line-spliced import found; comment "import bogus;" correctly not */
    CHECK(scan("app.cpp") == std::vector<std::string> {"math"});

    chdir(prev.c_str());
    std::filesystem::remove_all(dir);
}
