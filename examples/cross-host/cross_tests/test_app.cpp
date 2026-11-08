// Cross test: built for aarch64-linux-musl like the app. `./truss test` builds
// it, then skips running it with a warning (no runner configured for the cross
// target) — a skipped cross test is never reported as passed.
#include <cstdio>

int main() {
    std::printf("cross test body (only runs on the target)\n");
    return 0;
}
