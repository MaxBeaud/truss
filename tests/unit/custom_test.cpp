// add_custom_command(): run a (usually for_host()) tool during the build and
// feed its outputs to other targets (implicit compile inputs + -I propagation),
// plus validation and cross-hygiene error cases.
module;
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
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
        const auto dir = std::filesystem::temp_directory_path() / ("truss-custom-test-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~ScratchDir() { chdir(prev.c_str()); }
};

bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

/* gen -o FILE... : writes a header defining GEN_VALUE into every FILE */
void write_gen_tool() {
    REQUIRE(detail::write_file("tools/gen.cpp",
                               "#include <cstdio>\n"
                               "int main(int argc, char** argv) {\n"
                               "    if (argc < 3) return 2;\n"
                               "    for (int i = 2; i < argc; ++i) {\n"
                               "        FILE* f = std::fopen(argv[i], \"w\");\n"
                               "        if (!f) return 1;\n"
                               "        std::fputs(\"#define GEN_VALUE 42\\n\", f);\n"
                               "        std::fclose(f);\n"
                               "    }\n"
                               "    return 0;\n"
                               "}\n")
                .has_value());
}

/* app: compiles against the generated header (found through the propagated -I) */
void write_app() {
    REQUIRE(detail::write_file("app/main.cpp",
                               "#include \"out.h\"\n"
                               "int main() { return GEN_VALUE == 42 ? 0 : 1; }\n")
                .has_value());
}

std::string read_manifest() {
    auto m = detail::read_file("build/debug/ninja/build.ninja");
    REQUIRE(m.has_value());
    return *m;
}

std::filesystem::path find_exe(const std::string& name) {
    auto hits = fs::glob("build/debug/bin/*/" + name);
    REQUIRE(hits.has_value());
    REQUIRE(hits->size() == 1);
    /* artifact paths in the manifest are absolute (build_dir is rooted at cwd) */
    return std::filesystem::current_path() / hits->front();
}

auto mtime(const std::filesystem::path& p) {
    return std::filesystem::last_write_time(p);
}

} // namespace

TEST_CASE("custom command e2e: for_host gen tool -> generated header -> app compiles, builds and runs green") {
    ScratchDir scratch {"e2e"};
    write_gen_tool();
    write_app();
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("custom-e2e", 1, argv);

    auto& gen = proj.add_executable("gen").sources("tools/gen.cpp").for_host();
    auto& out = proj.add_custom_command("generated")
                    .command({"$tool", "-o", "$out"})
                    .uses(gen)
                    .outputs({"build/debug/gen/generated/out.h"});
    /* a second command with no .outputs(): the documented default kicks in */
    proj.add_custom_command("stamp").command({"$tool", "-o", "$out"}).uses(gen);
    proj.add_executable("app").sources("app/main.cpp").depends_on(out);

    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    /* the outputs exist (custom one + the default build/debug/gen/stamp/output) */
    const std::filesystem::path header = "build/debug/gen/generated/out.h";
    CHECK(std::filesystem::exists(header));
    CHECK(std::filesystem::exists("build/debug/gen/stamp/output"));

    /* the manifest: one custom_command edge whose command line carries the
     * expanded $tool (the gen exe) and whose inputs include the tool exe */
    const std::string manifest = read_manifest();
    INFO(manifest);
    CHECK(contains(manifest, "rule custom_command"));
    const auto gen_exe = find_exe("gen").generic_string();
    CHECK(contains(manifest, "build build/debug/gen/generated/out.h: custom_command"));
    CHECK(contains(manifest, gen_exe));
    CHECK(contains(manifest, "command = " + gen_exe + " -o build/debug/gen/generated/out.h"));
    /* the app's compile edge takes the generated header as an order-only input
     * (so unrelated TUs do not recompile when codegen reruns; the depfile
     * tracks actual #includes) and its directory as an include dir */
    CHECK(contains(manifest, "app/main.cpp || build/debug/gen/generated/out.h"));
    CHECK(contains(manifest, "-I\"build/debug/gen/generated\""));

    /* the app runs green (it compiled against the generated header) */
    const auto app = find_exe("app").generic_string();
    auto run = os::run({app});
    REQUIRE(run.has_value());
    CHECK(run->exit_code == 0);
}

TEST_CASE("custom command: touching the tool source re-runs the command and recompiles the consumer") {
    ScratchDir scratch {"incremental"};
    write_gen_tool();
    write_app();
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("custom-incr", 1, argv);

    auto& gen = proj.add_executable("gen").sources("tools/gen.cpp").for_host();
    auto& out = proj.add_custom_command("generated")
                    .command({"$tool", "-o", "$out"})
                    .uses(gen)
                    .outputs({"build/debug/gen/generated/out.h"});
    proj.add_executable("app").sources("app/main.cpp").depends_on(out);

    auto r1 = proj.build();
    INFO((r1 ? "ok" : r1.error().message));
    REQUIRE(r1.has_value());

    const std::filesystem::path header = "build/debug/gen/generated/out.h";
    const auto app_exe = find_exe("app");
    const auto h1 = mtime(header);
    const auto a1 = mtime(app_exe);

    /* touch the tool source (now(), not now()+1s: outputs land strictly newer) */
    std::filesystem::last_write_time("tools/gen.cpp", std::chrono::file_clock::now());
    auto r2 = proj.build();
    INFO((r2 ? "ok" : r2.error().message));
    REQUIRE(r2.has_value());
    CHECK(mtime(header) > h1);      /* the command re-ran */
    CHECK(mtime(app_exe) > a1);     /* and the app was recompiled + relinked */

    /* a no-op rebuild changes nothing */
    const auto h2 = mtime(header);
    const auto a2 = mtime(app_exe);
    auto r3 = proj.build();
    REQUIRE(r3.has_value());
    CHECK(mtime(header) == h2);
    CHECK(mtime(app_exe) == a2);
}

TEST_CASE("custom command: multiple outputs land on one edge") {
    ScratchDir scratch {"multi"};
    write_gen_tool();
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("custom-multi", 1, argv);
    auto& gen = proj.add_executable("gen").sources("tools/gen.cpp");
    proj.add_custom_command("pair")
        .command({"$tool", "-o", "$out"})
        .uses(gen)
        .outputs({"build/debug/gen/pair/a.h", "build/debug/gen/pair/b.h"});
    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());
    CHECK(std::filesystem::exists("build/debug/gen/pair/a.h"));
    CHECK(std::filesystem::exists("build/debug/gen/pair/b.h"));
    const std::string manifest = read_manifest();
    INFO(manifest);
    CHECK(contains(manifest,
                   "build build/debug/gen/pair/a.h build/debug/gen/pair/b.h: custom_command"));
    CHECK(contains(manifest, "-o build/debug/gen/pair/a.h build/debug/gen/pair/b.h"));
}

TEST_CASE("custom command validation: empty command, non-executable uses(), $tool without uses(), unsafe output") {
    char prog[] = "truss-tests";
    {
        ScratchDir scratch {"err-empty"};
        char* argv[] = {prog};
        Project proj("err-empty", 1, argv);
        proj.add_custom_command("x").outputs({"build/debug/gen/x/o.h"});
        auto r = proj.build();
        INFO((r ? "unexpectedly ok" : r.error().message));
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
        CHECK(contains(r.error().message, "empty command"));
    }
    {
        ScratchDir scratch {"err-uses-kind"};
        write_gen_tool();
        char* argv[] = {prog};
        Project proj("err-uses-kind", 1, argv);
        auto& lib = proj.add_static_library("notatool").sources("tools/gen.cpp");
        proj.add_custom_command("x").command({"$tool", "-o", "$out"}).uses(lib);
        auto r = proj.build();
        INFO((r ? "unexpectedly ok" : r.error().message));
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
        CHECK(contains(r.error().message, "must name an executable"));
    }
    {
        ScratchDir scratch {"err-tool-missing"};
        char* argv[] = {prog};
        Project proj("err-tool-missing", 1, argv);
        proj.add_custom_command("x").command({"$tool", "-o", "$out"}).outputs({"build/debug/gen/x/o.h"});
        auto r = proj.build();
        INFO((r ? "unexpectedly ok" : r.error().message));
        REQUIRE_FALSE(r.has_value());
        CHECK(contains(r.error().message, "$tool"));
        CHECK(contains(r.error().message, ".uses(tool)"));
    }
    {
        ScratchDir scratch {"err-bad-output"};
        write_gen_tool();
        char* argv[] = {prog};
        Project proj("err-bad-output", 1, argv);
        auto& gen = proj.add_executable("gen").sources("tools/gen.cpp");
        proj.add_custom_command("x").command({"$tool", "-o", "$out"}).uses(gen).outputs({"bad out.h"});
        auto r = proj.build();
        INFO((r ? "unexpectedly ok" : r.error().message));
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
        CHECK(contains(r.error().message, "bad out.h"));
    }
    {
        ScratchDir scratch {"err-sources"};
        write_gen_tool();
        char* argv[] = {prog};
        Project proj("err-sources", 1, argv);
        proj.add_custom_command("x").command({"true"}).outputs({"o.h"}).sources("tools/gen.cpp");
        auto r = proj.build();
        INFO((r ? "unexpectedly ok" : r.error().message));
        REQUIRE_FALSE(r.has_value());
        CHECK(contains(r.error().message, "takes no sources"));
    }
}

TEST_CASE("custom command cross hygiene: a target-triple consumer may not depends_on a for_host custom command") {
    ScratchDir scratch {"cross-mismatch"};
    write_gen_tool();
    write_app();
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("cross-mismatch", 1, argv);
    proj.configure(Config().toolchain(Toolchain::Zig).target("x86_64-linux-musl"));
    auto& gen = proj.add_executable("gen").sources("tools/gen.cpp").for_host();
    /* the command itself pinned to the host: its outputs are "built for" the host */
    auto& cc = proj.add_custom_command("genout")
                   .command({"$tool", "-o", "$out"})
                   .uses(gen)
                   .outputs({"build/debug/gen/genout/out.h"})
                   .for_host();
    proj.add_executable("app").sources("app/main.cpp").depends_on(cc); /* cross -> host: error */
    auto r = proj.build();
    INFO((r ? "unexpectedly ok" : r.error().message));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(contains(r.error().message, "depends on 'genout'"));
    CHECK(contains(r.error().message, "x86_64-linux-musl"));
}

TEST_CASE("custom command cross hygiene: in a cross project the uses() tool must be for_host()") {
    ScratchDir scratch {"cross-tool"};
    write_gen_tool();
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("cross-tool", 1, argv);
    proj.configure(Config().toolchain(Toolchain::Zig).target("x86_64-linux-musl"));
    auto& gen = proj.add_executable("gen").sources("tools/gen.cpp"); /* NOT for_host: cross exe */
    proj.add_custom_command("genout").command({"$tool", "-o", "$out"}).uses(gen);
    auto r = proj.build();
    INFO((r ? "unexpectedly ok" : r.error().message));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(contains(r.error().message, "runs on the build host"));
    CHECK(contains(r.error().message, "for_host()"));
}

TEST_CASE("custom command zig e2e: in a cross build the for_host tool runs on the host and feeds the cross app") {
    if (!os::program_exists("zig")) { MESSAGE("zig not installed, skipping"); return; }
    ScratchDir scratch {"cross-e2e"};
    write_gen_tool();
    write_app();
    char prog[] = "truss-tests";
    char* argv[] = {prog};
    Project proj("custom-cross-e2e", 1, argv);
    proj.configure(Config().toolchain(Toolchain::Zig).target("x86_64-linux-musl"));

    auto& gen = proj.add_executable("gen").sources("tools/gen.cpp").for_host();
    /* the command follows the project (cross) triple; only its tool is host-side */
    auto& out = proj.add_custom_command("generated")
                    .command({"$tool", "-o", "$out"})
                    .uses(gen)
                    .outputs({"build/debug/gen/generated/out.h"});
    proj.add_executable("app").sources("app/main.cpp").depends_on(out);

    auto r = proj.build();
    INFO((r ? "ok" : r.error().message));
    REQUIRE(r.has_value());

    /* the host tool ran (its exe is a host binary) and produced the header ... */
    CHECK(std::filesystem::exists("build/debug/gen/generated/out.h"));
    const auto gen_exe = find_exe("gen").generic_string();
    CHECK(contains(gen_exe, "x64-linux-")); /* host triple dir, not the musl one */
    CHECK_FALSE(contains(gen_exe, "musl"));
    /* ... the cross app compiled against it ... */
    const auto app_exe = find_exe("app").generic_string();
    CHECK(contains(app_exe, "x64-linux-musl-zig"));
    const std::string manifest = read_manifest();
    INFO(manifest);
    CHECK(contains(manifest, "command = " + gen_exe + " -o build/debug/gen/generated/out.h"));
    /* ... and the cross exe runs natively on this x64 host (static musl) */
    auto run = os::run({app_exe});
    REQUIRE(run.has_value());
    CHECK(run->exit_code == 0);
}
