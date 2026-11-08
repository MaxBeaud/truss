#pragma once

// Public header of the "prebuilt" greet library. Consumed via the imported
// library's system_include_dirs("prebuilt/include").
namespace greet {

const char* message();
int add(int a, int b);

} // namespace greet
