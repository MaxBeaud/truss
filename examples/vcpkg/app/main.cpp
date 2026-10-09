#include <cstdio>
#include <cstring>
#include <vector>

#include <zlib.h>

int main() {
    const char* text = "hello truss via vcpkg zlib";
    std::vector<unsigned char> comp(128);
    uLongf n = comp.size();
    if (compress2(comp.data(), &n, reinterpret_cast<const Bytef*>(text),
                  static_cast<uLong>(std::strlen(text)), Z_DEFAULT_COMPRESSION) != Z_OK) {
        std::fprintf(stderr, "compress2 failed\n");
        return 1;
    }
    std::vector<unsigned char> back(128);
    uLongf m = back.size();
    if (uncompress(back.data(), &m, comp.data(), n) != Z_OK) {
        std::fprintf(stderr, "uncompress failed\n");
        return 1;
    }
    std::printf("vcpkg zlib: %zu -> %lu -> %.*s\n", std::strlen(text), n,
                static_cast<int>(m), back.data());
    return 0;
}
