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
    auto r = detail::resolve_module_imports(u, bmis, {});
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
    auto r = detail::resolve_module_imports(u, bmis, {});
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
    auto r = detail::resolve_module_imports(u, bmis, {});
    REQUIRE(r.has_value());
    CHECK(r->first == "-fmodule-file=math:part=/b/math_part.pcm");
    CHECK(r->second == std::vector<std::string>{"/b/math_part.pcm"});
}

TEST_CASE("resolve: import std resolves via the bmis map, unknown import is a Config error") {
    const std::map<std::string, std::string, std::less<>> bmis {{"math", "/b/math.pcm"}, {"std", "/b/std.pcm"}};
    detail::ModuleUnitInfo u;
    u.path = "app.cpp";
    u.imports = {"std"};
    auto ok = detail::resolve_module_imports(u, bmis, {});
    REQUIRE(ok.has_value());
    CHECK(ok->first == "-fmodule-file=std=/b/std.pcm");
    CHECK(ok->second == std::vector<std::string>{"/b/std.pcm"});

    u.imports = {"nope"};
    auto bad = detail::resolve_module_imports(u, bmis, {});
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().kind == Error::Kind::Config);
}

TEST_CASE("resolve: partition import without module declaration is an error") {
    const std::map<std::string, std::string, std::less<>> bmis {{"m:p", "/b/p.pcm"}};
    detail::ModuleUnitInfo u;
    u.path = "weird.cpp";
    u.imports = {":p"};
    auto r = detail::resolve_module_imports(u, bmis, {});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
}

TEST_CASE("scan-deps: authoritative closures replace the regex heuristic") {
    if (!os::program_exists("clang-scan-deps")) {
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

TEST_CASE("resolve: transitive closure wires indirect imports once (export import and plain import)") {
    const std::map<std::string, std::string, std::less<>> bmis {
        {"A", "/b/a.pcm"}, {"B", "/b/b.pcm"}, {"C", "/b/c.pcm"}, {"D", "/b/d.pcm"},
        {"C:p", "/b/c_p.pcm"},
    };
    /* A -> B -> C (C has partition p, which imports D); a cycle D -> A must terminate */
    const std::map<std::string, std::vector<std::string>, std::less<>> mi {
        {"A", {"B"}}, {"B", {"C"}}, {"C", {":p"}}, {"C:p", {"D"}}, {"D", {"A"}},
    };
    detail::ModuleUnitInfo u;
    u.path = "app.cpp";
    u.imports = {"A"};
    auto r = detail::resolve_module_imports(u, bmis, mi);
    REQUIRE(r.has_value());
    for (const char* m : {"A=", "B=", "C=", "C:p=", "D="})
        CHECK(r->first.find(std::string("-fmodule-file=") + m) != std::string::npos);
    CHECK(r->second.size() == 5); /* partition not double-wired */
}

TEST_CASE("resolve: std.compat pulls std through the module_imports closure") {
    const std::map<std::string, std::string, std::less<>> bmis {{"std", "/b/std.pcm"}, {"std.compat", "/b/sc.pcm"}};
    const std::map<std::string, std::vector<std::string>, std::less<>> mi {{"std", {}}, {"std.compat", {"std"}}};
    detail::ModuleUnitInfo u;
    u.path = "app.cpp";
    u.imports = {"std.compat"};
    auto r = detail::resolve_module_imports(u, bmis, mi);
    REQUIRE(r.has_value());
    CHECK(r->second.size() == 2);
    CHECK(r->first.find("-fmodule-file=std=/b/std.pcm") != std::string::npos);
}

namespace {

/* lib3: module C; lib2: B (export import C); lib1: A (plain import B, so C is only reachable
 * through B); exe depends only on lib1 and imports A. */
void write_chain() {
    REQUIRE(detail::write_file("c.cppm", "export module C;\nexport int c() { return 1; }\n").has_value());
    REQUIRE(detail::write_file("b.cppm",
                               "export module B;\nexport import C;\nexport int b() { return c() + 10; }\n")
                .has_value());
    REQUIRE(detail::write_file("a.cppm",
                               "export module A;\nimport B;\nexport int a() { return b() + 100; }\n")
                .has_value());
    REQUIRE(detail::write_file("main.cpp", "import A;\nint main() { return a() == 111 ? 0 : 1; }\n").has_value());
}

struct ChainDir {
    std::filesystem::path prev = std::filesystem::current_path();
    explicit ChainDir(const std::string& name) {
        const auto dir = std::filesystem::temp_directory_path() / ("truss-chain-test-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~ChainDir() { chdir(prev.c_str()); }
};

} // namespace

TEST_CASE("modules: dry-run threads the full transitive BMI closure to a consumer of lib1 only") {
    ChainDir dir {"dry"};
    write_chain();
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("chain-dry", 2, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));
    auto& l3 = proj.add_static_library("lib3").sources("c.cppm");
    auto& l2 = proj.add_static_library("lib2").sources("b.cppm").depends_on(l3);
    auto& l1 = proj.add_static_library("lib1").sources("a.cppm").depends_on(l2);
    proj.add_executable("app").sources("main.cpp").depends_on(l1);
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    auto m = detail::read_file("build/debug/ninja/build.ninja");
    REQUIRE(m.has_value());
    const auto pos = m->find("main.cpp");
    REQUIRE(pos != std::string::npos);
    const auto eol = m->find('\n', m->find("flags", pos));
    const std::string_view edge(m->data() + pos, eol - pos);
    for (const char* mod : {"-fmodule-file=A=", "-fmodule-file=B=", "-fmodule-file=C="})
        CHECK_MESSAGE(edge.find(mod) != std::string_view::npos, mod);
}

TEST_CASE("modules: real build and run through a three-library module chain") {
    if (!os::program_exists("clang++")) { MESSAGE("clang++ not installed, skipping"); return; }
    ChainDir dir {"e2e"};
    write_chain();
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("chain-e2e", 1, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));
    auto& l3 = proj.add_static_library("lib3").sources("c.cppm");
    auto& l2 = proj.add_static_library("lib2").sources("b.cppm").depends_on(l3);
    auto& l1 = proj.add_static_library("lib1").sources("a.cppm").depends_on(l2);
    proj.add_executable("app").sources("main.cpp").depends_on(l1);
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const auto bin = std::filesystem::current_path() / "build/debug/bin" /
                     detail::triple_dir(*detail::apply_config(Config().toolchain(Toolchain::LLVM), host_triple()));
    auto run = os::run({(bin / "app").string()});
    REQUIRE(run.has_value());
    CHECK(run->exit_code == 0);
}
