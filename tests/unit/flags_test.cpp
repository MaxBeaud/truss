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
                                   Lto::Default, Hardening::None, false, none, none, "");
    REQUIRE(dbg.has_value());
    CHECK(contains(dbg->compile, "-std=c++23"));
    CHECK(contains(dbg->compile, "-g3"));
    CHECK(contains(dbg->compile, "-O0"));
    CHECK_FALSE(contains(dbg->compile, "NDEBUG"));

    auto rel = detail::build_flags(t, BuildMode::Release, CppStandard::Default, Warnings::Default,
                                   DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                   Lto::Default, Hardening::None, false, none, none, "");
    REQUIRE(rel.has_value());
    CHECK(contains(rel->compile, "-O2"));
    CHECK(contains(rel->compile, "-DNDEBUG"));
}

TEST_CASE("flags: explicit options win over mode defaults") {
    const auto t = clang_triple();
    const std::array<std::string, 0> none {};
    auto r = detail::build_flags(t, BuildMode::Debug, CppStandard::Cpp26, Warnings::All,
                                 DebugInfo::None, Optimization::O3, Sanitizer::Address,
                                 Lto::Thin, Hardening::None, true, none, none, "-Wextra-x");
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
                                 Lto::Default, Hardening::None, false, defs, incs, "");
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
                                Lto::Default, Hardening::None, paths, libs, none, none, "-lm");
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
                                Lto::Thin, Hardening::None, none, none, none, none, "");
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
                                 Lto::Default, Hardening::None, true, defs, incs, "");
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
                                   Lto::Thin, Hardening::None, false, none, none, "");
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
                                Lto::Full, Hardening::None, paths, libs, none, none, "/SUBSYSTEM:CONSOLE");
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
                                Lto::Default, Hardening::None, none, none, fws, none, "");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
}

TEST_CASE("flags: C variant uses gnu17, never a C++ -std") {
    const auto t = clang_triple();
    const std::array<std::string, 1> incs {"include"};
    auto r = detail::build_flags(t, BuildMode::Debug, CppStandard::Cpp26, Warnings::Default,
                                 DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                 Lto::Default, Hardening::None, true, std::array<std::string, 0> {}, incs, "");
    REQUIRE(r.has_value());
    CHECK(contains(r->compile, "-std=c++26"));
    CHECK(contains(r->compile_c, "-std=gnu17"));
    CHECK_FALSE(contains(r->compile_c, "-std=c++"));
    /* shared flags still apply to the C variant */
    CHECK(contains(r->compile_c, "-fPIC"));
    CHECK(contains(r->compile_c, "-I\"include\""));
}

TEST_CASE("hardening: Default resolves to the Light stack") {
    const auto t = clang_triple();
    const std::array<std::string, 0> none {};
    auto c = detail::build_flags(t, BuildMode::Debug, CppStandard::Default, Warnings::Default,
                                 DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                 Lto::Default, Hardening::Default, false, none, none, "");
    REQUIRE(c.has_value());
    CHECK(contains(c->compile, "-fstack-protector-strong"));
    CHECK_FALSE(contains(c->compile, "-ftrivial-auto-var-init"));
    auto l = detail::link_flags(t, BuildMode::Debug, DebugInfo::Default, Sanitizer::Default,
                                Lto::Default, Hardening::Default, none, none, none, none, "");
    REQUIRE(l.has_value());
    CHECK(contains(l->driver, "-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,--as-needed"));
}

TEST_CASE("hardening: None opts out entirely") {
    const auto t = clang_triple();
    const std::array<std::string, 0> none {};
    auto c = detail::build_flags(t, BuildMode::Release, CppStandard::Default, Warnings::Default,
                                 DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                 Lto::Default, Hardening::None, false, none, none, "");
    REQUIRE(c.has_value());
    CHECK_FALSE(contains(c->compile, "-fstack-protector"));
    auto l = detail::link_flags(t, BuildMode::Release, DebugInfo::Default, Sanitizer::Default,
                                Lto::Default, Hardening::None, none, none, none, none, "");
    REQUIRE(l.has_value());
    CHECK_FALSE(contains(l->driver, "-Wl,-z"));
}

TEST_CASE("hardening: Medium — var-init, x86-64-gated CFI, libc++ hardened mode") {
    const auto t = clang_triple();
    const std::array<std::string, 0> none {};
    auto c = detail::build_flags(t, BuildMode::Release, CppStandard::Default, Warnings::Default,
                                 DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                 Lto::Default, Hardening::Medium, false, none, none, "");
    REQUIRE(c.has_value());
    CHECK(contains(c->compile, "-fstack-protector-strong"));
    CHECK(contains(c->compile, "-ftrivial-auto-var-init=pattern"));
    CHECK(contains(c->compile, "-fcf-protection=full -fstack-clash-protection"));
    CHECK(contains(c->compile, "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_HARDENED"));
    CHECK_FALSE(contains(c->compile, "_GLIBCXX_ASSERTIONS"));
    /* stdlib defines are C++-only; the C variant gets the codegen flags */
    CHECK_FALSE(contains(c->compile_c, "_LIBCPP_HARDENING_MODE"));
    CHECK(contains(c->compile_c, "-ftrivial-auto-var-init=pattern"));

    /* non-x86-64: clang rejects cf-protection/stack-clash — must be gated off */
    TargetTriple arm = clang_triple();
    arm.arch = Arch::Arm64;
    auto ca = detail::build_flags(arm, BuildMode::Release, CppStandard::Default, Warnings::Default,
                                  DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                  Lto::Default, Hardening::Medium, false, none, none, "");
    REQUIRE(ca.has_value());
    CHECK(contains(ca->compile, "-ftrivial-auto-var-init=pattern"));
    CHECK_FALSE(contains(ca->compile, "-fcf-protection"));
    CHECK_FALSE(contains(ca->compile, "-fstack-clash-protection"));
}

TEST_CASE("hardening: Max — libc++ debug mode + libstdc++ assertions") {
    const auto t = clang_triple();
    const std::array<std::string, 0> none {};
    auto c = detail::build_flags(t, BuildMode::Release, CppStandard::Default, Warnings::Default,
                                 DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                 Lto::Default, Hardening::Max, false, none, none, "");
    REQUIRE(c.has_value());
    CHECK(contains(c->compile, "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG"));
    CHECK(contains(c->compile, "-D_GLIBCXX_ASSERTIONS"));
    CHECK(contains(c->compile, "-ftrivial-auto-var-init=pattern"));
}

TEST_CASE("hardening: MSVC mapping — tiers + x86-64 gating") {
    TargetTriple t = clang_triple();
    t.family = CompilerFamily::Msvc;
    const std::array<std::string, 0> none {};

    auto light = detail::build_flags(t, BuildMode::Release, CppStandard::Default, Warnings::Default,
                                     DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                     Lto::Default, Hardening::Light, false, none, none, "");
    auto light_l = detail::link_flags(t, BuildMode::Release, DebugInfo::Default, Sanitizer::Default,
                                      Lto::Default, Hardening::Light, none, none, none, none, "");
    REQUIRE(light.has_value());
    REQUIRE(light_l.has_value());
    CHECK_FALSE(contains(light->compile, "/guard:cf"));
    CHECK(contains(light_l->linker, "/NXCOMPAT /DYNAMICBASE"));
    CHECK_FALSE(contains(light_l->linker, "/guard:cf"));

    auto max = detail::build_flags(t, BuildMode::Release, CppStandard::Default, Warnings::Default,
                                   DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                   Lto::Default, Hardening::Max, false, none, none, "");
    auto max_l = detail::link_flags(t, BuildMode::Release, DebugInfo::Default, Sanitizer::Default,
                                    Lto::Default, Hardening::Max, none, none, none, none, "");
    REQUIRE(max.has_value());
    REQUIRE(max_l.has_value());
    CHECK(contains(max->compile, "/guard:cf"));
    CHECK(contains(max->compile, "/sdl"));
    CHECK(contains(max_l->linker, "/NXCOMPAT /DYNAMICBASE"));
    CHECK(contains(max_l->linker, "/guard:cf"));
    CHECK(contains(max_l->linker, "/CETCOMPAT"));

    /* non-x86-64: /guard:cf and /CETCOMPAT are x86-64; base + /sdl stay */
    TargetTriple arm = clang_triple();
    arm.family = CompilerFamily::Msvc;
    arm.arch = Arch::Arm64;
    auto arm_c = detail::build_flags(arm, BuildMode::Release, CppStandard::Default, Warnings::Default,
                                     DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                     Lto::Default, Hardening::Max, false, none, none, "");
    auto arm_l = detail::link_flags(arm, BuildMode::Release, DebugInfo::Default, Sanitizer::Default,
                                    Lto::Default, Hardening::Max, none, none, none, none, "");
    REQUIRE(arm_c.has_value());
    REQUIRE(arm_l.has_value());
    CHECK_FALSE(contains(arm_c->compile, "/guard:cf"));
    CHECK(contains(arm_c->compile, "/sdl"));
    CHECK(contains(arm_l->linker, "/NXCOMPAT /DYNAMICBASE"));
    CHECK_FALSE(contains(arm_l->linker, "/guard:cf"));
    CHECK_FALSE(contains(arm_l->linker, "/CETCOMPAT"));
}
