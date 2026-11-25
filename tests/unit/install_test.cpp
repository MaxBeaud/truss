// Unit tests for first-class Unix install() & uninstall.
module;
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

module truss;
import :api;
import :impl;

using namespace truss;

namespace {

bool inst_contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

struct InstScratchDir {
    std::filesystem::path prev;
    std::filesystem::path dir;
    explicit InstScratchDir(const std::string& name) {
        prev = std::filesystem::current_path();
        dir = std::filesystem::temp_directory_path() / ("truss-install-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(chdir(dir.c_str()) == 0);
    }
    ~InstScratchDir() { chdir(prev.c_str()); }
};

struct InstEnvGuard {
    std::string key;
    std::optional<std::string> prev;
    InstEnvGuard(std::string k, const std::string& val) : key(std::move(k)) {
        if (const char* old = std::getenv(key.c_str()))
            prev = old;
        ::setenv(key.c_str(), val.c_str(), 1);
    }
    ~InstEnvGuard() {
        if (prev)
            ::setenv(key.c_str(), prev->c_str(), 1);
        else
            ::unsetenv(key.c_str());
    }
};

mode_t file_mode(const std::filesystem::path& p) {
    struct stat st {};
    REQUIRE(::stat(p.c_str(), &st) == 0);
    return st.st_mode & 0777;
}

} // namespace

TEST_CASE("install: --prefix, shared lib + executable RPATH execution, headers, manifest, and uninstall") {
    InstScratchDir dir {"e2e"};
    REQUIRE(detail::write_file("include/greeter.hpp", "#pragma once\nint greet_value();\n").has_value());
    REQUIRE(detail::write_file("src/greeter.cpp", "#include \"greeter.hpp\"\nint greet_value() { return 42; }\n").has_value());
    REQUIRE(detail::write_file("src/static_helper.cpp", "int helper_val() { return 7; }\n").has_value());
    REQUIRE(detail::write_file(
        "src/main.cpp",
        "#include \"greeter.hpp\"\n"
        "int helper_val();\n"
        "int main() { return (greet_value() + helper_val()) == 49 ? 0 : 1; }\n").has_value());

    const std::filesystem::path prefix = dir.dir / "staged_prefix";

    // 1. Build and install with --prefix
    {
        std::string prefix_str = prefix.generic_string();
        std::vector<char*> argv {
            const_cast<char*>("truss"),
            const_cast<char*>("install"),
            const_cast<char*>("--prefix"),
            prefix_str.data(),
        };
        Project proj {"demo", argv};
        auto& sh = proj.add_shared_library("greeter")
                       .sources("src/greeter.cpp")
                       .include_dirs("include")
                       .install()
                       .install_headers("include", "demo");
        auto& st = proj.add_static_library("helper")
                       .sources("src/static_helper.cpp")
                       .install();
        proj.add_executable("demo-cli")
            .sources("src/main.cpp")
            .depends_on(sh)
            .depends_on(st)
            .install();

        REQUIRE(proj.build().has_value());
    }

    const std::filesystem::path bin = prefix / "bin/demo-cli";
    const std::filesystem::path so = prefix / "lib/libgreeter.so";
    const std::filesystem::path ar = prefix / "lib/libhelper.a";
    const std::filesystem::path hdr = prefix / "include/demo/greeter.hpp";
    const std::filesystem::path manifest = dir.dir / "build/debug/install_manifest.txt";

    REQUIRE(std::filesystem::exists(bin));
    REQUIRE(std::filesystem::exists(so));
    REQUIRE(std::filesystem::exists(ar));
    REQUIRE(std::filesystem::exists(hdr));
    REQUIRE(std::filesystem::exists(manifest));

    CHECK(file_mode(bin) == 0755);
    CHECK(file_mode(so) == 0755);
    CHECK(file_mode(ar) == 0644);
    CHECK(file_mode(hdr) == 0644);

    // 2. Delete build artifacts (except manifest) to prove installed binary
    //    resolves libgreeter.so via $ORIGIN/../lib without build/!
    std::filesystem::remove_all(dir.dir / "build/debug/lib");
    std::filesystem::remove_all(dir.dir / "build/debug/bin");
    auto run_res = detail::run_process(std::vector<std::string> {bin.generic_string()}, true, 30);
    REQUIRE(run_res.has_value());
    CHECK(run_res->exit_code == 0);

    // 3. Verify install_manifest.txt lists all installed files
    auto mtxt = detail::read_file(manifest);
    REQUIRE(mtxt.has_value());
    CHECK(inst_contains(*mtxt, bin.generic_string()));
    CHECK(inst_contains(*mtxt, so.generic_string()));
    CHECK(inst_contains(*mtxt, ar.generic_string()));
    CHECK(inst_contains(*mtxt, hdr.generic_string()));

    // 4. Run `./truss uninstall` and verify files + manifest are removed
    {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("uninstall")};
        Project proj {"demo", argv};
        REQUIRE(proj.build().has_value());
    }
    CHECK_FALSE(std::filesystem::exists(bin));
    CHECK_FALSE(std::filesystem::exists(so));
    CHECK_FALSE(std::filesystem::exists(ar));
    CHECK_FALSE(std::filesystem::exists(hdr));
    CHECK_FALSE(std::filesystem::exists(manifest));
}

TEST_CASE("install: DESTDIR staging, --strip, custom install_dir, and validation errors") {
    InstScratchDir dir {"destdir"};
    REQUIRE(detail::write_file("main.cpp", "int main() { return 0; }\n").has_value());

    SUBCASE("DESTDIR + --prefix /usr + --strip + custom install_dir") {
        const std::filesystem::path stage = dir.dir / "stage";
        InstEnvGuard guard("DESTDIR", stage.generic_string());

        std::vector<char*> argv {
            const_cast<char*>("truss"),
            const_cast<char*>("install"),
            const_cast<char*>("--prefix"),
            const_cast<char*>("/usr"),
            const_cast<char*>("--strip"),
        };
        Project proj {"demo", argv};
        proj.add_executable("tool").sources("main.cpp").install().install_dir("libexec/demo");

        if (!os::find_program({"llvm-strip", "strip"}).has_value()) {
            MESSAGE("strip not found; skipping --strip subcase");
            return;
        }
        REQUIRE(proj.build().has_value());
        CHECK(std::filesystem::exists(stage / "usr/libexec/demo/tool"));
    }

    SUBCASE("install() on Interface target is a Config error") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("-n")};
        Project proj {"demo", argv};
        proj.add_interface_library("hdr").install();
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
    }

    SUBCASE("uninstall without manifest is a Config error") {
        std::vector<char*> argv {const_cast<char*>("truss"), const_cast<char*>("uninstall")};
        Project proj {"demo", argv};
        auto r = proj.build();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == Error::Kind::Config);
    }

    SUBCASE("install_headers skips symlinks") {
        REQUIRE(detail::write_file("inc/real.hpp", "#pragma once\n").has_value());
        REQUIRE(detail::write_file("secret.txt", "TOP_SECRET\n").has_value());
        std::error_code ec;
        std::filesystem::create_symlink(dir.dir / "secret.txt", dir.dir / "inc/leak.hpp", ec);
        if (ec) {
            MESSAGE("symlinks unsupported; skipping symlink test");
            return;
        }
        const std::filesystem::path prefix = dir.dir / "pfx";
        std::string pfx = prefix.generic_string();
        std::vector<char*> argv {
            const_cast<char*>("truss"),
            const_cast<char*>("install"),
            const_cast<char*>("--prefix"),
            pfx.data(),
        };
        Project proj {"demo", argv};
        proj.add_interface_library("hdrs").install_headers("inc", "demo");
        REQUIRE(proj.build().has_value());
        CHECK(std::filesystem::exists(prefix / "include/demo/real.hpp"));
        CHECK_FALSE(std::filesystem::exists(prefix / "include/demo/leak.hpp"));
    }
}
