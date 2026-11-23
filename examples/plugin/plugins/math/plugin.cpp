// plugin_math — built as a shared object, never linked: the host dlopen()s it.
#include "plugin_api.hpp"

namespace {
/* hidden by symbols_hidden(): the host can only see plugin_add */
int scaled(int x) { return x * 2; }
} // namespace

PLUGIN_EXPORT int plugin_add(int a, int b) {
    (void)scaled(0); // keep the internal helper honest (and non-exported)
    return a + b;
}
