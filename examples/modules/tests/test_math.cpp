#include <cstdio>
#include <cstdlib>

import math;

static void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

int main() {
    check(mathx::add(2, 3) == 5, "add(2,3) == 5");
    check(mathx::mul(4, 5) == 20, "mul(4,5) == 20");
    std::printf("all modules-example tests passed\n");
    return 0;
}
