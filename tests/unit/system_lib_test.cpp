// Unit tests for proj.system_lib() and DepChain (.fallback() / .required() / operator bool).
module;
#include <doctest/doctest.h>

#include <cstdlib>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

module truss;
import :api;
import :impl;

using namespace truss;

namespace {

bool sl_contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

struct SlScratchDir {
    std::filesystem::path prev;
    std::filesystem::path dir;
    explicit SlScratchDir(const std::string& name) {
        prev = std::filesystem::current_path();
        dir = std::filesystem::temp_directory_path() / ("truss-syslib-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~SlScratchDir() { chdir(prev.c_str()); }
};

struct EnvGuard {
    std::string key;
    std::optional<std::string> prev;
    EnvGuard(std::string k, const std::string& val) : key(std::move(k)) {
        if (const char* old = std::getenv(key.c_str()))
            prev = old;
        ::setenv(key.c_str(), val.c_str(), 1);
    }
    ~EnvGuard() {
        if (prev)
            ::setenv(key.c_str(), prev->c_str(), 1);
        else
            ::unsetenv(key.c_str());
    }
};

} // namespace

TEST_CASE("system_lib: manual headers/libs/paths probing") {
    SlScratchDir dir {"manual"};
    REQUIRE(detail::write_file("fake_sys/include/foo.h", "#pragma once\nint foo_val();\n").has_value());
    REQUIRE(detail::write_file("fake_sys/lib/libfoo.a", "!<arch>\n").has_value());
    REQUIRE(detail::write_file("main.cpp", "int main() { return 0; }\n").has_value());

    std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n")};
    Project proj {"app", argv};

    auto foo = proj.system_lib("foo")
                   .pkg_config(false)
                   .headers({"foo.h"})
                   .libs({"foo"})
                   .paths({(dir.dir / "fake_sys").generic_string()});

    REQUIRE(static_cast<bool>(foo));
    CHECK(foo.has_value());

    proj.add_executable("app").sources("main.cpp").depends_on(foo);
    REQUIRE(proj.build().has_value());

    auto m = detail::read_file("build/debug/ninja/build.ninja");
    REQUIRE(m.has_value());
    CHECK(sl_contains(*m, "fake_sys/include"));
    CHECK(sl_contains(*m, "fake_sys/lib"));
    CHECK(sl_contains(*m, "-lfoo"));
}

TEST_CASE("system_lib: pkg-config probing with PKG_CONFIG_PATH and min_version") {
    if (!os::program_exists("pkg-config")) {
        MESSAGE("pkg-config not installed; skipping pkg-config test");
        return;
    }

    SlScratchDir dir {"pkgconfig"};
    const std::string pc_dir = (dir.dir / "pc").generic_string();
    const std::string inc_dir = (dir.dir / "inc").generic_string();
    const std::string lib_dir = (dir.dir / "lib").generic_string();
    std::filesystem::create_directories(pc_dir);
    std::filesystem::create_directories(inc_dir);
    std::filesystem::create_directories(lib_dir);

    const std::string pc_file =
        "prefix=" + dir.dir.generic_string() + "\n"
        "Name: demo_pkg\n"
        "Description: Demo\n"
        "Version: 2.3.0\n"
        "Cflags: -I" + inc_dir + " -DDEMO_PKG=1\n"
        "Libs: -L" + lib_dir + " -ldemo\n";
    REQUIRE(detail::write_file(dir.dir / "pc/demo_pkg.pc", pc_file).has_value());
    REQUIRE(detail::write_file("main.cpp", "int main() { return 0; }\n").has_value());

    EnvGuard guard("PKG_CONFIG_PATH", pc_dir);

    std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n")};
    Project proj {"app", argv};

    auto ok = proj.system_lib("demo_pkg").min_version("2.0.0");
    REQUIRE(static_cast<bool>(ok));

    auto too_old = proj.system_lib("demo_pkg").min_version("3.0.0");
    CHECK_FALSE(static_cast<bool>(too_old));

    proj.add_executable("app").sources("main.cpp").depends_on(ok);
    REQUIRE(proj.build().has_value());

    auto m = detail::read_file("build/debug/ninja/build.ninja");
    REQUIRE(m.has_value());
    CHECK(sl_contains(*m, inc_dir));
    CHECK(sl_contains(*m, "-DDEMO_PKG=1"));
    CHECK(sl_contains(*m, "-ldemo"));
}

TEST_CASE("system_lib: distinct specs for same name do not collide and unsafe pkg-config tokens are rejected") {
    SlScratchDir dir {"sec-and-spec"};
    REQUIRE(detail::write_file("sys_a/include/a.h", "#pragma once\n").has_value());
    REQUIRE(detail::write_file("sys_b/include/b.h", "#pragma once\n").has_value());
    REQUIRE(detail::write_file("main.cpp", "int main() { return 0; }\n").has_value());

    SUBCASE("different specs for same library name produce distinct targets") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n")};
        Project proj {"app", argv};

        auto a = proj.system_lib("mylib").pkg_config(false).headers("a.h").paths((dir.dir / "sys_a").generic_string());
        auto b = proj.system_lib("mylib").pkg_config(false).headers("b.h").paths((dir.dir / "sys_b").generic_string());
        REQUIRE(static_cast<bool>(a));
        REQUIRE(static_cast<bool>(b));
        CHECK(a.result()->get().name() != b.result()->get().name());
    }

    if (os::program_exists("pkg-config")) {
        SUBCASE("unsafe shell metacharacters from pkg-config are rejected") {
            const std::string pc_dir = (dir.dir / "bad_pc").generic_string();
            std::filesystem::create_directories(pc_dir);
            const std::string bad_pc =
                "Name: evil\n"
                "Description: Evil\n"
                "Version: 1.0.0\n"
                "Cflags: -DFOO=$(id)\n"
                "Libs: -levil\n";
            REQUIRE(detail::write_file(dir.dir / "bad_pc/evil.pc", bad_pc).has_value());
            EnvGuard guard("PKG_CONFIG_PATH", pc_dir);

            std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n")};
            Project proj {"app", argv};
            auto r = proj.system_lib("evil").result();
            REQUIRE_FALSE(r.has_value());
            CHECK(r.error().kind == Error::Kind::Config);
            CHECK(sl_contains(r.error().message, "unsafe"));
        }
    }
}

TEST_CASE("system_lib: direct & lazy fallback chaining, short-circuiting, and required()") {
    SlScratchDir dir {"fallback"};
    REQUIRE(detail::write_file("fb/include/bar.h", "#pragma once\n").has_value());
    REQUIRE(detail::write_file("fb/lib/libbar.a", "!<arch>\n").has_value());
    REQUIRE(detail::write_file("main.cpp", "int main() { return 0; }\n").has_value());

    SUBCASE("direct fallback succeeds and lazy fallback short-circuits") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n")};
        Project proj {"app", argv};

        bool lazy_called = false;
        auto bar = proj.system_lib("bar")
                       .pkg_config(false)
                       .headers("bar.h")
                       .libs("bar")
                       .paths((dir.dir / "fb").generic_string());

        auto dep = proj.system_lib("truss_nonexistent_pkg_xyz")
                       .fallback(bar)
                       .fallback([&]() -> Result<TargetRef> {
                           lazy_called = true;
                           return std::unexpected(Error {Error::Kind::Config, "should not run"});
                       })
                       .required();

        CHECK(static_cast<bool>(dep));
        CHECK_FALSE(lazy_called);
        proj.add_executable("app").sources("main.cpp").depends_on(dep);
        CHECK(proj.build().has_value());
    }

    SUBCASE("lazy fallback runs only when primary fails and chains multiple times") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n")};
        Project proj {"app", argv};

        auto& local_iface = proj.add_interface_library("local_fb").defines("FROM_LOCAL_FB");
        int calls = 0;

        auto dep = proj.system_lib("truss_nonexistent_pkg_1")
                       .fallback([&]() -> Result<TargetRef> {
                           ++calls;
                           return std::unexpected(Error {Error::Kind::Config, "step 2 failed"});
                       })
                       .fallback([&]() -> const Target& {
                           ++calls;
                           return local_iface;
                       });

        CHECK(calls == 2);
        CHECK(static_cast<bool>(dep));
        proj.add_executable("app").sources("main.cpp").depends_on(dep);
        REQUIRE(proj.build().has_value());
        auto m = detail::read_file("build/debug/ninja/build.ninja");
        REQUIRE(m.has_value());
        CHECK(sl_contains(*m, "-DFROM_LOCAL_FB"));
    }

    SUBCASE("required() reports all tried steps when every step fails") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n")};
        Project proj {"app", argv};

        auto dep = proj.system_lib("truss_missing_a")
                       .fallback(proj.system_lib("truss_missing_b"))
                       .fallback([]() -> Result<TargetRef> {
                           return std::unexpected(Error {Error::Kind::Fetch, "fetch failed"});
                       })
                       .required();

        CHECK_FALSE(static_cast<bool>(dep));
        Result<TargetRef> res = dep;
        REQUIRE_FALSE(res.has_value());
        CHECK(res.error().kind == Error::Kind::Config);
        CHECK(sl_contains(res.error().message, "truss_missing_a"));
        CHECK(sl_contains(res.error().message, "truss_missing_b"));
        CHECK(sl_contains(res.error().message, "fetch failed"));

        auto build_res = proj.build();
        REQUIRE_FALSE(build_res.has_value());
        CHECK(build_res.error().kind == Error::Kind::Config);
    }

    SUBCASE("proj.require() wraps any Result<TargetRef> or Target into DepChain") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n")};
        Project proj {"app", argv};
        auto& fallback_tgt = proj.add_interface_library("fb_tgt").defines("FROM_REQ_FB");

        auto dep = proj.require(Result<TargetRef>(std::unexpected(Error {Error::Kind::Fetch, "primary down"})))
                       .fallback(fallback_tgt);
        CHECK(static_cast<bool>(dep));
        proj.add_executable("app").sources("main.cpp").depends_on(dep);
        CHECK(proj.build().has_value());
    }
}
