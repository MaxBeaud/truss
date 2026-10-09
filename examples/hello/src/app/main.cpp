#include <cstdio>

#include "core.hpp"

int main() {
    std::printf("Hello from %s: 2 + 3 = %d\n", core::name(), core::add(2, 3));
    return 0;
}
