// host — opens libplugin_math.so at runtime by path and calls its exported ABI.
#include "plugin_api.hpp"

#include <cstdio>
#include <dlfcn.h>
#include <filesystem>
#include <system_error>

int main() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path exe = fs::canonical("/proc/self/exe", ec);
    if (ec) {
        std::fprintf(stderr, "host: cannot locate the executable: %s\n", ec.message().c_str());
        return 1;
    }
    /* the host lives in build/<mode>/bin/<triple>/, the library in lib/<triple>/ */
    const fs::path exe_dir = exe.parent_path();
    const fs::path plugin =
        exe_dir.parent_path().parent_path() / "lib" / exe_dir.filename() / "libplugin_math.so";

    void* handle = dlopen(plugin.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        std::fprintf(stderr, "host: dlopen(%s): %s\n", plugin.c_str(), dlerror());
        return 1;
    }
    using add_fn = int (*)(int, int);
    auto add = reinterpret_cast<add_fn>(dlsym(handle, "plugin_add"));
    if (add == nullptr) {
        std::fprintf(stderr, "host: dlsym(plugin_add): %s\n", dlerror());
        dlclose(handle);
        return 1;
    }
    const int sum = add(2, 3);
    std::printf("plugin says: 2 + 3 = %d\n", sum);
    dlclose(handle);
    return sum == 5 ? 0 : 1;
}
