# imported — linking a prebuilt library with `import_library()`

A prebuilt `.a` (vendor drop, SDK, artifact from another build system) is
consumed without truss ever building it:

```cpp
auto& greet = proj.import_library("greet")
                  .import_archive("prebuilt/libgreet.a")   // linked verbatim into dependents
                  .system_include_dirs("prebuilt/include"); // headers as -isystem

proj.add_executable("app").sources_in("src/app").depends_on(greet); // deref-free
```

- `import_archive(path)` takes an absolute path or one relative to the project
  root; the file must exist by `build()` time (a missing file is a Config error
  with the fix). Repeat calls accumulate (e.g. a `.a` plus an import `.lib`).
- `system_include_dirs(...)` propagates to consumers as `-isystem`, so warnings
  inside the prebuilt's headers never trip a consumer's `-Werror`.
- Consumers get the archive on their link line (after their objects, in GNU ld
  order) through `depends_on()` — no deref needed.

The `prebuilt/libgreet.a` archive itself is produced by the example's `truss.cpp`
up front (via `os::run`), standing in for a third-party drop.

Run it:

```sh
./truss run     # builds app + test_greet, runs app
./truss test    # builds and runs test_greet
```
