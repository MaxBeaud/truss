// Static linking (static_link / static_deps / static_stdlib), zig toolchain,
// cross-compilation targets and the review fixes that came with them.
module;
#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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
        const auto dir = std::filesystem::temp_directory_path() / ("truss-static-test-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~ScratchDir() { chdir(prev.c_str()); }
};

TargetTriple linux_triple(std::string compiler, CompilerFamily family) {
    TargetTriple t;
    t.os = Os::Linux;
    t.arch = Arch::X64;
    t.family = family;
    t.compiler = std::move(compiler);
    t.ar = "ar";
    return t;
}

bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

Result<detail::LinkFlags> link_with(const TargetTriple& t, std::span<const std::string> libs,
                            std::span<const std::string> static_libs, StaticStdlib s) {
    const std::array<std::string, 0> none {};
    return detail::link_flags(t, BuildMode::Debug, DebugInfo::Default, Sanitizer::Default, Lto::Default,
                              Hardening::None, none, libs, none, none, "", static_libs, s);
}

std::string manifest_text() {
    auto m = detail::read_file("build/debug/ninja/build.ninja");
    REQUIRE(m.has_value());
    return *m;
}

} // namespace

/* --- flags ------------------------------------------------------------------ */

TEST_CASE("static: -Bstatic block precedes dynamic libs") {
    const auto t = linux_triple("clang++", CompilerFamily::Clang);
    const std::array<std::string, 1> dyn {"ssl"};
    const std::array<std::string, 2> stat {"curl", "z"};
    auto r = link_with(t, dyn, stat, StaticStdlib::Default);
    REQUIRE(r.has_value());
    CHECK(contains(r->linker, "-Wl,-Bstatic -lcurl -lz -Wl,-Bdynamic -lssl"));
}

TEST_CASE("static: macOS and MSVC reject -Bstatic libs") {
    const std::array<std::string, 0> none {};
    const std::array<std::string, 1> stat {"z"};
    auto mac = linux_triple("clang++", CompilerFamily::Clang);
    mac.os = Os::MacOS;
    CHECK_FALSE(link_with(mac, none, stat, StaticStdlib::Default).has_value());
    auto msvc = linux_triple("clang-cl", CompilerFamily::Msvc);
    msvc.os = Os::Windows;
    CHECK_FALSE(link_with(msvc, none, stat, StaticStdlib::Default).has_value());
}

TEST_CASE("static: runtime libs stay dynamic") {
    for (const std::string_view l : {"c", "m", "pthread", "dl", "rt", "stdc++", "c++", "gcc_s"})
        CHECK(detail::is_runtime_lib(l));
    for (const std::string_view l : {"z", "ssl", "fmt", "curl"})
        CHECK_FALSE(detail::is_runtime_lib(l));
}

TEST_CASE("static_stdlib: Gcc / Llvm / Musl flags and validation") {
    const std::array<std::string, 0> none {};
    auto gcc = linux_triple("g++", CompilerFamily::Gcc);
    auto r = link_with(gcc, none, none, StaticStdlib::Gcc);
    REQUIRE(r.has_value());
    CHECK(contains(r->driver, "-static-libstdc++ -static-libgcc"));

    /* Llvm needs every object on libc++ */
    CHECK_FALSE(link_with(gcc, none, none, StaticStdlib::Llvm).has_value());
    auto clang = linux_triple("clang++", CompilerFamily::Clang);
    CHECK_FALSE(link_with(clang, none, none, StaticStdlib::Llvm).has_value());
    clang.stdlib = StdLib::Libcxx;
    r = link_with(clang, none, none, StaticStdlib::Llvm);
    REQUIRE(r.has_value());
    CHECK(contains(r->driver, "-nostdlib++"));
    CHECK(r->linker.ends_with("-Wl,-Bstatic -lc++ -lc++abi -Wl,-Bdynamic"));
    /* ... and Gcc is then a mismatch */
    CHECK_FALSE(link_with(clang, none, none, StaticStdlib::Gcc).has_value());

    /* Musl needs a musl toolchain */
    CHECK_FALSE(link_with(gcc, none, none, StaticStdlib::Musl).has_value());
    auto musl = linux_triple("x86_64-linux-musl-g++", CompilerFamily::Gcc);
    r = link_with(musl, none, none, StaticStdlib::Musl);
    REQUIRE(r.has_value());
    CHECK(contains(r->driver, "-static"));

    auto mac = linux_triple("clang++", CompilerFamily::Clang);
    mac.os = Os::MacOS;
    CHECK_FALSE(link_with(mac, none, none, StaticStdlib::Gcc).has_value());
}

TEST_CASE("hardening: GNU-ld link flags only on ELF targets") {
    const std::array<std::string, 0> none {};
    auto t = linux_triple("clang++", CompilerFamily::Clang);
    auto lin = detail::link_flags(t, BuildMode::Release, DebugInfo::Default, Sanitizer::Default, Lto::Default,
                                  Hardening::Light, none, none, none, none, "");
    REQUIRE(lin.has_value());
    CHECK(contains(lin->driver, "-Wl,-z,relro"));
    t.os = Os::MacOS;
    auto mac = detail::link_flags(t, BuildMode::Release, DebugInfo::Default, Sanitizer::Default, Lto::Default,
                                  Hardening::Light, none, none, none, none, "");
    REQUIRE(mac.has_value());
    CHECK_FALSE(contains(mac->driver, "-z,relro"));
}

/* --- zig + cross targets ------------------------------------------------------ */

TEST_CASE("zig: detection, C driver, toolchain bundle") {
    CHECK(detail::family_from_compiler_name("zig c++") == CompilerFamily::Clang);
    CHECK(detail::family_from_compiler_name("/opt/zig/zig c++") == CompilerFamily::Clang);
    CHECK(detail::is_zig(linux_triple("zig c++", CompilerFamily::Clang)));
    CHECK(detail::is_zig(linux_triple("/opt/zig/zig c++", CompilerFamily::Clang)));
    CHECK_FALSE(detail::is_zig(linux_triple("clang++", CompilerFamily::Clang)));

    CHECK(detail::c_driver("zig c++") == "zig cc");
    CHECK(detail::c_driver("clang++") == "clang");
    CHECK(detail::c_driver("/usr/bin/clang++-19") == "/usr/bin/clang-19");
    CHECK(detail::c_driver("g++") == "gcc");
    CHECK(detail::c_driver("x86_64-linux-musl-g++") == "x86_64-linux-musl-gcc");
    CHECK(detail::c_driver("/opt/g++-tools/bin/c++") == "/opt/g++-tools/bin/cc");
    CHECK(detail::c_driver("clang-cl") == "clang-cl");

    auto r = detail::apply_config(Config().toolchain(Toolchain::Zig), host_triple());
    REQUIRE(r.has_value());
    CHECK(r->compiler == "zig c++");
    CHECK(r->ar == "zig ar");
    CHECK(r->family == CompilerFamily::Clang);
    CHECK(r->stdlib == StdLib::Default);
    /* zig always uses its bundled libc++ */
    CHECK_FALSE(detail::apply_config(Config().toolchain(Toolchain::Zig).stdlib(StdLib::Libstdcxx), host_triple())
                    .has_value());
}

TEST_CASE("target: triple parsing") {
    auto p = detail::parse_target("aarch64-linux-musl");
    CHECK(p.arch == Arch::Arm64);
    CHECK(p.os == Os::Linux);
    CHECK(p.abi == "musl");
    p = detail::parse_target("x86_64-unknown-linux-gnu");
    CHECK(p.arch == Arch::X64);
    CHECK(p.os == Os::Linux);
    CHECK(p.abi == "gnu");
    p = detail::parse_target("x86_64-w64-mingw32");
    CHECK(p.os == Os::Windows);
    CHECK(p.abi == "gnu");
    p = detail::parse_target("aarch64-linux-android21");
    CHECK(p.os == Os::Android);
    p = detail::parse_target("arm64-apple-darwin");
    CHECK(p.arch == Arch::Arm64);
    CHECK(p.os == Os::MacOS);
    CHECK(p.abi.empty());
    p = detail::parse_target("i686-windows-gnu");
    CHECK(p.arch == Arch::X86);
    CHECK(detail::parse_target("banana").os == Os::Unknown);
}

TEST_CASE("target: Config::target sets os/arch, flags and triple dir") {
    auto r = detail::apply_config(Config().toolchain(Toolchain::Zig).target("aarch64-linux-musl"), host_triple());
    REQUIRE(r.has_value());
    CHECK(r->arch == Arch::Arm64);
    CHECK(r->os == Os::Linux);
    CHECK(detail::triple_dir(*r) == "arm64-linux-musl-zig");

    const std::array<std::string, 0> none {};
    auto c = detail::build_flags(*r, BuildMode::Debug, CppStandard::Default, Warnings::Default, DebugInfo::Default,
                                 Optimization::Default, Sanitizer::Default, Lto::Default, Hardening::None, false,
                                 none, none, "");
    REQUIRE(c.has_value());
    CHECK(contains(c->compile, "-target aarch64-linux-musl"));
    CHECK(contains(c->compile_c, "-target aarch64-linux-musl"));
    auto l = link_with(*r, none, none, StaticStdlib::Musl);
    REQUIRE(l.has_value());
    CHECK(contains(l->driver, "-target aarch64-linux-musl"));

    auto clang = detail::apply_config(Config().compiler("clang++").target("x86_64-windows-gnu"), host_triple());
    REQUIRE(clang.has_value());
    CHECK(clang->os == Os::Windows);
    CHECK(detail::triple_dir(*clang) == "x64-windows-clang");
    auto cc = detail::build_flags(*clang, BuildMode::Debug, CppStandard::Default, Warnings::Default,
                                  DebugInfo::Default, Optimization::Default, Sanitizer::Default, Lto::Default,
                                  Hardening::None, false, none, none, "");
    REQUIRE(cc.has_value());
    CHECK(contains(cc->compile, "--target=x86_64-windows-gnu"));

    /* GCC cross-compiles through a prefixed driver; garbage triples are refused */
    auto gcc = detail::apply_config(Config().compiler("g++").target("aarch64-linux-gnu"), host_triple());
    REQUIRE(gcc.has_value());
    CHECK_FALSE(detail::build_flags(*gcc, BuildMode::Debug, CppStandard::Default, Warnings::Default,
                                    DebugInfo::Default, Optimization::Default, Sanitizer::Default, Lto::Default,
                                    Hardening::None, false, none, none, "")
                    .has_value());
    CHECK_FALSE(detail::apply_config(Config().target("linux-x64"), host_triple()).has_value());
}

TEST_CASE("target: Windows cross build names app.exe, zig disables the launcher") {
    ScratchDir scratch {"zig-win"};
    REQUIRE(detail::write_file("app/main.cpp", "int main() {}\n").has_value());
    REQUIRE(detail::write_file("lib/x.c", "int x(void) { return 1; }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n"; /* manifest only: zig is not required on the test box */
    char* argv[] = {prog, dry};
    Project proj("zig-win", 2, argv);
    proj.configure(Config().toolchain(Toolchain::Zig).target("x86_64-windows-gnu").launcher("true"));
    auto& lib = proj.add_shared_library("x").sources("lib/x.c");
    proj.add_executable("app").sources("app/main.cpp").depends_on(lib);
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());
    const std::string m = manifest_text();
    CHECK(contains(m, "bin/x64-windows-zig/app.exe"));
    /* no rpath on Windows: the dll sits next to app.exe, consumers link the
     * MinGW import lib (named explicitly, zig would name it after an object) */
    CHECK(contains(m, "bin/x64-windows-zig/libx.dll"));
    CHECK(contains(m, "-Wl,--out-implib,"));
    CHECK(contains(m, "lib/x64-windows-zig/libx.dll.a"));
    CHECK_FALSE(contains(m, "lib/x64-windows-zig/libx.dll "));
    CHECK(contains(m, "cc = zig cc"));
    CHECK(contains(m, "command = $cxx $flags -MMD")); /* no launcher prefix */
    CHECK_FALSE(contains(m, "-soname"));
    CHECK_FALSE(contains(m, "ORIGIN"));
}

TEST_CASE("zig: C++20 modules are refused with a clang-pointing Config error") {
    ScratchDir scratch {"zig-mod"};
    REQUIRE(detail::write_file("src/math.cppm", "export module math;\nexport int f();\n").has_value());
    REQUIRE(detail::write_file("src/math_impl.cpp", "module math;\nint f() { return 1; }\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n"; /* the error is raised at resolve time: zig never runs */
    char* argv[] = {prog, dry};
    Project proj("zig-mod", 2, argv);
    proj.configure(Config().toolchain(Toolchain::Zig));
    proj.add_static_library("math").sources({"src/math.cppm", "src/math_impl.cpp"});
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(contains(r.error().message, "zig toolchain cannot build"));
    CHECK(contains(r.error().message, "Toolchain::LLVM"));
}

TEST_CASE("zig: modules_mode(Manual) is refused before clang-scan-deps runs") {
    ScratchDir scratch {"zig-manual"};
    REQUIRE(detail::write_file("app/main.cpp", "int main() {}\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("zig-manual", 2, argv);
    proj.configure(Config().toolchain(Toolchain::Zig));
    proj.add_executable("app").sources("app/main.cpp").modules_mode(ModulesMode::Manual);
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(contains(r.error().message, "clang-scan-deps cannot run the zig driver"));
}

TEST_CASE("zig: pch() is refused with a Config error") {
    ScratchDir scratch {"zig-pch"};
    REQUIRE(detail::write_file("app/main.cpp", "int main() {}\n").has_value());
    REQUIRE(detail::write_file("pch.hpp", "#include <vector>\n").has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("zig-pch", 2, argv);
    proj.configure(Config().toolchain(Toolchain::Zig));
    proj.add_executable("app").sources("app/main.cpp").pch("pch.hpp");
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(contains(r.error().message, "cannot build a precompiled header"));
}

/* --- integration (real clang builds) -------------------------------------------- */

TEST_CASE("static_deps: shared deps linked from a static twin") {
    ScratchDir scratch {"twin"};
    REQUIRE(detail::write_file("g/greet.cpp", "int greet() { return 42; }\n").has_value());
    REQUIRE(detail::write_file("app/main.cpp", "int greet();\nint main() { return greet() == 42 ? 0 : 1; }\n")
                .has_value());
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("twin", 1, argv);
    auto& g = proj.add_shared_library("greet").sources("g/greet.cpp");
    proj.add_executable("dyn").sources("app/main.cpp").depends_on(g);
    proj.add_executable("stat").sources("app/main.cpp").depends_on(g).static_deps();
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const std::string m = manifest_text();
    const std::string tdir = detail::triple_dir(host_triple());
    CHECK(contains(m, "lib/" + tdir + "/libgreet.so"));
    CHECK(contains(m, "/libgreet.a: archive"));

    const auto exe = std::filesystem::current_path() / "build/debug/bin" / tdir;
    auto ldd_stat = os::capture({"ldd", (exe / "stat").string()});
    REQUIRE(ldd_stat.has_value());
    CHECK_FALSE(contains(ldd_stat->output, "libgreet"));
    auto ldd_dyn = os::capture({"ldd", (exe / "dyn").string()});
    REQUIRE(ldd_dyn.has_value());
    CHECK(contains(ldd_dyn->output, "libgreet.so"));
    auto run = os::run({(exe / "stat").string()});
    REQUIRE(run.has_value());
    CHECK(run->exit_code == 0);
}

TEST_CASE("static_link: shared lib becomes an archive; Interface libs go -Bstatic") {
    ScratchDir scratch {"static-link"};
    REQUIRE(detail::write_file("g/greet.cpp", "int greet() { return 7; }\n").has_value());
    REQUIRE(detail::write_file("app/main.cpp", "int greet();\nint main() { return greet() == 7 ? 0 : 1; }\n")
                .has_value());
    char prog[] = "truss-tests";
    char dry[] = "-n";
    char* argv[] = {prog, dry};
    Project proj("static-link", 2, argv);
    auto& g = proj.add_shared_library("greet").sources("g/greet.cpp").static_link();
    CHECK(g.kind() == TargetKind::StaticLibrary);
    auto& pkg = proj.add_interface_library("pkg").link_libs({"zz_static", "m"}).static_link();
    auto& sys = proj.add_interface_library("sys").link_libs("zz_dynamic");
    proj.add_executable("app").sources("app/main.cpp").depends_on(g).depends_on(pkg).depends_on(sys);
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());
    const std::string m = manifest_text();
    CHECK(contains(m, "libgreet.a"));
    CHECK_FALSE(contains(m, "libgreet.so"));
    CHECK(contains(m, "-Wl,-Bstatic -lzz_static -Wl,-Bdynamic -lzz_dynamic -lm"));

    g.static_link(false); /* reversible */
    CHECK(g.kind() == TargetKind::SharedLibrary);
}

TEST_CASE("static_stdlib(Gcc): binary only needs the C library") {
    if (host_triple().family != CompilerFamily::Clang && host_triple().family != CompilerFamily::Gcc)
        return;
    ScratchDir scratch {"static-stdlib"};
    REQUIRE(detail::write_file("app/main.cpp",
                               "#include <string>\n#include <vector>\n"
                               "int main() { std::vector<std::string> v {\"a\"}; return v.size() == 1 ? 0 : 1; }\n")
                .has_value());
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("static-stdlib", 1, argv);
    proj.configure(Config().toolchain(Toolchain::GCC));
    proj.add_executable("app").sources("app/main.cpp").static_stdlib(StaticStdlib::Gcc);
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());
    auto exe = std::filesystem::current_path() / "build/debug/bin" /
               detail::triple_dir(*detail::apply_config(Config().toolchain(Toolchain::GCC), host_triple())) / "app";
    auto ldd = os::capture({"ldd", exe.string()});
    REQUIRE(ldd.has_value());
    CHECK_FALSE(contains(ldd->output, "libstdc++"));
    CHECK_FALSE(contains(ldd->output, "libgcc_s"));
    auto run = os::run({exe.string()});
    REQUIRE(run.has_value());
    CHECK(run->exit_code == 0);
}

/* --- review fixes -------------------------------------------------------------- */

TEST_CASE("depends_on(failed Result) is reported by build()") {
    ScratchDir scratch {"deferred"};
    REQUIRE(detail::write_file("app/main.cpp", "int main() {}\n").has_value());
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("deferred", 1, argv);
    proj.add_executable("app").sources("app/main.cpp").depends_on(proj.add_subdirectory("no/such/dir"));
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(contains(r.error().message, "depends_on()"));
    CHECK(contains(r.error().message, "not a directory"));
}

TEST_CASE("fetch: option-looking URLs and tags are refused") {
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("fetch-opts", 1, argv);
    auto r = proj.fetch("x", FetchOptions {.git = "--upload-pack=touch /tmp/pwned", .tag = "v1", .url = {}, .sha256 = {}});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    r = proj.fetch("x", FetchOptions {.git = "https://example.invalid/x.git", .tag = "..", .url = {}, .sha256 = {}});
    REQUIRE_FALSE(r.has_value());
    r = proj.fetch("x", FetchOptions {.git = {}, .tag = {}, .url = "-o/etc/passwd", .sha256 = "00"});
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("-j must be a whole positive number") {
    char prog[] = "truss-tests";
    char j[] = "-j";
    char bad[] = "4x";
    char* argv[] = {prog, j, bad};
    Project proj("jobs", 3, argv);
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
}

TEST_CASE("write_file: atomic replace leaves no temp file") {
    ScratchDir scratch {"atomic"};
    REQUIRE(detail::write_file("out.txt", "one").has_value());
    REQUIRE(detail::write_file("out.txt", "two").has_value());
    CHECK(*detail::read_file("out.txt") == "two");
    int entries = 0;
    for ([[maybe_unused]] const auto& e : std::filesystem::directory_iterator("."))
        ++entries;
    CHECK(entries == 1);
}

TEST_CASE("capture: concurrent spawns do not leak pipe ends") {
    /* each child sleeps; with inheritable pipes the fast child's reader would
     * wait for the slow sibling. Just check outputs stay separated. */
    std::array<Result<os::ProcessResult>, 4> out;
    {
        std::vector<std::jthread> pool;
        for (size_t i = 0; i < out.size(); ++i)
            pool.emplace_back([&, i] { out[i] = os::capture({"sh", "-c", "echo " + std::to_string(i)}); });
    }
    for (size_t i = 0; i < out.size(); ++i) {
        REQUIRE(out[i].has_value());
        CHECK(out[i]->output == std::to_string(i) + "\n");
    }
}

/* --- real zig end-to-end (skipped when zig is absent) ---------------------------- */

TEST_CASE("zig e2e: host static C library + executable (zig cc, zig ar)") {
    if (!os::program_exists("zig")) { MESSAGE("zig not installed, skipping"); return; }
    ScratchDir scratch {"zig-e2e-host"};
    REQUIRE(detail::write_file("lib/val.c", "int val(void) { return 42; }\n").has_value());
    REQUIRE(detail::write_file("app/main.c", "int val(void);\nint main(void) { return val() == 42 ? 0 : 1; }\n")
                .has_value());
    char prog[] = "truss-tests";
    char dbg[] = "debug";
    char* argv[] = {prog, dbg};
    Project proj("zig-e2e-host", 2, argv);
    proj.configure(Config().toolchain(Toolchain::Zig));
    auto& lib = proj.add_static_library("val").sources("lib/val.c");
    proj.add_executable("app").sources("app/main.c").depends_on(lib);
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const auto bld = std::filesystem::current_path() / "build/debug";
    CHECK(std::filesystem::exists(bld / "lib/x64-linux-zig/libval.a"));
    const auto exe = bld / "bin/x64-linux-zig/app";
    REQUIRE(std::filesystem::exists(exe));
    auto run = os::run({exe.string()});
    REQUIRE(run.has_value());
    CHECK(run->exit_code == 0);
}

TEST_CASE("zig e2e: x86_64-linux-musl static_stdlib(Musl) + static_deps runs fully static") {
    if (!os::program_exists("zig")) { MESSAGE("zig not installed, skipping"); return; }
    ScratchDir scratch {"zig-e2e-musl"};
    REQUIRE(detail::write_file("g/greet.c", "int greet(void) { return 7; }\n").has_value());
    REQUIRE(detail::write_file("app/main.cpp",
                               "#include <string>\n#include <vector>\n"
                               "extern \"C\" int greet();\n"
                               "int main() { std::vector<std::string> v {\"a\"}; return (v.size() == 1 && greet() == 7) ? 0 : 1; }\n")
                .has_value());
    char prog[] = "truss-tests";
    char dbg[] = "debug";
    char* argv[] = {prog, dbg};
    Project proj("zig-e2e-musl", 2, argv);
    proj.configure(Config().toolchain(Toolchain::Zig).target("x86_64-linux-musl"));
    auto& g = proj.add_static_library("greet").sources("g/greet.c");
    proj.add_executable("app")
        .sources("app/main.cpp")
        .depends_on(g)
        .static_deps()
        .static_stdlib(StaticStdlib::Musl);
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const auto exe = std::filesystem::current_path() / "build/debug/bin/x64-linux-musl-zig/app";
    REQUIRE(std::filesystem::exists(exe));
    auto run = os::run({exe.string()});
    REQUIRE(run.has_value());
    CHECK(run->exit_code == 0);
    auto ldd = os::capture({"ldd", exe.string()});
    if (ldd.has_value()) {
        INFO(ldd->output);
        CHECK((contains(ldd->output, "not a dynamic executable") || contains(ldd->output, "statically linked")));
    }
}

TEST_CASE("zig e2e: x86_64-windows-gnu shared library produces dll + import lib") {
    if (!os::program_exists("zig")) { MESSAGE("zig not installed, skipping"); return; }
    ScratchDir scratch {"zig-e2e-win"};
    REQUIRE(detail::write_file("lib/x.c", "int x(void) { return 1; }\n").has_value());
    char prog[] = "truss-tests";
    char dbg[] = "debug";
    char* argv[] = {prog, dbg};
    Project proj("zig-e2e-win", 2, argv);
    proj.configure(Config().toolchain(Toolchain::Zig).target("x86_64-windows-gnu").launcher("true"));
    proj.add_shared_library("x").sources("lib/x.c");
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    const auto bld = std::filesystem::current_path() / "build/debug";
    CHECK(std::filesystem::exists(bld / "bin/x64-windows-zig/libx.dll"));
    CHECK(std::filesystem::exists(bld / "lib/x64-windows-zig/libx.dll.a"));
}
