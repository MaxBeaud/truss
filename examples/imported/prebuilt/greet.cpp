// Source of the prebuilt library. The example's truss.cpp compiles and archives
// this into prebuilt/libgreet.a before the build starts (a stand-in for a
// vendor/SDK drop that ships only a .a + headers).
#include "greet.h"

namespace greet {

const char* message() {
    return "hello from the prebuilt lib";
}

int add(int a, int b) {
    return a + b;
}

} // namespace greet
