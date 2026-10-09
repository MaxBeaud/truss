#include <cstdio>
#include <cstdlib>
#include <string_view>

#include <tl/expected.hpp>
#include "xxhash.h"

static void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

int main() {
    /* xxhash: values computed against the fetched library itself */
    check(XXH64("", 0, 0) == 0xEF46DB3751D8E999ULL, "XXH64 empty-string vector");
    check(XXH32("abc", 3, 0) == 0x32D153FFULL, "XXH32(\"abc\") vector");

    /* tl::expected: value + error paths */
    tl::expected<int, int> ok = 42;
    check(*ok == 42, "tl::expected holds value");
    tl::expected<int, int> err = tl::unexpected(7);
    check(!err && err.error() == 7, "tl::unexpected carries error");

    std::printf("all fetch-example tests passed\n");
    return 0;
}
