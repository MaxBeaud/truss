module;
#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("flags: MSVC returns a Toolchain error, never silent flags") {
    TargetTriple t = clang_triple();
    t.family = CompilerFamily::Msvc;
    const std::array<std::string, 0> none {};
    auto r = detail::build_flags(t, BuildMode::Debug, CppStandard::Default, Warnings::Default,
                                 DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                 Lto::Default, false, none, none, "");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Toolchain);
}

TEST_CASE("flags: link flags order -L before -l") {
    const auto t = clang_triple();
    const std::array<std::string, 1> paths {"build/lib"};
    const std::array<std::string, 1> libs {"foo"};
    const std::array<std::string, 0> none {};
    auto r = detail::link_flags(t, Sanitizer::Default, Lto::Default, paths, libs, none, none, "-lm");
    REQUIRE(r.has_value());
    CHECK(contains(*r, "-L\"build/lib\""));
    CHECK(contains(*r, "-lfoo"));
    CHECK(r->find("-L") < r->find("-lfoo"));
    CHECK(contains(*r, "-lm"));
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
