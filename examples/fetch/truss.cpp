// fetch — example: CPM-style fetched dependencies.
//
// tl-expected arrives via git tag (header-only -> Interface target),
// xxhash arrives via sha256-verified tarball (root-layout C lib -> static lib).
// Both are cached in ~/.cache/truss and pinned in ./truss.lock.
import truss;

#include <cstdio>

static int fail(const truss::Error& e) {
    std::fprintf(stderr, "truss: %s\n", e.message.c_str());
    return 1;
}

int main(int argc, char** argv) {
    truss::Project proj("fetch", argc, argv);

    auto expected = proj.fetch("tl-expected", truss::FetchOptions {
        .git = "https://github.com/TartanLlama/expected",
        .tag = "v1.1.0",
    });
    if (!expected)
        return fail(expected.error());
    auto expected_t = proj.add_subdirectory(*expected);
    if (!expected_t)
        return fail(expected_t.error());

    auto xxh = proj.fetch("xxhash", truss::FetchOptions {
        .url = "https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.2.tar.gz",
        .sha256 = "baee0c6afd4f03165de7a4e67988d16f0f2b257b51d0e3cb91909302a26a79c4",
    });
    if (!xxh)
        return fail(xxh.error());
    auto xxh_t = proj.add_subdirectory(*xxh);
    if (!xxh_t)
        return fail(xxh_t.error());

    proj.add_executable("app")
        .sources("app/main.cpp")
        .depends_on(**xxh_t)
        .depends_on(**expected_t);

    proj.add_executable("test_fetch")
        .sources("tests/test_fetch.cpp")
        .depends_on(**xxh_t)
        .depends_on(**expected_t)
        .is_test();

    auto result = proj.build();
    return result ? 0 : 1;
}
