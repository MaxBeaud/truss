/* End-to-end MSVC-family target test: clang-cl compiles COFF objects with
 * /showIncludes (deps = msvc, the samurai truss patch), llvm-lib archives,
 * lld-link links a freestanding PE (no Windows CRT on this Linux box, so the
 * exe is produced but never run). Skips cleanly when the tools are absent. */
module;
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include <unistd.h>

module truss;
import :api;
import :impl;

using namespace truss;

namespace {

/* RAII chdir: Project takes cwd() at construction */
struct ScratchDir {
    std::filesystem::path prev;
    std::filesystem::path dir;
    explicit ScratchDir(const std::string& name) {
        prev = std::filesystem::current_path();
        dir = std::filesystem::temp_directory_path() / ("truss-msvc-test-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir / "src");
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~ScratchDir() {
        chdir(prev.c_str());
        std::filesystem::remove_all(dir);
    }
};

TargetTriple clang_cl_triple() {
    TargetTriple t;
    t.os = Os::Windows;
    t.arch = Arch::X64;
    t.family = CompilerFamily::Msvc;
    t.compiler = "clang-cl";
    t.ar = "llvm-lib";
    t.linker = "lld-link"; /* no VS installation on this Linux box */
    return t;
}

bool msvc_toolchain_available() {
    return detail::program_exists("clang-cl") && detail::program_exists("lld-link") &&
           detail::program_exists("llvm-lib");
}

/* Catch2 cannot stringify file_time_t; compare integer ns instead */
long long mtime_ns(const std::filesystem::path& p) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::filesystem::last_write_time(p).time_since_epoch())
        .count();
}

} // namespace

TEST_CASE("msvc: clang-cl end-to-end (compile, /showIncludes deps, archive, link)") {
    if (!msvc_toolchain_available()) {
        WARN("clang-cl/lld-link/llvm-lib not on PATH: skipping MSVC integration test");
        return;
    }

    ScratchDir scratch {"deps"};
    REQUIRE(detail::write_file("src/value.h", "#define VALUE 7\n").has_value());
    REQUIRE(detail::write_file("src/add.cpp",
                               "#include \"value.h\"\n"
                               "int add() { return VALUE; }\n")
                  .has_value());
    REQUIRE(detail::write_file("src/main.cpp",
                               "int add();\n"
                               "int main() { return add() - 7; }\n")
                  .has_value());

    char prog[] = "truss-tests";
    char* argv[] = {prog};

    auto make_project = [&] {
        auto proj = std::make_unique<Project>("msvc-test", 1, argv);
        const auto triple = clang_cl_triple();
        auto& lib = proj->add_static_library("add")
                        .sources("src/add.cpp")
                        .cross_target(triple);
        /* freestanding link: no Windows CRT import libs on this box */
        proj->add_executable("app")
            .sources("src/main.cpp")
            .cross_target(triple)
            .depends_on(lib)
            .linker_flags("/NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:main");
        return proj;
    };

    {
        auto proj = make_project();
        auto r = proj->build();
        INFO((r ? "build ok" : r.error().message));
        REQUIRE(r.has_value());
    }

    const std::filesystem::path bld = "build/debug";
    const std::filesystem::path obj = bld / "obj/x64-windows-msvc/add/src/add.cpp.obj";
    CHECK(std::filesystem::exists(obj));
    CHECK(std::filesystem::exists(bld / "obj/x64-windows-msvc/app/src/main.cpp.obj"));
    CHECK(std::filesystem::exists(bld / "lib/x64-windows-msvc/add.lib"));
    CHECK(std::filesystem::exists(bld / "bin/x64-windows-msvc/app.exe"));

    /* header dependency tracking: touch value.h and rebuild — the /showIncludes
     * record must dirty add.cpp.obj (and relink the exe) */
    const long long before = mtime_ns(obj);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(detail::write_file("src/value.h", "#define VALUE 7\n/* touched */\n").has_value());
    {
        auto proj = make_project();
        auto r = proj->build();
        INFO((r ? "rebuild ok" : r.error().message));
        REQUIRE(r.has_value());
    }
    const long long after = mtime_ns(obj);
    CHECK(after != before);

    /* no-op build: nothing is rebuilt */
    {
        auto proj = make_project();
        REQUIRE(proj->build().has_value());
    }
    CHECK(mtime_ns(obj) == after);
}

TEST_CASE("msvc: .cppm sources are rejected, not silently miscompiled") {
    if (!msvc_toolchain_available()) {
        WARN("clang-cl/lld-link/llvm-lib not on PATH: skipping MSVC integration test");
        return;
    }
    ScratchDir scratch {"modules"};
    REQUIRE(detail::write_file("src/m.cppm", "export module m;\nexport int f() { return 1; }\n").has_value());

    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("msvc-modules", 1, argv);
    proj.add_static_library("m").sources("src/m.cppm").cross_target(clang_cl_triple());
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
}
