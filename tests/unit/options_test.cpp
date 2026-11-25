// User-defined build options: Options builder, Project::options/get,
// Target::options/passthrough_options, -D CLI parsing, options.cache, and
// the `options` / `configure --reset` verbs.
module;
#include <doctest/doctest.h>

#include <cstdint>
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

bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

struct ScratchDir {
    std::filesystem::path prev;
    std::filesystem::path dir;
    explicit ScratchDir(const std::string& name) {
        prev = std::filesystem::current_path();
        dir = std::filesystem::temp_directory_path() / ("truss-options-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~ScratchDir() { chdir(prev.c_str()); }
};

std::string manifest_text() {
    auto m = detail::read_file("build/debug/ninja/build.ninja");
    REQUIRE(m.has_value());
    return std::move(*m);
}

} // namespace

TEST_CASE("options: builder defaults, structured bindings, and Project::get()") {
    ScratchDir dir {"defaults"};
    std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("options")};
    Project proj {"app", argv};

    auto opts = Options {}
                    .option("with_tests", true, "Build tests")
                    .option("max_threads", 4, "Worker threads")
                    .option("backend", "vulkan", "Render backend")
                    .choice("audio", {"pipewire", "alsa", "none"}, "pipewire", "Audio driver");

    auto [with_tests, max_threads, backend, audio] = proj.options(opts);
    CHECK(with_tests == true);
    CHECK(max_threads == 4);
    CHECK(backend == "vulkan");
    CHECK(audio == "pipewire");

    CHECK(proj.get<bool>("with_tests") == true);
    CHECK(proj.get<int>("max_threads") == 4);
    CHECK(proj.get<std::string>("backend") == "vulkan");
    CHECK(proj.get<std::string>("app", "audio") == "pipewire");
    CHECK(proj.get<std::string>("app.audio") == "pipewire");
    CHECK(proj.build().has_value());
}

TEST_CASE("options: -D CLI overrides (bare bool, =val, spaced -D, scoped)") {
    ScratchDir dir {"cli-overrides"};
    std::vector<char*> argv {
        const_cast<char*>("truss"),
        const_cast<char*>("options"),
        const_cast<char*>("-Dwith_tests=false"),
        const_cast<char*>("-Denable_simd"),
        const_cast<char*>("-D"),
        const_cast<char*>("max_threads=16"),
        const_cast<char*>("-Dapp.audio=alsa"),
    };
    Project proj {"app", argv};

    auto [with_tests, enable_simd, max_threads, audio] = proj.options(
        Options {}
            .option("with_tests", true)
            .option("enable_simd", false)
            .option("max_threads", 4)
            .choice("audio", {"pipewire", "alsa", "none"}, "pipewire"));

    CHECK(with_tests == false);
    CHECK(enable_simd == true);
    CHECK(max_threads == 16);
    CHECK(audio == "alsa");
    CHECK(proj.build().has_value());
}

TEST_CASE("options: cache persistence, partial override, and configure --reset") {
    ScratchDir dir {"cache"};

    // 1. Set two options via -D and configure (persists to build/debug/options.cache).
    {
        std::vector<char*> argv {
            const_cast<char*>("truss"),
            const_cast<char*>("configure"),
            const_cast<char*>("-Dfoo=false"),
            const_cast<char*>("-Dcount=42"),
        };
        Project proj {"app", argv};
        auto [foo, count] = proj.options(Options {}.option("foo", true).option("count", 1));
        CHECK(foo == false);
        CHECK(count == 42);
        REQUIRE(proj.build().has_value());
        CHECK(std::filesystem::exists("build/debug/options.cache"));
    }

    // 2. Next invocation with only -Dfoo=true preserves count=42 from cache
    //    and removes app.foo (now back at default) from options.cache.
    {
        std::vector<char*> argv {
            const_cast<char*>("truss"),
            const_cast<char*>("configure"),
            const_cast<char*>("-Dfoo=true"),
        };
        Project proj {"app", argv};
        auto [foo, count] = proj.options(Options {}.option("foo", true).option("count", 1));
        CHECK(foo == true);
        CHECK(count == 42);
        REQUIRE(proj.build().has_value());
        auto cache = detail::read_file("build/debug/options.cache");
        REQUIRE(cache.has_value());
        CHECK_FALSE(contains(*cache, "app.foo="));
        CHECK(contains(*cache, "app.count=42"));
    }

    // 3. `configure --reset` removes options.cache; next run reverts to defaults.
    {
        std::vector<char*> argv {
            const_cast<char*>("truss"),
            const_cast<char*>("configure"),
            const_cast<char*>("--reset"),
        };
        Project proj {"app", argv};
        REQUIRE(proj.build().has_value());
        CHECK_FALSE(std::filesystem::exists("build/debug/options.cache"));
    }
    {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("options")};
        Project proj {"app", argv};
        auto [foo, count] = proj.options(Options {}.option("foo", true).option("count", 1));
        CHECK(foo == true);
        CHECK(count == 1);
        CHECK(proj.build().has_value());
    }
}

TEST_CASE("options: Target::passthrough_options and public_passthrough_options") {
    ScratchDir dir {"passthrough"};
    REQUIRE(detail::write_file("lib.cpp", "int lib_fn() { return 0; }\n").has_value());
    REQUIRE(detail::write_file("main.cpp", "int lib_fn(); int main() { return lib_fn(); }\n").has_value());

    std::vector<char*> argv {
        const_cast<char*>("truss"),
        const_cast<char*>("-n"),
        const_cast<char*>("-Daudio.AUDIO_SIMD"),
        const_cast<char*>("-Daudio.RATE=48000"),
        const_cast<char*>("-Daudio.TAG=fast"),
    };
    Project proj {"app", argv};

    auto& audio = proj.add_static_library("audio")
                      .sources("lib.cpp")
                      .passthrough_options(
                          Options {}
                              .option("AUDIO_SIMD", false)
                              .option("RATE", 44100)
                              .option("TAG", "default"))
                      .public_passthrough_options(Options {}.option("AUDIO_PUBLIC", true));

    proj.add_executable("app").sources("main.cpp").depends_on(audio);
    REQUIRE(proj.build().has_value());

    const std::string m = manifest_text();
    CHECK(contains(m, "-DAUDIO_SIMD=1"));
    CHECK(contains(m, "-DRATE=48000"));
    CHECK(contains(m, "-DAUDIO_PUBLIC=1"));
    CHECK(contains(m, "-DTAG='\"fast\"'"));
}

TEST_CASE("options: int64_t, string_view, dotted scopes, and get() errors") {
    ScratchDir dir {"extra"};

    SUBCASE("int64_t, string_view, and dotted target scope lookup") {
        std::vector<char*> argv {
            const_cast<char*>("truss"),
            const_cast<char*>("options"),
            const_cast<char*>("-Dfmt-11.0.2.big=9000000000"),
        };
        Project proj {"app", argv};
        auto& dep = proj.add_static_library("fmt-11.0.2");
        auto [big, tag] = dep.options(
            Options {}
                .option("big", std::int64_t {1})
                .option("tag", std::string_view {"v11"}));
        CHECK(big == 9000000000LL);
        CHECK(tag == "v11");
        CHECK(proj.get<std::int64_t>("fmt-11.0.2.big") == 9000000000LL);
        CHECK(proj.get<std::string>("fmt-11.0.2.tag") == "v11");
        CHECK(proj.build().has_value());
    }

    SUBCASE("get() unknown option reports Config error") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("options")};
        Project proj {"app", argv};
        (void)proj.get<int>("missing");
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
    }

    SUBCASE("get() type mismatch reports Config error") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("options")};
        Project proj {"app", argv};
        (void)proj.options(Options {}.option("backend", "vulkan"));
        (void)proj.get<int>("backend");
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
    }

    SUBCASE("passthrough_options rejects invalid C identifier") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n")};
        Project proj {"app", argv};
        proj.add_static_library("lib").passthrough_options(Options {}.option("bad-name", true));
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
    }

    SUBCASE("configure --reset rejects -D flags") {
        std::vector<char*> argv {
            const_cast<char*>("truss"),
            const_cast<char*>("configure"),
            const_cast<char*>("--reset"),
            const_cast<char*>("-Dfoo=1"),
        };
        Project proj {"app", argv};
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
    }
}

TEST_CASE("options: validation errors (unknown, type mismatch, choice, ambiguity, Interface)") {
    ScratchDir dir {"errors"};

    SUBCASE("unknown -D option") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n"), const_cast<char*>("-Dnope=1")};
        Project proj {"app", argv};
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
    }

    SUBCASE("invalid int value") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n"), const_cast<char*>("-Dn=abc")};
        Project proj {"app", argv};
        (void)proj.options(Options {}.option("n", 1));
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
    }

    SUBCASE("invalid choice value") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n"), const_cast<char*>("-Dbe=dx12")};
        Project proj {"app", argv};
        (void)proj.options(Options {}.choice("be", {"vulkan", "opengl"}, "vulkan"));
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
    }

    SUBCASE("bare -D on non-bool") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n"), const_cast<char*>("-Dn")};
        Project proj {"app", argv};
        (void)proj.options(Options {}.option("n", 1));
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
    }

    SUBCASE("ambiguous unscoped -D across targets") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n"), const_cast<char*>("-Dtests=false")};
        Project proj {"app", argv};
        auto& a = proj.add_static_library("a");
        auto& b = proj.add_static_library("b");
        (void)a.options(Options {}.option("tests", true));
        (void)b.options(Options {}.option("tests", true));
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
        CHECK(contains(r.error().message, "ambiguous"));
    }

    SUBCASE("passthrough_options rejected on Interface target") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n")};
        Project proj {"app", argv};
        proj.add_interface_library("hdr").passthrough_options(Options {}.option("FOO", true));
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
    }
}

TEST_CASE("options: `options` verb prints schema and exits cleanly") {
    ScratchDir dir {"verb"};
    std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("options")};
    Project proj {"app", argv};
    (void)proj.options(Options {}.option("with_tests", true, "Build tests"));
    CHECK(proj.build().has_value());
}
