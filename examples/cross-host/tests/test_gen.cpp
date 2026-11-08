// Host-side test: for_host() + is_test() — built for the build machine and run
// by `./truss test`.
#include <cstdio>
#include <cstdlib>

#include "hostcore.hpp"

static void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

int main() {
    check(hostcore::answer() == 42, "hostcore::answer() == 42");
    std::printf("all host-tool tests passed\n");
    return 0;
}
