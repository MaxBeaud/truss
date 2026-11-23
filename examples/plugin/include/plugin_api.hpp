#pragma once
// The ABI a plugin and its host share. `symbols_hidden()` hides every symbol
// truss's targets compile unless it is explicitly exported, so the one entry
// point is annotated; global variables/functions without this attribute would
// not be findable by dlsym().
#if defined(_WIN32)
#define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

PLUGIN_EXPORT int plugin_add(int a, int b);
