#include <cstdio>

#include <greet.h>

int main() {
    std::printf("%s: 2 + 3 = %d\n", greet::message(), greet::add(2, 3));
    return 0;
}
