/* Linux -> Windows cross-compilation with the two mingw-w64 toolchains:
 * mingw-w64's g++ (target-prefixed GCC driver) and llvm-mingw (clang family:
 * full module + import std support). Exes are file-checked as PE; they run
 * under wine when it is installed. Skips cleanly when the tools are absent. */
module;
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include <unistd.h>

module truss;
import :api;
import :impl;

using namespace truss;

namespace {

struct ScratchDir {
    std::filesystem::path prev;
    explicit ScratchDir(const std::string& name) {
        prev = std::filesystem::current_path();
        const auto dir = std::filesystem::temp_directory_path() / ("truss-mingw-test-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~ScratchDir() { chdir(prev.c_str()); }
};

bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

void write_project() {
    REQUIRE(detail::write_file("lib/core.cpp", "int core() { return 7; }\n").has_value());
    REQUIRE(detail::write_file("app/main.cpp",
                               "#include <cstdio>\nint core();\n"
                               "int main() { std::puts(\"win\"); return core() == 7 ? 0 : 1; }\n")
                .has_value());
}

/* llvm-mingw's cross clang++ (absolute path: the runtime dlls are derived
 * from it): <arch>-w64-mingw32-clang++ on PATH */
std::string llvm_mingw_cxx() {
    for (const char* arch : {"x86_64", "aarch64"}) {
        auto found = os::find_program(std::string(arch) + "-w64-mingw32-clang++");
        if (found)
            return found->generic_string();
    }
    return {};
}

/* wine runs a PE exe; llvm-mingw's runtime dlls must be on its search path
 * (WINEPATH takes Windows-style Z:\ paths) */
bool run_pe(const std::filesystem::path& exe, const std::string& dll_dir) {
    if (!os::program_exists("wine"))
        return false; /* caller skips */
    if (!dll_dir.empty()) {
        std::string winepath = "Z:";
        for (const char c : dll_dir)
            winepath += c == '/' ? '\\' : c;
        setenv("WINEPATH", winepath.c_str(), 1);
    }
    auto run = os::run({exe.string()});
    unsetenv("WINEPATH");
    return run.has_value() && run->exit_code == 0;
}

} // namespace

TEST_CASE("mingw: mingw-w64 g++ cross builds exe + archive (target-prefixed driver)") {
    if (!os::program_exists("x86_64-w64-mingw32-g++")) {
        MESSAGE("x86_64-w64-mingw32-g++ not installed, skipping");
        return;
    }
    ScratchDir scratch {"gcc"};
    write_project();
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("mingw-gcc", 1, argv);
    proj.configure(Config{}.compiler("x86_64-w64-mingw32-g++").target("x86_64-w64-mingw32"));
    auto& lib = proj.add_static_library("core").sources("lib/core.cpp");
    proj.add_executable("app").sources("app/main.cpp").depends_on(lib);
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    CHECK(std::filesystem::exists("build/debug/bin/x64-windows-gcc/app.exe"));
    CHECK(std::filesystem::exists("build/debug/lib/x64-windows-gcc/libcore.a"));
}

TEST_CASE("mingw: llvm-mingw cross builds exe + shared lib (dll + import lib)") {
    const std::string cxx = llvm_mingw_cxx();
    if (cxx.empty()) {
        MESSAGE("llvm-mingw not installed, skipping");
        return;
    }
    ScratchDir scratch {"clang"};
    write_project();
    REQUIRE(detail::write_file("lib/dl.cpp", "__declspec(dllexport) int dl() { return 5; }\n").has_value());
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("mingw-clang", 1, argv);
    proj.configure(Config{}.compiler(cxx).target("x86_64-w64-mingw32"));
    auto& lib = proj.add_static_library("core").sources("lib/core.cpp");
    auto& dl = proj.add_shared_library("dl").sources("lib/dl.cpp");
    proj.add_executable("app").sources("app/main.cpp").depends_on(lib).depends_on(dl);
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    CHECK(std::filesystem::exists("build/debug/bin/x64-windows-clang/app.exe"));
    CHECK(std::filesystem::exists("build/debug/bin/x64-windows-clang/libdl.dll"));
    CHECK(std::filesystem::exists("build/debug/lib/x64-windows-clang/libdl.dll.a"));

    /* run under wine when both are present (dlls next to the exe: no WINEPATH
     * needed for the project's own dll; the CRT dlls are static by default) */
    if (os::program_exists("wine"))
        CHECK_MESSAGE(run_pe("build/debug/bin/x64-windows-clang/app.exe", {}),
                      "the cross-built exe should run under wine");
}

TEST_CASE("mingw: llvm-mingw builds C++20 modules and import std for Windows") {
    const std::string cxx = llvm_mingw_cxx();
    if (cxx.empty()) {
        MESSAGE("llvm-mingw not installed, skipping");
        return;
    }
    ScratchDir scratch {"clang-mod"};
    REQUIRE(detail::write_file("lib/greet.cppm",
                               "export module greet;\n"
                               "import std;\n"
                               "export int greet() { return 7; }\n")
                .has_value());
    REQUIRE(detail::write_file("app/main.cpp",
                               "import std;\nimport greet;\n"
                               "int main() { std::println(\"{}\", greet()); }\n")
                .has_value());
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("mingw-modules", 1, argv);
    proj.configure(Config{}.compiler(cxx).target("x86_64-w64-mingw32").stdlib(StdLib::Libcxx));
    auto& g = proj.add_static_library("greet").sources("lib/greet.cppm");
    proj.add_executable("app").sources("app/main.cpp").depends_on(g);
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());
    CHECK(std::filesystem::exists("build/debug/bin/x64-windows-clang/app.exe"));

    /* wine + the cross libc++ runtime dlls: llvm-mingw keeps them in
     * <root>/<triple>/bin next to the compiler's bin/ */
    if (os::program_exists("wine")) {
        const auto cxx_path = std::filesystem::path(cxx);
        const std::string triple_name = cxx_path.filename().generic_string();
        const auto stem = triple_name.substr(0, triple_name.find("-clang++"));
        const std::string dll_dir = (cxx_path.parent_path().parent_path() / stem / "bin").generic_string();
        CHECK_MESSAGE(run_pe("build/debug/bin/x64-windows-clang/app.exe", dll_dir),
                      "the cross-built module exe should run under wine");
    }}
