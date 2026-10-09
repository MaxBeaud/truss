set_project("truss")
set_version("0.1.0")

-- xmake's own artifacts live in .xmake/ so truss's convention (build/<mode>/)
-- never collides with the bootstrap build.
set_config("builddir", ".xmake")

add_rules("mode.debug", "mode.release")

-- Generate compile_commands.json automatically for clangd
add_rules("plugin.compile_commands.autoupdate", { outputdir = "." })

-- Force LLVM/Clang & LLVM's libc++
set_toolchains("clang")
set_runtimes("c++_shared")
set_languages("c++latest")

-- C++26 named modules (truss is a single-TU module: src/truss.cppm)
set_policy("build.c++.modules", true)

-- =============================================================================
-- Global Compiler Warnings & Linker Selection
-- =============================================================================
set_warnings("all", "extra")
add_cxflags(
    "-Wformat=2",
    "-Wshadow",
    "-Wconversion",
    "-Wsign-conversion",
    "-Wnull-dereference",
    "-Wdouble-promotion",
    "-Wcast-align",
    "-Wold-style-cast",
    "-Wnon-virtual-dtor",
    "-Wno-c++98-compat",
    "-Werror=return-type",
    "-Werror=format"
)

-- Dynamic linker detection
on_config(function (target)
import("lib.detect.find_tool")
local mold = find_tool("mold")
if mold then
    target:add("ldflags", "-fuse-ld=mold")
    target:add("shflags", "-fuse-ld=mold")
else
    cprint("${yellow}--> [mold] Linker not found. Falling back to default system linker.${clear}")
    end
end)

-- =============================================================================
-- Dependencies
-- =============================================================================

add_requires("catch2")

-- samurai (vendored ninja-compatible build backend, see vendor/samurai/TRUSS-VENDOR.txt).
-- Compiled as plain C17; samu.c is excluded because src/samu/shim.c replaces its main().
local samu_force = {
    languages = "c17",
    warnings = "none",
    cxflags = {
        "-D_DEFAULT_SOURCE",
        "-D_POSIX_C_SOURCE=200809L",
        -- silence the global C++-only -W flags leaking onto C translation units
        "-Wno-unused-command-line-argument",
        "-w",
    },
}

-- =============================================================================
-- Targets
-- =============================================================================

-- Module interface units of truss (BMI provider for every consumer target).
target("truss-mod")
    set_kind("moduleonly")
    add_rules("c++.build.modules")
    set_policy("check.auto_ignore_flags", false)
    add_files("src/*.cppm")

-- truss implementation: module implementation units + vendored samurai + shim.
target("truss-lib")
    set_kind("static")
    add_rules("c++.build.modules")
    add_deps("truss-mod")
    add_files("src/core/*.cpp")
    add_includedirs("vendor/samurai")
    add_files("src/samu/shim.c", {
        force = {
            languages = "c17",
            warnings = "none",
            cxflags = samu_force.cxflags,
        },
    })
    -- samu.c is excluded: its main() is replaced by src/samu/shim.c
    add_files("vendor/samurai/**.c|samu.c", { force = samu_force })

-- Self-hosting build script (truss building truss — PLAN.MD phase 7).
-- Until module support lands in truss itself, this validates the consumption
-- path: `import truss;` + link libtruss.a -> ./truss
target("truss-cli")
    set_kind("binary")
    add_rules("c++.build.modules")
    add_deps("truss-mod", "truss-lib")
    add_files("truss.cpp")
    set_basename("truss")
    set_targetdir("$(projectdir)")

-- examples/hello build script, compiled against libtruss (end-to-end check).
target("hello-truss")
    set_kind("binary")
    add_rules("c++.build.modules")
    add_deps("truss-mod", "truss-lib")
    add_files("examples/hello/truss.cpp")
    set_basename("truss")
    set_targetdir("$(projectdir)/examples/hello")

-- examples/shared build script (shared libs + rpath + user tasks).
target("shared-truss")
    set_kind("binary")
    add_rules("c++.build.modules")
    add_deps("truss-mod", "truss-lib")
    add_files("examples/shared/truss.cpp")
    set_basename("truss")
    set_targetdir("$(projectdir)/examples/shared")

-- examples/modules build script (C++ modules in user projects).
target("modules-truss")
    set_kind("binary")
    add_rules("c++.build.modules")
    add_deps("truss-mod", "truss-lib")
    add_files("examples/modules/truss.cpp")
    set_basename("truss")
    set_targetdir("$(projectdir)/examples/modules")

-- examples/fetch build script (fetched dependencies).
target("fetch-truss")
    set_kind("binary")
    add_rules("c++.build.modules")
    add_deps("truss-mod", "truss-lib")
    add_files("examples/fetch/truss.cpp")
    set_basename("truss")
    set_targetdir("$(projectdir)/examples/fetch")

-- Unit tests: test files are implementation units of truss (module truss;) so they
-- can exercise the internal :impl partition (glob, fs, proc, flags, ninja emitter).
target("truss-tests")
    set_kind("binary")
    add_rules("c++.build.modules")
    add_deps("truss-mod", "truss-lib")
    add_packages("catch2")
    add_files("tests/unit/*.cpp")

if is_mode("debug") then
    add_defines("_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG")
    add_cxflags("-g3", "-O1", "-fno-omit-frame-pointer")
    add_cxflags("-fsanitize=address,undefined", "-fsanitize-address-use-after-scope")
    add_ldflags("-fsanitize=address,undefined")
else
    -- Optimization
    add_cxflags("-O3", "-funroll-loops", "-fomit-frame-pointer", "-ffunction-sections", "-fdata-sections")
    add_ldflags("-Wl,--gc-sections", "-s")
    set_policy("build.optimization.lto", true)
    add_cxflags("-flto=full")
    add_ldflags("-flto=full")

    -- Hardening
    add_defines("_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST")
    add_cxflags(
        "-fstack-protector-strong",
        "-fstack-clash-protection",
        "-fcf-protection=full",
        "-ftrivial-auto-var-init=pattern"
    )
    add_ldflags(
        "-Wl,-z,relro",
        "-Wl,-z,now",
        "-Wl,-z,noexecstack",
        "-Wl,--as-needed"
    )
end
