#include <cstdio>
#include <cstdlib>
#include <string_view>

#include <greet.h>

static void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

int main() {
    check(std::string_view(greet::message()) == "hello from the prebuilt lib", "message()");
    check(greet::add(2, 3) == 5, "add(2, 3) == 5");
    check(greet::add(-4, 4) == 0, "add(-4, 4) == 0");
    std::printf("all imported-lib tests passed\n");
    return 0;
}
