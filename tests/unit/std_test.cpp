/* import std; support: guards, manifest wiring and real clang + libc++ builds */
module;
#include <doctest/doctest.h>

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
        const auto dir = std::filesystem::temp_directory_path() / ("truss-std-test-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~ScratchDir() { chdir(prev.c_str()); }
};

bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

size_t count(std::string_view hay, std::string_view needle) {
    size_t n = 0;
    for (size_t p = hay.find(needle); p != std::string_view::npos; p = hay.find(needle, p + needle.size()))
        ++n;
    return n;
}

std::string manifest_text() {
    auto m = detail::read_file("build/debug/ninja/build.ninja");
    REQUIRE(m.has_value());
    return *m;
}

/* clang++ with a libc++ std module manifest */
bool std_module_available() {
    if (!os::program_exists("clang++"))
        return false;
    auto r = os::capture({"clang++", "-std=c++23", "-stdlib=libc++", "-print-library-module-manifest-path"});
    if (!r || r->exit_code != 0)
        return false;
    std::string p = r->output;
    while (!p.empty() && (p.back() == '\n' || p.back() == '\r' || p.back() == ' '))
        p.pop_back();
    std::error_code ec;
    return !p.empty() && std::filesystem::is_regular_file(p, ec);
}

/* g++ with libstdc++'s std module manifest (GCC 15+) */
bool gcc_std_module_available() {
    if (!os::program_exists("g++"))
        return false;
    auto r = os::capture({"g++", "-print-file-name=libstdc++.modules.json"});
    if (!r || r->exit_code != 0)
        return false;
    std::string p = r->output;
    while (!p.empty() && (p.back() == '\n' || p.back() == '\r' || p.back() == ' '))
        p.pop_back();
    std::error_code ec;
    return !p.empty() && std::filesystem::is_regular_file(p, ec);
}

constexpr std::string_view kPrintln =
    "import std;\nint main() { std::println(\"hello {}\", 42); return 0; }\n";

} // namespace

TEST_CASE("import std: dry-run wires one std BMI, -fmodule-file and links std.o") {
    if (!std_module_available()) { MESSAGE("clang++/libc++ std module not available, skipping"); return; }
    ScratchDir scratch {"dry"};
    REQUIRE(detail::write_file("a/main.cpp", std::string(kPrintln)).has_value());
    REQUIRE(detail::write_file("b/main.cpp", std::string(kPrintln)).has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("std-dry", 2, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));
    proj.add_executable("a").sources("a/main.cpp");
    proj.add_executable("b").sources("b/main.cpp");
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const std::string m = manifest_text();
    CHECK(count(m, "-fmodule-output=") == 1);                 /* a single std edge, deduped */
    CHECK(count(m, ": compile_bmi ") == 1);
    CHECK(contains(m, "/std/"));
    CHECK(count(m, "-fmodule-file=std=") == 2);               /* both consumers */
    CHECK_FALSE(contains(m, "std.compat"));
    CHECK(count(m, "/std.o") >= 3);                           /* its edge + two link lines */
}

TEST_CASE("import std: toolchains without libc++ modules are a Config error") {
    ScratchDir scratch {"neg"};
    REQUIRE(detail::write_file("app/main.cpp", std::string(kPrintln)).has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    {
        Project proj("std-zig", 2, argv);
        proj.configure(Config().toolchain(Toolchain::Zig));
        proj.add_executable("app").sources("app/main.cpp");
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
        CHECK(contains(r.error().message, "libc++"));
        CHECK(contains(r.error().message, "Toolchain::LLVM"));
    }
    {
        Project proj("std-gcc", 2, argv);
        proj.configure(Config().toolchain(Toolchain::GCC));
        proj.add_executable("app").sources("app/main.cpp");
        auto r = proj.build();
        if (gcc_std_module_available()) {
            /* GCC 15+ ships the std module: a valid configuration now */
            INFO((r ? "ok" : r.error().message));
            CHECK(r.has_value());
        } else {
            REQUIRE_FALSE(r.has_value());
            CHECK(r.error().kind == Error::Kind::Config);
            CHECK(contains(r.error().message, "GCC 15+"));
        }
    }
    {
        Project proj("std-libstdcxx", 2, argv);
        proj.configure(Config().compiler("clang++").stdlib(StdLib::Libstdcxx));
        proj.add_executable("app").sources("app/main.cpp");
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
    }
}

TEST_CASE("import std: needs C++23 or later") {
    ScratchDir scratch {"old"};
    REQUIRE(detail::write_file("app/main.cpp", std::string(kPrintln)).has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("std-old", 2, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));
    proj.add_executable("app").sources("app/main.cpp").cpp_standard(CppStandard::Cpp20);
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(contains(r.error().message, "c++23"));
}

TEST_CASE("import std: real build and run (exe, static lib + exe, std.compat)") {
    if (!std_module_available()) { MESSAGE("clang++/libc++ std module not available, skipping"); return; }
    ScratchDir scratch {"e2e"};
    REQUIRE(detail::write_file("solo/main.cpp", std::string(kPrintln)).has_value());
    REQUIRE(detail::write_file("lib/greet.cpp",
                               "import std;\nint greet() { std::println(\"lib\"); return 7; }\n").has_value());
    REQUIRE(detail::write_file("app/main.cpp",
                               "import std;\nint greet();\n"
                               "int main() { std::println(\"app\"); return greet() == 7 ? 0 : 1; }\n")
                .has_value());
    REQUIRE(detail::write_file("cmp/main.cpp",
                               "import std.compat;\n"
                               "int main() { std::println(\"{}\", std::strlen(\"abc\")); return ::strlen(\"ab\") == 2 ? 0 : 1; }\n")
                .has_value());
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("std-e2e", 1, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));
    proj.add_executable("solo").sources("solo/main.cpp");
    auto& g = proj.add_static_library("greet").sources("lib/greet.cpp");
    proj.add_executable("app").sources("app/main.cpp").depends_on(g);
    proj.add_executable("cmp").sources("cmp/main.cpp");
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const auto bin = std::filesystem::current_path() / "build/debug/bin" /
                     detail::triple_dir(*detail::apply_config(Config().toolchain(Toolchain::LLVM), host_triple()));
    for (const char* name : {"solo", "app", "cmp"}) {
        auto run = os::run({(bin / name).string()});
        REQUIRE(run.has_value());
        CHECK_MESSAGE(run->exit_code == 0, name);
    }
}

TEST_CASE("import std: exe links a shared lib that imports std (no std.o in the exe)") {
    if (!std_module_available()) { MESSAGE("clang++/libc++ std module not available, skipping"); return; }
    ScratchDir scratch {"shared"};
    /* the shared lib imports std; the exe does NOT. std.o must live in the .so
     * only, never also in the exe (a second module initializer is a duplicate
     * definition / double init). */
    REQUIRE(detail::write_file("lib/val.cpp",
                               "import std;\nint libval() { std::println(\"so: {}\", 99); return 99; }\n")
                .has_value());
    REQUIRE(detail::write_file("app/main.cpp",
                               "int libval();\nint main() { return libval() == 99 ? 0 : 1; }\n")
                .has_value());
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("std-shared", 1, argv);
    proj.configure(Config().toolchain(Toolchain::LLVM));
    auto& sl = proj.add_shared_library("val").sources("lib/val.cpp");
    proj.add_executable("app").sources("app/main.cpp").depends_on(sl);
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const std::string tdir =
        detail::triple_dir(*detail::apply_config(Config().toolchain(Toolchain::LLVM), host_triple()));
    const std::string m = manifest_text();
    /* ELF shared libs land in lib/; the .so carries std.o, the exe must not */
    REQUIRE(contains(m, "lib/" + tdir + "/libval.so"));
    /* isolate the exe's link line and assert it does NOT pull in std.o
     * (its only std user is a shared lib, which has its own copy) */
    const std::string key = "bin/" + tdir + "/app: link_exe";
    const auto start = m.find(key);
    REQUIRE(start != std::string::npos);
    const auto eol = m.find('\n', start);
    const std::string app_link = m.substr(start, eol - start);
    CHECK_FALSE(contains(app_link, "std.o"));

    const auto bin = std::filesystem::current_path() / "build/debug/bin" / tdir;
    auto run = os::run({(bin / "app").string()});
    REQUIRE(run.has_value());
    CHECK(run->exit_code == 0);
}

TEST_CASE("import std + modules: GCC end to end (mapper, partitions, std.gcm)") {
    if (!gcc_std_module_available()) { MESSAGE("g++ without libstdc++.modules.json, skipping"); return; }
    ScratchDir scratch {"gcc-std"};
    /* a partition + a GMF include + std inside the partition: exercises the
     * module mapper (CMI paths), compile_bmi_gcc and the transitive closure */
    REQUIRE(detail::write_file("lib/greet.cppm", "export module greet;\nexport import :core;\n").has_value());
    REQUIRE(detail::write_file("lib/core.cppm",
                               "module;\n#include <string>\n"
                               "export module greet:core;\n"
                               "import std;\n"
                               "export std::string greet() { return std::format(\"gcc: {}\", 7); }\n")
                .has_value());
    REQUIRE(detail::write_file("app/main.cpp",
                               "import std;\nimport greet;\n"
                               "int main() { std::println(\"{}\", greet()); }\n")
                .has_value());
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("gcc-std", 1, argv);
    proj.configure(Config().toolchain(Toolchain::GCC));
    auto& g = proj.add_static_library("greet").sources({"lib/greet.cppm", "lib/core.cppm"});
    proj.add_executable("app").sources("app/main.cpp").depends_on(g);
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const std::string tdir =
        detail::triple_dir(*detail::apply_config(Config().toolchain(Toolchain::GCC), host_triple()));
    /* CMIs landed where the mapper routed them (the std dir name carries the
     * BMI-config flags, so glob rather than spell it) */
    auto std_gcm = fs::glob("build/debug/std/*/std.gcm");
    REQUIRE(std_gcm.has_value());
    CHECK(std_gcm->size() == 1);
    const bool greet_cmi = std::filesystem::exists("build/debug/obj/x64-linux-gcc/greet/bmi/greet.gcm")
                        || std::filesystem::exists("build/debug/obj/x64-linux-gcc/lib/greet.gcm");
    CHECK(greet_cmi);
    const auto bin = std::filesystem::current_path() / "build/debug/bin" / tdir;
    auto run = os::run({(bin / "app").string()});
    REQUIRE(run.has_value());
    CHECK(run->exit_code == 0);
}
