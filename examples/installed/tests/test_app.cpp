#include <cstdio>

int add(int a, int b);

int main() {
    if (add(2, 3) != 5) {
        std::fprintf(stderr, "FAILED: add(2,3) != 5\n");
        return 1;
    }
    std::printf("all installed-example tests passed\n");
    return 0;
}
