module;
#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <string>

#include <unistd.h>

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

/* chdir RAII for the dry-run manifest tests */
struct ScratchDir {
    std::filesystem::path prev;
    explicit ScratchDir(const std::string& name) {
        prev = std::filesystem::current_path();
        const auto dir = std::filesystem::temp_directory_path() / ("truss-flags-test-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~ScratchDir() { chdir(prev.c_str()); }
};

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

TEST_CASE("flags: Warnings::Default is -Wall -Wextra (as documented)") {
    const auto t = clang_triple();
    const std::array<std::string, 0> none {};
    auto r = detail::build_flags(t, BuildMode::Debug, CppStandard::Default, Warnings::Default,
                                 DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                 Lto::Default, Hardening::None, false, none, none, "");
    REQUIRE(r.has_value());
    CHECK(contains(r->compile, "-Wall -Wextra"));
    CHECK_FALSE(contains(r->compile, "-Wpedantic"));
    CHECK_FALSE(contains(r->compile, "-w"));
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

TEST_CASE("flags: system includes become -isystem after -I") {
    const auto t = clang_triple();
    const std::array<std::string, 0> none {};
    const std::array<std::string, 1> incs {"include"};
    const std::array<std::string, 1> sys {"third/include"};
    auto r = detail::build_flags(t, BuildMode::Debug, CppStandard::Default, Warnings::Default,
                                 DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                 Lto::Default, Hardening::None, false, none, incs, "", sys);
    REQUIRE(r.has_value());
    const auto i = r->compile.find("-I\"include\"");
    const auto s = r->compile.find("-isystem \"third/include\"");
    REQUIRE(i != std::string::npos);
    REQUIRE(s != std::string::npos);
    CHECK(i < s);
    CHECK(contains(r->compile_c, "-isystem \"third/include\""));
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
    CHECK(contains(r->compile, "/I\"include\""));
    CHECK_FALSE(contains(r->compile, "-fPIC"));    /* no PIC on Windows */    CHECK_FALSE(contains(r->compile, "-std="));
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
    CHECK(contains(r->linker, "/LIBPATH:\"build/lib\""));
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

TEST_CASE("config: -stdlib= emitted on compile and link when selected") {
    auto t = clang_triple();
    t.stdlib = StdLib::Libcxx;
    const std::array<std::string, 0> none {};
    auto c = detail::build_flags(t, BuildMode::Debug, CppStandard::Default, Warnings::Default,
                                 DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                 Lto::Default, Hardening::None, false, none, none, "");
    REQUIRE(c.has_value());
    CHECK(contains(c->compile, "-stdlib=libc++"));
    CHECK(contains(c->compile_c, "-stdlib=libc++"));
    auto l = detail::link_flags(t, BuildMode::Debug, DebugInfo::Default, Sanitizer::Default,
                                Lto::Default, Hardening::None, none, none, none, none, "");
    REQUIRE(l.has_value());
    CHECK(contains(l->driver, "-stdlib=libc++"));

    t.stdlib = StdLib::Libstdcxx;
    auto l2 = detail::link_flags(t, BuildMode::Debug, DebugInfo::Default, Sanitizer::Default,
                                 Lto::Default, Hardening::None, none, none, none, none, "");
    REQUIRE(l2.has_value());
    CHECK(contains(l2->driver, "-stdlib=libstdc++"));
}

TEST_CASE("flags: include paths are quote-escaped for the shell") {
    const auto t = clang_triple();
    const std::array<std::string, 0> none {};
    const std::array<std::string, 2> incs {"plain/include", "odd/$(touch pwned)/`id`"};
    auto r = detail::build_flags(t, BuildMode::Debug, CppStandard::Default, Warnings::Default,
                                 DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                 Lto::Default, Hardening::None, false, none, incs, "");
    REQUIRE(r.has_value());
    CHECK(contains(r->compile, "-I\"plain/include\""));
    /* $ and ` stay literal inside double quotes only when backslash-escaped */
    const auto at = r->compile.find("-I\"odd/");
    REQUIRE(at != std::string::npos);
    const auto quoted = r->compile.substr(at, r->compile.find('"', at + 3) - at + 1);
    CHECK(quoted.find("\\$(") != std::string::npos);
    CHECK(quoted.find("\\`id\\`") != std::string::npos);
    const bool no_bare_dollar_paren = quoted.find("\"$(") == std::string::npos;
    CHECK(no_bare_dollar_paren); /* every $( is escaped, so no substitution happens */
}

TEST_CASE("flags: library and framework names are validated, not interpolated") {
    const auto t = clang_triple();
    const std::array<std::string, 0> none {};
    const auto link = [&](std::span<const std::string> libs,
                          std::span<const std::string> fws = {}) {
        return detail::link_flags(t, BuildMode::Debug, DebugInfo::Default, Sanitizer::Default,
                                  Lto::Default, Hardening::None, none, libs, fws, none, "");
    };
    const std::array<std::string, 1> ok {"z"};
    auto good = link(ok);
    REQUIRE(good.has_value());
    CHECK(contains(good->linker, "-lz"));

    for (const char* bad : {"z;touch pwned", "z$(id)", "-lz", "a b"}) {
        const std::array<std::string, 1> libs {bad};
        auto r = link(libs);
        INFO(bad);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
    }
    const std::array<std::string, 1> bad_fw {"Cocoa$(id)"};
    auto r = link(std::array<std::string, 0> {}, bad_fw);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
}

TEST_CASE("flags: C standard is per-target and never touches C++ flags") {
    const auto t = clang_triple();
    const std::array<std::string, 0> none {};
    const auto flags_for = [&](CStandard c) {
        return detail::build_flags(t, BuildMode::Debug, CppStandard::Cpp23, Warnings::Default,
                                   DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                   Lto::Default, Hardening::None, false, none, none, "", none, c);
    };
    auto def = flags_for(CStandard::Default);
    REQUIRE(def.has_value());
    CHECK(contains(def->compile_c, "-std=gnu17")); /* today's default is unchanged */
    CHECK(contains(def->compile, "-std=c++23"));

    for (const auto& [c, flag] : {std::pair{CStandard::C99, "-std=c99"},
                                  std::pair{CStandard::C11, "-std=c11"},
                                  std::pair{CStandard::C17, "-std=c17"},
                                  std::pair{CStandard::C23, "-std=c23"}}) {
        auto r = flags_for(c);
        INFO(flag);
        REQUIRE(r.has_value());
        CHECK(contains(r->compile_c, flag));
        CHECK_FALSE(contains(r->compile_c, "-std=gnu17"));
        CHECK_FALSE(contains(r->compile, flag)); /* C++ side untouched */
    }
}

TEST_CASE("flags: c_flags() applies to C sources only") {
    const auto t = clang_triple();
    const std::array<std::string, 0> none {};
    auto r = detail::build_flags(t, BuildMode::Debug, CppStandard::Default, Warnings::Default,
                                 DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                 Lto::Default, Hardening::None, false, none, none,
                                 "-DSHARED=1", none, CStandard::Default, "-DC_ONLY=1 -ffast-math");
    REQUIRE(r.has_value());
    CHECK(contains(r->compile, "-DSHARED=1"));
    CHECK(contains(r->compile_c, "-DSHARED=1"));
    CHECK(contains(r->compile_c, "-DC_ONLY=1 -ffast-math"));
    CHECK_FALSE(contains(r->compile, "-DC_ONLY=1"));
    CHECK_FALSE(contains(r->compile, "-ffast-math"));
}

TEST_CASE("flags: MSVC accepts C11/C17 and rejects C99/C23") {
    TargetTriple t = clang_triple();
    t.family = CompilerFamily::Msvc;
    t.compiler = "clang-cl";
    t.ar = "llvm-lib";
    const std::array<std::string, 0> none {};
    const auto flags_for = [&](CStandard c) {
        return detail::build_flags(t, BuildMode::Debug, CppStandard::Default, Warnings::Default,
                                   DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                   Lto::Default, Hardening::None, false, none, none, "", none, c);
    };
    auto c11 = flags_for(CStandard::C11);
    REQUIRE(c11.has_value());
    CHECK(contains(c11->compile_c, "/std:c11"));
    auto c17 = flags_for(CStandard::C17);
    REQUIRE(c17.has_value());
    CHECK(contains(c17->compile_c, "/std:c17"));
    for (const auto c : {CStandard::C99, CStandard::C23}) {
        auto r = flags_for(c);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
        CHECK(contains(r.error().message, "MSVC has no such C mode"));
    }
}

TEST_CASE("sources_in: .ixx interface units are picked up like .cppm") {
    ScratchDir dir {"ixx"};
    REQUIRE(detail::write_file("src/mod.ixx", "export module m;\nexport int f() { return 1; }\n").has_value());
    REQUIRE(detail::write_file("src/main.cpp", "import m;\nint main() { return f(); }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("ixx", 2, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));
    proj.add_static_library("m").sources_in("src");
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());
    auto manifest = detail::read_file("build/debug/ninja/build.ninja");
    REQUIRE(manifest.has_value());
    CHECK(contains(*manifest, "src/mod.ixx"));
    CHECK(contains(*manifest, "compile_bmi"));
}

TEST_CASE("c_flags: reaches the C edge in a mixed target, not the C++ edge") {
    ScratchDir dir {"mixed"};
    REQUIRE(detail::write_file("src/a.c", "int a(void) { return 1; }\n").has_value());
    REQUIRE(detail::write_file("src/b.cpp", "int b() { return 2; }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("mixed", 2, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));
    proj.add_static_library("mix").sources_in("src")
        .c_standard(CStandard::C11)
        .c_flags("-DC_ONLY=1");
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());
    auto manifest = detail::read_file("build/debug/ninja/build.ninja");
    REQUIRE(manifest.has_value());
    /* one build statement per source: from its `build` line to the blank line
     * that ends the edge (the flags live in the indented variable block) */
    const auto edge_for = [&](std::string_view name) {
        const auto at = manifest->find(std::string(name));
        REQUIRE(at != std::string::npos);
        const auto start = manifest->rfind("\nbuild ", at);
        REQUIRE(start != std::string::npos);
        const auto end = manifest->find("\n\n", at);
        REQUIRE(end != std::string::npos);
        return manifest->substr(start, end - start);
    };
    const std::string c_edge = edge_for("src/a.c");
    const std::string cxx_edge = edge_for("src/b.cpp");
    CHECK(contains(c_edge, "-std=c11"));
    CHECK(contains(c_edge, "-DC_ONLY=1"));
    CHECK_FALSE(contains(cxx_edge, "-DC_ONLY=1"));
    CHECK_FALSE(contains(cxx_edge, "-std=c11"));
}

TEST_CASE("flags: symbols_hidden() hides symbols per language") {
    const auto t = clang_triple();
    const std::array<std::string, 0> none {};
    auto r = detail::build_flags(t, BuildMode::Debug, CppStandard::Default, Warnings::Default,
                                 DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                 Lto::Default, Hardening::None, false, none, none, "", none,
                                 CStandard::Default, "", true);
    REQUIRE(r.has_value());
    CHECK(contains(r->compile, "-fvisibility=hidden"));
    CHECK(contains(r->compile, "-fvisibility-inlines-hidden"));
    CHECK(contains(r->compile_c, "-fvisibility=hidden"));
    CHECK_FALSE(contains(r->compile_c, "-fvisibility-inlines-hidden")); /* C++-only */

    /* MSVC has no equivalent: exports are the model, so the knob is inert */
    TargetTriple m = t;
    m.family = CompilerFamily::Msvc;
    m.compiler = "clang-cl";
    m.ar = "llvm-lib";
    auto rmsvc = detail::build_flags(m, BuildMode::Debug, CppStandard::Default, Warnings::Default,
                                     DebugInfo::Default, Optimization::Default, Sanitizer::Default,
                                     Lto::Default, Hardening::None, false, none, none, "", none,
                                     CStandard::Default, "", true);
    REQUIRE(rmsvc.has_value());
    CHECK_FALSE(contains(rmsvc->compile, "-fvisibility"));
}
