// Regression tests for review findings: stale archive members, samurai signal
// restoration + fd leak, options.cache scope validation, int option overflow,
// test --target filtering, depends_on cycle detection, uninstall prefix
// containment, and dot-name rejection.
module;
#include <doctest/doctest.h>

#include <csignal>
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

bool hr_contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

struct HrScratchDir {
    std::filesystem::path prev;
    std::filesystem::path dir;
    explicit HrScratchDir(const std::string& name) {
        prev = std::filesystem::current_path();
        dir = std::filesystem::temp_directory_path() / ("truss-hardening-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~HrScratchDir() { chdir(prev.c_str()); }
};

std::size_t count_open_fds() {
    std::size_t count = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd", ec)) {
        (void)entry;
        ++count;
    }
    return count;
}

} // namespace

TEST_CASE("hardening: stale archive members are purged when a source is removed") {
    HrScratchDir dir {"stale-ar"};
    REQUIRE(detail::write_file("src/a.cpp", "int a_fn() { return 1; }\n").has_value());
    REQUIRE(detail::write_file("src/b.cpp", "int b_fn() { return 2; }\n").has_value());

    // 1. Initial build with both a.cpp and b.cpp
    {
        std::vector<char*> argv {const_cast<char*>("truss")};
        Project proj {"demo", argv};
        proj.add_static_library("core").sources({"src/a.cpp", "src/b.cpp"});
        REQUIRE(proj.build().has_value());
    }

    // 2. Remove b.cpp from the library and rebuild (no clean)
    {
        std::vector<char*> argv {const_cast<char*>("truss")};
        Project proj {"demo", argv};
        proj.add_static_library("core").sources("src/a.cpp");
        REQUIRE(proj.build().has_value());
    }

    // Verify archive listing via `ar t` has a.cpp.o and NOT b.cpp.o
    std::filesystem::path archive_path;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir.dir / "build/debug/lib")) {
        if (entry.path().filename() == "libcore.a") {
            archive_path = entry.path();
            break;
        }
    }
    REQUIRE_FALSE(archive_path.empty());
    auto ar_list = detail::run_process(std::vector<std::string> {"ar", "t", archive_path.generic_string()}, true, 30);
    REQUIRE(ar_list.has_value());
    REQUIRE(ar_list->exit_code == 0);
    CHECK(hr_contains(ar_list->output, "a.cpp.o"));
    CHECK_FALSE(hr_contains(ar_list->output, "b.cpp.o"));
}

TEST_CASE("hardening: samurai restores signal handlers and does not leak sigfd fds") {
    HrScratchDir dir {"signals"};
    REQUIRE(detail::write_file("main.cpp", "int main() { return 0; }\n").has_value());

    struct sigaction before {};
    REQUIRE(::sigaction(SIGINT, nullptr, &before) == 0);
    const std::size_t fds_before = count_open_fds();

    {
        std::vector<char*> argv {const_cast<char*>("truss")};
        Project proj {"demo", argv};
        proj.add_executable("app").sources("main.cpp");
        REQUIRE(proj.build().has_value());
    }

    struct sigaction after {};
    REQUIRE(::sigaction(SIGINT, nullptr, &after) == 0);
    const std::size_t fds_after = count_open_fds();

    CHECK(before.sa_handler == after.sa_handler);
    CHECK(fds_after == fds_before);
}

TEST_CASE("hardening: options.cache rejects invalid scope and does not save before validate()") {
    HrScratchDir dir {"opt-scope"};
    REQUIRE(detail::write_file("main.cpp", "int main() { return 0; }\n").has_value());

    std::vector<char*> argv {const_cast<char*>("truss")};
    Project proj {"demo", argv};
    auto& bad = proj.add_executable("evil\nflag").sources("main.cpp");
    (void)bad.options(Options {}.option("opt", true));
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK_FALSE(std::filesystem::exists(dir.dir / "build/debug/options.cache"));
}

TEST_CASE("hardening: out-of-range -D integer option reports Config error instead of 0") {
    HrScratchDir dir {"opt-overflow"};

    std::vector<char*> argv {
        const_cast<char*>("truss"),
        const_cast<char*>("options"),
        const_cast<char*>("-Dcount=9999999999999"),
    };
    Project proj {"demo", argv};
    auto [count] = proj.options(Options {}.option("count", 4));
    (void)count;
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(hr_contains(r.error().message, "out of range"));
}

TEST_CASE("hardening: test --target builds and runs only the selected test") {
    HrScratchDir dir {"test-target"};
    REQUIRE(detail::write_file("pass.cpp", "int main() { return 0; }\n").has_value());
    REQUIRE(detail::write_file("fail.cpp", "int main() { return 1; }\n").has_value());

    std::vector<char*> argv {
        const_cast<char*>("truss"),
        const_cast<char*>("test"),
        const_cast<char*>("--target"),
        const_cast<char*>("unit_pass"),
    };
    Project proj {"demo", argv};
    proj.add_executable("unit_pass").sources("pass.cpp").is_test();
    proj.add_executable("unit_fail").sources("fail.cpp").is_test();
    REQUIRE(proj.build().has_value());
}

TEST_CASE("hardening: depends_on() cycle is caught as a Config error at configure time") {
    HrScratchDir dir {"dep-cycle"};
    REQUIRE(detail::write_file("a.cpp", "int a() { return 1; }\n").has_value());
    REQUIRE(detail::write_file("b.cpp", "int b() { return 2; }\n").has_value());

    std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n")};
    Project proj {"demo", argv};
    auto& a = proj.add_static_library("liba").sources("a.cpp");
    auto& b = proj.add_static_library("libb").sources("b.cpp");
    a.depends_on(b);
    b.depends_on(a);
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(hr_contains(r.error().message, "cycle"));
}

TEST_CASE("hardening: uninstall refuses manifest entries outside install prefix") {
    HrScratchDir dir {"uninstall-contain"};
    const std::filesystem::path outside = dir.dir / "victim.txt";
    REQUIRE(detail::write_file(outside, "keep me\n").has_value());

    const std::filesystem::path prefix = dir.dir / "prefix";
    const std::filesystem::path manifest = dir.dir / "build/debug/install_manifest.txt";
    REQUIRE(detail::write_file(
                manifest,
                "# prefix=" + prefix.generic_string() + "\n" + outside.generic_string() + "\n")
                .has_value());

    std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("uninstall")};
    Project proj {"demo", argv};
    auto r = proj.build();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == Error::Kind::Config);
    CHECK(std::filesystem::exists(outside));
}

TEST_CASE("hardening: dot and dot-dot target names are rejected as Config errors") {
    HrScratchDir dir {"dot-names"};
    REQUIRE(detail::write_file("main.cpp", "int main() { return 0; }\n").has_value());

    for (const char* bad : {".", ".."}) {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n")};
        Project proj {"demo", argv};
        proj.add_executable(bad).sources("main.cpp");
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
    }
}
