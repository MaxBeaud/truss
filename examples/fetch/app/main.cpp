#include <cstdio>
#include <string_view>

#include <tl/expected.hpp>
#include "xxhash.h"

static tl::expected<uint64_t, const char*> hash(std::string_view s) {
    if (s.empty())
        return tl::unexpected("empty input");
    return XXH64(s.data(), s.size(), 0);
}

int main() {
    auto h = hash("truss");
    if (!h) {
        std::fprintf(stderr, "hash failed: %s\n", h.error());
        return 1;
    }
    std::printf("XXH64(\"truss\") = %016llx\n", static_cast<unsigned long long>(*h));

    auto bad = hash("");
    std::printf("empty input rejected: %s\n", bad ? "no" : bad.error());
    return 0;
}
