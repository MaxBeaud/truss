// imported — example: consume a prebuilt third-party-style archive with
// proj.import_library(...).import_archive(...), wired in with a deref-free
// depends_on() plus system_include_dirs() for its headers.
//
// Compile (from the repo root):  ./setup.sh examples
// Run (from examples/imported):  ./truss run | ./truss test
import truss;

#include <cstdio>
#include <string>

// Step 1: produce the "prebuilt" library, standing in for an SDK/vendor drop.
// import_archive() files must exist by build() time — truss links them verbatim
// and never builds them — so the script makes the archive up front with the
// scripting helpers.
static int make_prebuilt() {
    const auto cc = truss::os::find_program({"clang++", "g++", "cc"});
    const auto ar = truss::os::find_program({"llvm-ar", "ar"});
    if (!cc || !ar) {
        std::fprintf(stderr, "imported: need a C++ compiler and ar on PATH to make the prebuilt lib\n");
        return 1;
    }
    const std::string cxx = cc->generic_string();
    const std::string arp = ar->generic_string();
    auto c = truss::os::run({cxx, "-O2", "-Iprebuilt/include", "-c", "prebuilt/greet.cpp",
                             "-o", "prebuilt/greet.o"});
    if (!c || c->exit_code != 0) {
        std::fprintf(stderr, "imported: compiling prebuilt/greet.cpp failed\n");
        return 1;
    }
    auto a = truss::os::run({arp, "rcs", "prebuilt/libgreet.a", "prebuilt/greet.o"});
    if (!a || a->exit_code != 0) {
        std::fprintf(stderr, "imported: archiving prebuilt/libgreet.a failed\n");
        return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    if (int rc = make_prebuilt(); rc != 0)
        return rc;

    truss::Project proj("imported", argc, argv);

    // Step 2: wrap the prebuilt archive as an imported library. Its headers go
    // in via system_include_dirs() (-isystem): consumers get them, and warnings
    // inside them never trip a consumer's -Werror.
    auto& greet = proj.import_library("greet")
                      .import_archive("prebuilt/libgreet.a")
                      .system_include_dirs("prebuilt/include");

    // Consumers depends_on() the imported library directly (no deref): they get
    // the -isystem headers and the archive on their link line.
    proj.add_executable("app")
        .sources_in("src/app")
        .depends_on(greet);

    proj.add_executable("test_greet")
        .sources_in("tests")
        .depends_on(greet)
        .is_test();

    auto result = proj.build();
    return result ? 0 : 1;
}
