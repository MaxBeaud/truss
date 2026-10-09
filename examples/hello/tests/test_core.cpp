#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "core.hpp"

static void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

int main() {
    check(core::add(2, 3) == 5, "add(2, 3) == 5");
    check(core::add(-1, 1) == 0, "add(-1, 1) == 0");
    check(std::string_view(core::name()) == "hello-core", "name() == \"hello-core\"");
    std::printf("all hello-core tests passed\n");
    return 0;
}
