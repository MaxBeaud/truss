module;
#include <doctest/doctest.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <sys/stat.h>
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

TEST_CASE("scan: raw strings, strings, char literals and comments are not code") {
    /* the embedded-TS pattern from real test files: `import ... from "..."` and
     * other module-looking lines inside literals must not be scanned */
    const auto p = scratch_write("embed.cpp", R"SRC(export module emb;
static const char* kTs = R"(import { Signal } from "@forge";
import { View } from "@forge/view";)";
static const char* kDelim = u8R"x(import u8raw; module u8raw;)x";
static const char* kPlain = "import plain; module plain;";
static const char* kEsc = "a\"; import escaped;";
static constexpr char kCh = 'i';
static constexpr int kNum = 1'000'000;
/* import blocked; */
// import linecomment;
import real;)SRC");
    auto r = detail::scan_module_unit(p);
    REQUIRE(r.has_value());
    CHECK(r->module_name == "emb");
    CHECK(r->imports == std::vector<std::string>{"real"});
}

TEST_CASE("scan: header-unit imports are recorded, never silently dropped") {
    const auto p = scratch_write("hdrunit.cpp",
                                 "module;\n#include <vector>\nexport module hu;\n"
                                 "import <string>;\nimport real;\n");
    auto r = detail::scan_module_unit(p);
    REQUIRE(r.has_value());
    CHECK(r->module_name == "hu");
    CHECK(r->imports == std::vector<std::string>{"real"});
    REQUIRE(r->header_imports.size() == 1);
    CHECK(r->header_imports.front() == "<string>");
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

TEST_CASE("resolve: an unresolved transitive import is a Config error naming it") {
    const std::map<std::string, std::string, std::less<>> bmis {
        {"A", "/b/a.pcm"}, {"B", "/b/b.pcm"},
    };
    const std::map<std::string, std::vector<std::string>, std::less<>> mi {
        {"A", {"B"}}, {"B", {"missing"}},
    };
    detail::ModuleUnitInfo u;
    u.path = "app.cpp";
    u.imports = {"A"};
    auto r = detail::resolve_module_imports(u, bmis, mi);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    /* the error names the module that actually has no BMI, and who imports it */
    CHECK(r.error().message.find("'missing'") != std::string::npos);
    CHECK(r.error().message.find("imported by 'B'") != std::string::npos);
}

TEST_CASE("resolve: a transitive std import does not fail the closure") {
    /* std is wired from the toolchain's std manifest, never from target BMIs */
    const std::map<std::string, std::string, std::less<>> bmis {{"A", "/b/a.pcm"}};
    const std::map<std::string, std::vector<std::string>, std::less<>> mi {{"A", {"std"}}};
    detail::ModuleUnitInfo u;
    u.path = "app.cpp";
    u.imports = {"A"};
    auto r = detail::resolve_module_imports(u, bmis, mi);
    REQUIRE(r.has_value());
    CHECK(r->first == "-fmodule-file=A=/b/a.pcm");
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
        return std::move(*r);
    };
    CHECK(scan("math_part.cppm").first == std::vector<std::string> {});
    CHECK(scan("math.cppm").first == std::vector<std::string> {"math:part"});
    CHECK(scan("math_impl.cpp").first == std::vector<std::string> {"math"});
    /* line-spliced import found; comment "import bogus;" correctly not */
    CHECK(scan("app.cpp").first == std::vector<std::string> {"math"});

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

TEST_CASE("modules: two targets exporting the same module name are a Config error") {
    ChainDir dir {"dup"};
    REQUIRE(detail::write_file("a.cppm", "export module dup;\nexport int a() { return 1; }\n").has_value());
    REQUIRE(detail::write_file("b.cppm", "export module dup;\nexport int b() { return 2; }\n").has_value());
    REQUIRE(detail::write_file("main.cpp", "int main() { return 0; }\n").has_value());
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("dup", 1, argv);
    proj.add_static_library("liba").sources("a.cppm");
    proj.add_static_library("libb").sources("b.cppm");
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(r.error().message.find("module 'dup'") != std::string::npos);
    CHECK(r.error().message.find("exactly one interface unit") != std::string::npos);
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

/* --- module-scan result cache -------------------------------------------- */

namespace {

std::filesystem::path fresh_cache_dir(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / ("truss-scan-cache-" + name);
    std::filesystem::remove_all(dir);
    return dir;
}

} // namespace

TEST_CASE("scan cache: a stored result is served back for an unchanged source") {
    const auto dir = fresh_cache_dir("roundtrip");
    const auto src = scratch_write("cached.cpp", "import math;\nint main() { return 0; }\n");
    const std::string key = detail::scan_cache_key("/proj", "clang++", "-std=c++23", src);

    detail::scan_cache_reset_stats();
    auto before = detail::scan_cache_lookup(dir, key, src);
    REQUIRE(before.has_value());
    CHECK_FALSE(before->has_value()); /* nothing stored yet */

    const std::vector<std::string> imports {"math"};
    const std::vector<std::string> headers {"<vector>"};
    REQUIRE(detail::scan_cache_store(dir, key, src, imports, headers).has_value());

    auto hit = detail::scan_cache_lookup(dir, key, src);
    REQUIRE(hit.has_value());
    REQUIRE(hit->has_value());
    CHECK((*hit)->first == imports);
    CHECK((*hit)->second == headers);
    CHECK(detail::scan_cache_stats().hot == 1);
    CHECK(detail::scan_cache_stats().cold == 1); /* counted by the store */
}

TEST_CASE("scan cache: a changed source (size) or a different key misses") {
    const auto dir = fresh_cache_dir("invalidate");
    const auto src = scratch_write("changed.cpp", "import a;\n");
    const std::string key = detail::scan_cache_key("/proj", "clang++", "-O0", src);
    const std::vector<std::string> imports {"a"};
    REQUIRE(detail::scan_cache_store(dir, key, src, imports, std::vector<std::string>{}).has_value());

    /* same key, different flags: a distinct entry */
    const std::string other = detail::scan_cache_key("/proj", "clang++", "-O2", src);
    CHECK(other != key);
    auto miss = detail::scan_cache_lookup(dir, other, src);
    REQUIRE(miss.has_value());
    CHECK_FALSE(miss->has_value());

    /* rewriting the source changes its size -> stale entry is not served */
    REQUIRE(detail::write_file(src, "import a;\nimport b;\n").has_value());
    auto changed = detail::scan_cache_lookup(dir, key, src);
    REQUIRE(changed.has_value());
    CHECK_FALSE(changed->has_value());
}

TEST_CASE("scan cache: a corrupt entry is a miss, never an error") {
    const auto dir = fresh_cache_dir("corrupt");
    const auto src = scratch_write("corrupt.cpp", "import a;\n");
    const std::string key = detail::scan_cache_key("/proj", "clang++", "", src);
    REQUIRE(detail::scan_cache_store(dir, key, src, std::vector<std::string>{"a"}, {}).has_value());

    const auto file = dir / (key + ".scan");
    auto text = detail::read_file(file);
    REQUIRE(text.has_value());
    REQUIRE(detail::write_file(file, text->substr(0, text->size() - 3)).has_value()); /* truncate */

    auto r = detail::scan_cache_lookup(dir, key, src);
    REQUIRE(r.has_value());
    CHECK_FALSE(r->has_value());
}

TEST_CASE("scan cache: the compiler identity is part of the key") {
    const auto key = detail::scan_cache_key("/proj", "clang++", "-std=c++23", "src/a.cpp");
    CHECK(key == detail::scan_cache_key("/proj", "clang++", "-std=c++23", "src/a.cpp"));
    CHECK(key != detail::scan_cache_key("/proj", "g++", "-std=c++23", "src/a.cpp"));
    CHECK(key != detail::scan_cache_key("/other", "clang++", "-std=c++23", "src/a.cpp"));
    CHECK(key != detail::scan_cache_key("/proj", "clang++", "-std=c++23", "src/b.cpp"));
    CHECK(key.size() == 64); /* lowercase hex sha256 */
}

TEST_CASE("scan: mentions_module_syntax is the raw-text pre-filter") {
    const auto plain = scratch_write("raw_plain.cpp", "#include <cstdio>\nint main() { return 0; }\n");
    auto a = detail::scan_module_unit(plain);
    REQUIRE(a.has_value());
    CHECK_FALSE(a->mentions_module_syntax);

    /* a keyword inside a comment still sets it: false positives only cost a
     * compiler scan, while false negatives would drop real imports */
    const auto commented = scratch_write("raw_comment.cpp", "// no import here\nint main() { return 0; }\n");
    auto b = detail::scan_module_unit(commented);
    REQUIRE(b.has_value());
    CHECK(b->mentions_module_syntax);
    CHECK(b->imports.empty());
}

TEST_CASE("scan cache: a second build of an unchanged tree rescans nothing") {
    if (!os::program_exists("clang-scan-deps")) { MESSAGE("clang-scan-deps not installed, skipping"); return; }
    ChainDir dir {"e2e-stats"};
    write_chain();
    char prog[] = "truss-tests";
    char dry[] = "-n";

    for (int run = 0; run < 2; ++run) {
        char* argv[] = {prog, dry};
        Project proj("chain-cache", 2, argv);
        proj.configure(Config().toolchain(Toolchain::LLVM));
        auto& l3 = proj.add_static_library("lib3").sources("c.cppm");
        auto& l2 = proj.add_static_library("lib2").sources("b.cppm").depends_on(l3);
        auto& l1 = proj.add_static_library("lib1").sources("a.cppm").depends_on(l2);
        proj.add_executable("app").sources("main.cpp").depends_on(l1);
        auto r = proj.build();
        INFO((r ? "ok" : r.error().message));
        REQUIRE(r.has_value());
        const auto s = detail::scan_cache_stats();
        if (run == 0) {
            CHECK(s.cold == 4); /* four units scanned on a cold tree */
            CHECK(s.hot == 0);
        } else {
            CHECK(s.hot == 4); /* everything served from build/debug/scan-cache */
            CHECK(s.cold == 0);
        }
    }
}

TEST_CASE("scan cache: TUs with no module syntax never reach the compiler scan") {
    ChainDir dir {"skip"};
    REQUIRE(detail::write_file("plain.cpp", "#include <cstdio>\nint main() { return 0; }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("skip-gate", 2, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));
    proj.add_executable("app").sources("plain.cpp");
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());
    const auto s = detail::scan_cache_stats();
    CHECK(s.skipped == 1);
    CHECK(s.cold == 0); /* clang-scan-deps never ran */
    CHECK(s.hot == 0);
    /* the cheap text scan is cached (so a no-op run does not even read the
     * file), but no compiler-scan entry exists: there was no scan */
    std::size_t text_entries = 0, scan_entries = 0;
    for (const auto& e : std::filesystem::directory_iterator("build/debug/scan-cache")) {
        if (e.path().extension() == ".text")
            ++text_entries;
        if (e.path().extension() == ".scan")
            ++scan_entries;
    }
    CHECK(text_entries == 1);
    CHECK(scan_entries == 0);
}

TEST_CASE("scan cache: a no-op build never even opens the sources") {
    ChainDir dir {"no-read"};
    REQUIRE(detail::write_file("plain.cpp", "#include <cstdio>\nint main() { return 0; }\n").has_value());
    const auto run_build = [&] {
        char prog[] = "truss-tests";
        char dry[] = "-n";
        char* argv[] = {prog, dry};
        Project proj("no-read", 2, argv);
        proj.configure(Config().toolchain(Toolchain::LLVM));
        proj.add_executable("app").sources("plain.cpp");
        return proj.build();
    };
    REQUIRE(run_build().has_value());
    REQUIRE(chmod("plain.cpp", 0) == 0); /* unreadable now */
    auto second = run_build();           /* served from the .text cache: no read */
    REQUIRE(chmod("plain.cpp", 0644) == 0);
    INFO((second ? "ok" : second.error().message));
    CHECK(second.has_value());
}

TEST_CASE("scan_module_unit: quoted header units, u8 char literals, and hex digit separators") {
    ChainDir dir {"lexer-edges"};
    REQUIRE(detail::write_file(
                "quoted_hdr.cpp",
                "import \"foo.h\";\n"
                "import <vector>;\n")
                .has_value());
    auto q = detail::scan_module_unit("quoted_hdr.cpp");
    REQUIRE(q.has_value());
    CHECK(q->header_imports == std::vector<std::string> {"\"foo.h\"", "<vector>"});

    REQUIRE(detail::write_file(
                "literals.cpp",
                "constexpr char8_t c = u8'a';\n"
                "constexpr unsigned x = 0xdead'beef;\n"
                "import math;\n")
                .has_value());
    auto lit = detail::scan_module_unit("literals.cpp");
    REQUIRE(lit.has_value());
    CHECK(lit->imports == std::vector<std::string> {"math"});
}

TEST_CASE("scan cache: corrupt huge timestamp is a safe miss (no UB)") {
    ChainDir dir {"corrupt-ts"};
    REQUIRE(detail::write_file("a.cpp", "import math;\n").has_value());
    const std::filesystem::path cdir = "cache";
    const std::string key = detail::scan_cache_key("/proj", "clang++", "-std=c++23", "a.cpp");
    REQUIRE(detail::write_file(
                cdir / (key + ".scan"),
                "truss-scan-cache 1\n999999999999999999999999999999 13\ni\tmath\n")
                .has_value());
    auto miss = detail::scan_cache_lookup(cdir, key, "a.cpp");
    REQUIRE(miss.has_value());
    CHECK_FALSE(miss->has_value());
}
