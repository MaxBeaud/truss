#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "greet.hpp"

int main() {
    if (std::string_view(greet::message()) != "hello from a shared library") {
        std::fprintf(stderr, "FAILED: unexpected message\n");
        return 1;
    }
    std::printf("shared-lib test passed\n");
    return 0;
}
