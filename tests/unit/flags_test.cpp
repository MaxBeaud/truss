module;
#include <doctest/doctest.h>

#include <array>
#include <string>

module truss;
import :api;
import :impl;

using namespace truss;

namespace {

TargetTriple clang_triple() {
    TargetTriple t;
    t.os = Os::Linux;
    t.arch = Arch::X64;
    t.family = CompilerFamily::Clang;
    t.compiler = "clang++";
    t.ar = "llvm-ar";
    return t;
}

bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

} // namespace

TEST_CASE("flags: defaults per mode") {
    const auto t = clang_triple();
    const std::array<std::string, 0> none {};

    auto dbg = detail::build_flags(t, BuildMode::Debug, CppStandard::Default, Warnings::Default,
                                   DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                   Lto::Default, false, none, none, "");
    REQUIRE(dbg.has_value());
    CHECK(contains(dbg->compile, "-std=c++23"));
    CHECK(contains(dbg->compile, "-g3"));
    CHECK(contains(dbg->compile, "-O0"));
    CHECK_FALSE(contains(dbg->compile, "NDEBUG"));

    auto rel = detail::build_flags(t, BuildMode::Release, CppStandard::Default, Warnings::Default,
                                   DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                   Lto::Default, false, none, none, "");
    REQUIRE(rel.has_value());
    CHECK(contains(rel->compile, "-O2"));
    CHECK(contains(rel->compile, "-DNDEBUG"));
}

TEST_CASE("flags: explicit options win over mode defaults") {
    const auto t = clang_triple();
    const std::array<std::string, 0> none {};
    auto r = detail::build_flags(t, BuildMode::Debug, CppStandard::Cpp26, Warnings::All,
                                 DebugInfo::None, Optimization::O3, Sanitizer::Address,
                                 Lto::Thin, true, none, none, "-Wextra-x");
    REQUIRE(r.has_value());
    CHECK(contains(r->compile, "-std=c++26"));
    CHECK(contains(r->compile, "-Wall -Wextra"));
    CHECK(contains(r->compile, "-g0"));
    CHECK(contains(r->compile, "-O3"));
    CHECK(contains(r->compile, "-fsanitize=address"));
    CHECK(contains(r->compile, "-flto=thin"));
    CHECK(contains(r->compile, "-fPIC"));
    CHECK(contains(r->compile, "-Wextra-x"));
}

TEST_CASE("flags: defines and includes") {
    const auto t = clang_triple();
    const std::array<std::string, 2> defs {"FOO=1", "BAR"};
    const std::array<std::string, 1> incs {"include"};
    auto r = detail::build_flags(t, BuildMode::Debug, CppStandard::Default, Warnings::Default,
                                 DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                 Lto::Default, false, defs, incs, "");
    REQUIRE(r.has_value());
    CHECK(contains(r->compile, "-DFOO=1"));
    CHECK(contains(r->compile, "-DBAR"));
    CHECK(contains(r->compile, "-I\"include\""));
}

TEST_CASE("flags: link flags order -L before -l") {
    const auto t = clang_triple();
    const std::array<std::string, 1> paths {"build/lib"};
    const std::array<std::string, 1> libs {"foo"};
    const std::array<std::string, 0> none {};
    auto r = detail::link_flags(t, BuildMode::Debug, DebugInfo::Default, Sanitizer::Default,
                                Lto::Default, paths, libs, none, none, "-lm");
    REQUIRE(r.has_value());
    CHECK(r->driver.empty());
    CHECK(contains(r->linker, "-L\"build/lib\""));
    CHECK(contains(r->linker, "-lfoo"));
    CHECK(r->linker.find("-L") < r->linker.find("-lfoo"));
    CHECK(contains(r->linker, "-lm"));
}

TEST_CASE("flags: sanitizer/lto link flags are driver-side") {
    const auto t = clang_triple();
    const std::array<std::string, 0> none {};
    auto r = detail::link_flags(t, BuildMode::Debug, DebugInfo::Default, Sanitizer::Address,
                                Lto::Thin, none, none, none, none, "");
    REQUIRE(r.has_value());
    CHECK(contains(r->driver, "-fsanitize=address"));
    CHECK(contains(r->driver, "-flto=thin"));
    CHECK(r->linker.empty());
}

TEST_CASE("flags: MSVC compile mapping (cl.exe / clang-cl syntax)") {
    TargetTriple t = clang_triple();
    t.family = CompilerFamily::Msvc;
    t.compiler = "clang-cl";
    const std::array<std::string, 2> defs {"FOO=1", "BAR"};
    const std::array<std::string, 1> incs {"include"};
    auto r = detail::build_flags(t, BuildMode::Debug, CppStandard::Default, Warnings::All,
                                 DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                 Lto::Default, true, defs, incs, "");
    REQUIRE(r.has_value());
    CHECK(contains(r->compile, "/std:c++latest")); /* truss default (C++23) */
    CHECK(contains(r->compile, "/W4"));
    CHECK(contains(r->compile, "/Z7"));            /* debug info in .obj, no PDB race */
    CHECK(contains(r->compile, "/Od"));
    CHECK(contains(r->compile, "/utf-8 /EHsc /nologo"));
    CHECK(contains(r->compile, "/DFOO=1"));
    CHECK(contains(r->compile, "/Iinclude"));
    CHECK_FALSE(contains(r->compile, "-fPIC"));    /* no PIC on Windows */
    CHECK_FALSE(contains(r->compile, "-std="));
    CHECK(contains(r->compile_c, "/std:c17"));
    CHECK_FALSE(contains(r->compile_c, "/std:c++"));
}

TEST_CASE("flags: MSVC release defaults and option mapping") {
    TargetTriple t = clang_triple();
    t.family = CompilerFamily::Msvc;
    const std::array<std::string, 0> none {};
    auto rel = detail::build_flags(t, BuildMode::Release, CppStandard::Cpp11, Warnings::Pedantic,
                                   DebugInfo::None, Optimization::O3, Sanitizer::Address,
                                   Lto::Thin, false, none, none, "");
    REQUIRE(rel.has_value());
    CHECK(contains(rel->compile, "/std:c++14"));   /* no C++11 mode: nearest is C++14 */
    CHECK(contains(rel->compile, "/W4"));
    CHECK(contains(rel->compile, "/O2"));          /* O3 maps to O2 */
    CHECK(contains(rel->compile, "/fsanitize=address"));
    CHECK(contains(rel->compile, "/GL"));          /* ThinLTO maps to /GL */
    CHECK(contains(rel->compile, "/DNDEBUG"));
    CHECK_FALSE(contains(rel->compile, "/Z7"));    /* DebugInfo::None */
}

TEST_CASE("flags: MSVC link flags split driver-side vs after /link") {
    TargetTriple t = clang_triple();
    t.family = CompilerFamily::Msvc;
    const std::array<std::string, 1> paths {"build/lib"};
    const std::array<std::string, 1> libs {"foo"};
    const std::array<std::string, 0> none {};
    auto r = detail::link_flags(t, BuildMode::Debug, DebugInfo::Full, Sanitizer::Address,
                                Lto::Full, paths, libs, none, none, "/SUBSYSTEM:CONSOLE");
    REQUIRE(r.has_value());
    CHECK(contains(r->driver, "/fsanitize=address"));
    CHECK(contains(r->linker, "/INCREMENTAL:NO"));
    CHECK(contains(r->linker, "/LTCG"));
    CHECK(contains(r->linker, "/DEBUG"));
    CHECK(contains(r->linker, "/LIBPATH:build/lib"));
    CHECK(contains(r->linker, "foo.lib"));
    CHECK_FALSE(contains(r->linker, "-lfoo"));
    CHECK(contains(r->linker, "/SUBSYSTEM:CONSOLE"));
}

TEST_CASE("flags: MSVC rejects Apple frameworks") {
    TargetTriple t = clang_triple();
    t.family = CompilerFamily::Msvc;
    const std::array<std::string, 1> fws {"CoreFoundation"};
    const std::array<std::string, 0> none {};
    auto r = detail::link_flags(t, BuildMode::Debug, DebugInfo::Default, Sanitizer::Default,
                                Lto::Default, none, none, fws, none, "");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
}

TEST_CASE("flags: C variant uses gnu17, never a C++ -std") {
    const auto t = clang_triple();
    const std::array<std::string, 1> incs {"include"};
    auto r = detail::build_flags(t, BuildMode::Debug, CppStandard::Cpp26, Warnings::Default,
                                 DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                 Lto::Default, true, std::array<std::string, 0> {}, incs, "");
    REQUIRE(r.has_value());
    CHECK(contains(r->compile, "-std=c++26"));
    CHECK(contains(r->compile_c, "-std=gnu17"));
    CHECK_FALSE(contains(r->compile_c, "-std=c++"));
    /* shared flags still apply to the C variant */
    CHECK(contains(r->compile_c, "-fPIC"));
    CHECK(contains(r->compile_c, "-I\"include\""));
}
