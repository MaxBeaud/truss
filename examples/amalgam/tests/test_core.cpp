#include <cstdio>

int mul(int a, int b);

int main() {
    if (mul(2, 3) != 6) {
        std::fprintf(stderr, "FAILED: mul(2,3) != 6\n");
        return 1;
    }
    std::printf("all amalgam-example tests passed\n");
    return 0;
}
