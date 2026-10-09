#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <zlib.h>

static void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

int main() {
    check(crc32(0, reinterpret_cast<const Bytef*>("abc"), 3) == 0x352441C2UL, "crc32(\"abc\")");
    check(adler32(0, reinterpret_cast<const Bytef*>("abc"), 3) == 0x024A0126UL, "adler32(\"abc\")");

    const char* text = "truss vcpkg zlib round-trip payload payload payload";
    std::vector<unsigned char> comp(256);
    uLongf n = comp.size();
    check(compress2(comp.data(), &n, reinterpret_cast<const Bytef*>(text),
                    static_cast<uLong>(std::strlen(text)), Z_BEST_COMPRESSION) == Z_OK,
          "compress2 ok");
    check(n < std::strlen(text), "compressed smaller than input");

    std::vector<unsigned char> back(256);
    uLongf m = back.size();
    check(uncompress(back.data(), &m, comp.data(), n) == Z_OK, "uncompress ok");
    check(m == std::strlen(text) && std::memcmp(back.data(), text, m) == 0, "round-trip equal");

    std::printf("all vcpkg-example tests passed (zlib %s)\n", zlibVersion());
    return 0;
}
