// Unit tests for proj.defaults() and proj.preset() / --preset / presets.
module;
#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <unistd.h>

module truss;
import :api;
import :impl;

using namespace truss;

namespace {

bool dp_contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

struct DpScratchDir {
    std::filesystem::path prev;
    std::filesystem::path dir;
    explicit DpScratchDir(const std::string& name) {
        prev = std::filesystem::current_path();
        dir = std::filesystem::temp_directory_path() / ("truss-defaults-preset-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~DpScratchDir() { chdir(prev.c_str()); }
};

std::string dp_manifest_text() {
    auto m = detail::read_file("build/debug/ninja/build.ninja");
    REQUIRE(m.has_value());
    return std::move(*m);
}

} // namespace

TEST_CASE("defaults: targets inherit proj.defaults() and per-target overrides win") {
    DpScratchDir dir {"inherit"};
    REQUIRE(detail::write_file("a.cpp", "int main() { return 0; }\n").has_value());
    REQUIRE(detail::write_file("b.cpp", "int main() { return 0; }\n").has_value());
    REQUIRE(detail::write_file("c.c", "int f(void) { return 0; }\n").has_value());

    std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n")};
    Project proj {"app", argv};

    // Target created BEFORE proj.defaults() does not inherit later defaults.
    auto& before = proj.add_executable("before").sources("a.cpp");

    proj.defaults()
        .cpp_standard(CppStandard::Cpp20)
        .c_standard(CStandard::C11)
        .warnings(Warnings::Pedantic)
        .debug_info(DebugInfo::Minimal)
        .optimization(Optimization::O2)
        .sanitizer(Sanitizer::Ub)
        .lto(Lto::Thin)
        .harden(Hardening::None)
        .pic(true)
        .symbols_hidden(true)
        .modules_mode(ModulesMode::Off);

    auto& inherited = proj.add_executable("inherited").sources({"b.cpp", "c.c"});
    auto& overridden = proj.add_executable("overridden")
                           .sources("a.cpp")
                           .cpp_standard(CppStandard::Cpp23)
                           .warnings(Warnings::None)
                           .optimization(Optimization::None)
                           .sanitizer(Sanitizer::None)
                           .lto(Lto::None)
                           .symbols_hidden(false)
                           .pic(false);

    CHECK(before.cpp_standard() == CppStandard::Default);
    CHECK(before.warnings() == Warnings::Default);
    CHECK(static_cast<const Target&>(before).symbols_hidden() == false);

    CHECK(inherited.cpp_standard() == CppStandard::Cpp20);
    CHECK(inherited.c_standard() == CStandard::C11);
    CHECK(inherited.warnings() == Warnings::Pedantic);
    CHECK(inherited.debug_info() == DebugInfo::Minimal);
    CHECK(inherited.optimization() == Optimization::O2);
    CHECK(inherited.sanitizer() == Sanitizer::Ub);
    CHECK(inherited.lto() == Lto::Thin);
    CHECK(inherited.hardening() == Hardening::None);
    CHECK(inherited.pic() == true);
    CHECK(static_cast<const Target&>(inherited).symbols_hidden() == true);
    CHECK(inherited.modules_mode() == ModulesMode::Off);

    CHECK(overridden.cpp_standard() == CppStandard::Cpp23);
    CHECK(overridden.warnings() == Warnings::None);
    CHECK(overridden.optimization() == Optimization::None);
    CHECK(overridden.sanitizer() == Sanitizer::None);
    CHECK(overridden.lto() == Lto::None);
    CHECK(static_cast<const Target&>(overridden).symbols_hidden() == false);
    CHECK(overridden.pic() == false);

    REQUIRE(proj.build().has_value());
    const std::string m = dp_manifest_text();
    CHECK(dp_contains(m, "-std=c++20"));
    CHECK(dp_contains(m, "-std=c11"));
    CHECK(dp_contains(m, "-Wpedantic"));
    CHECK(dp_contains(m, "-fvisibility=hidden"));
}

TEST_CASE("preset: selection, Config override, option precedence, and presets verb") {
    DpScratchDir dir {"preset-basic"};
    REQUIRE(detail::write_file("main.cpp", "int main() { return 0; }\n").has_value());

    SUBCASE("preset applies Config and options without polluting options.cache") {
        std::vector<char*> argv {
            const_cast<char*>("truss"),
            const_cast<char*>("-n"),
            const_cast<char*>("--preset"),
            const_cast<char*>("gcc-fast"),
        };
        Project proj {"app", argv};

        std::vector<std::string> fast_opts {"with_tests=false", "threads=8"};
        proj.preset("gcc-fast", Config {}.toolchain(Toolchain::GCC), fast_opts);
        proj.preset("minimal", {"with_tests=false"});

        auto [with_tests, threads] = proj.options(
            Options {}.option("with_tests", true).option("threads", 4));
        CHECK(with_tests == false);
        CHECK(threads == 8);

        proj.add_executable("app").sources("main.cpp");
        REQUIRE(proj.build().has_value());

        const std::string m = dp_manifest_text();
        CHECK(dp_contains(m, "cxx = g++"));
        CHECK_FALSE(std::filesystem::exists("build/debug/options.cache"));
    }

    SUBCASE("precedence: CLI -D > options.cache > preset > default") {
        // Seed options.cache with threads=12
        {
            std::vector<char*> argv {
                const_cast<char*>("truss"),
                const_cast<char*>("configure"),
                const_cast<char*>("-Dthreads=12"),
            };
            Project proj {"app", argv};
            (void)proj.options(Options {}.option("with_tests", true).option("threads", 4).option("tag", "def"));
            REQUIRE(proj.build().has_value());
        }

        // Run with --preset setting all three, plus -Dtag=cli
        {
            std::vector<char*> argv {
                const_cast<char*>("truss"),
                const_cast<char*>("options"),
                const_cast<char*>("--preset"),
                const_cast<char*>("custom"),
                const_cast<char*>("-Dtag=cli"),
            };
            Project proj {"app", argv};
            proj.preset("custom", {"with_tests=false", "threads=2", "tag=preset"});

            auto [with_tests, threads, tag] = proj.options(
                Options {}.option("with_tests", true).option("threads", 4).option("tag", "def"));
            CHECK(with_tests == false);  // from preset (> default)
            CHECK(threads == 12);        // from options.cache (> preset)
            CHECK(tag == "cli");         // from CLI -D (> cache & preset)
            REQUIRE(proj.build().has_value());
        }
    }

    SUBCASE("unknown --preset returns Config error") {
        std::vector<char*> argv {
            const_cast<char*>("truss"),
            const_cast<char*>("-n"),
            const_cast<char*>("--preset"),
            const_cast<char*>("nonexistent"),
        };
        Project proj {"app", argv};
        proj.preset("known", {"a=1"});
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
        CHECK(dp_contains(r.error().message, "unknown preset"));
    }

    SUBCASE("presets verb lists registered presets cleanly") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("presets")};
        Project proj {"app", argv};
        proj.preset("asan", Config {}.toolchain(Toolchain::LLVM), {"with_tests=true"});
        proj.preset("min", {"with_tests=false"});
        CHECK(proj.build().has_value());
    }
}
