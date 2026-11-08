// The cross app: compiled for aarch64-linux-musl by zig. It cannot run on the
// build host; `./truss test` builds it and skips cross tests with a warning.
#include <cstdio>

int main() {
#if defined(__aarch64__)
    std::printf("cross app running on aarch64-linux-musl\n");
    return 0;
#else
    std::printf("cross app built for the wrong arch (expected aarch64)\n");
    return 1;
#endif
}
