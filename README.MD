# truss

Build your C++ using C++. A small, fast build system with a
[mate.h](https://github.com/ander000/mate)-style API, xmake-class features,
and a [samurai](https://github.com/michaelforney/samurai) (ninja) backend.

Write your build script in C++ with truss's module API, compile it to `./truss`,
and run your builds:

```cpp
// truss.cpp
import truss;

int main(int argc, char** argv) {
    truss::Project proj("Engine", argc, argv);

    auto& core = proj.add_static_library("core")
                     .include_dirs("include")
                     .sources_in("src/core");

    proj.add_executable("app")
        .sources_in("src/app")
        .depends_on(core);

    proj.add_executable("test_core")
        .sources_in("tests")
        .depends_on(core)
        .is_test();

    return proj.build() ? 0 : 1;
}
```

```sh
./truss debug          # build into build/debug/
./truss test           # build + run every is_test() target
./truss run            # build + run the first executable
./truss release test   # release mode + tests
```

## Getting truss

Everything is always compiled on your machine — no prebuilt binaries, so the
library, its module BMI, and samurai all match your toolchain. Pick one:

**1. Vendored source + bootstrap (simplest).** Clone the repo, then:

```sh
./bootstrap.sh                 # plain clang++: no xmake required
clang++ -std=c++26 -stdlib=libc++ \
    -fmodule-file=truss=build-bootstrap/bmi/truss.pcm \
    -fmodule-file=truss:api=build-bootstrap/bmi/truss_api.pcm \
    truss.cpp build-bootstrap/libtruss.a -o truss
```

**2. Installed tool.** `./bootstrap.sh --install ~/.local` stages
`libtruss.a` + BMIs under `~/.local/lib/truss/`; projects compile their
`truss.cpp` against them (see `examples/installed`).

**3. Single header (mate.h-style drop-in).** No modules, no link step:

```sh
just amalgam                   # generates dist/truss_all.hpp
clang++ -std=c++26 -Idist truss.cpp -o truss   # #include "truss_all.hpp"
```

(For bootstrapping truss itself, `xmake` also works: `xmake f -m debug && xmake`.)

## Features

- **Targets**: executable / static / shared / interface (header-only), with
  builder methods or pipe composition (`| truss::with_sources_in("src")`).
- **Tasks**: `test`, `run`, `clean`, `compdb` (compile_commands.json), plus
  your own `proj.task("fmt", fn)`.
- **C++ modules for your project**: `.cppm` sources are detected, BMIs built
  and wired automatically (`import std;` passes through).
- **Dependencies**:
  - `add_subdirectory("vendor/foo")` — convention-based vendoring,
  - `fetch(name, {git+tag | url+sha256})` — CPM-style, cached in `~/.cache/truss`,
  - `conan("conanfile.txt")` / `vcpkg({"zlib"})` — package managers, consumed
    as `Interface` targets.
- **Incremental**: one ninja manifest per mode, depfiles, build-log reuse via
  samurai; truss builds itself in ~5s clean / ~0.1s no-op.

## Repository layout

| Path | What |
|---|---|
| `src/` | truss itself: one named module (`truss:api`, `truss:impl`, core units) |
| `vendor/samurai/` | vendored ninja-compatible backend (C, small patch set) |
| `truss.cpp` | truss's own build script (self-hosting) |
| `examples/` | one fixture per feature (hello, shared, modules, fetch, conan, vcpkg, installed, amalgam) |
| `tools/amalgamate.cpp` | generates the single-header `dist/truss_all.hpp` |
| `PLAN.MD` / `AGENT.MD` | phased plan + verified toolchain facts for contributors |

Contributions welcome — `just build && just test` must stay green.
Design constraints live in [AGENT.MD](AGENT.MD); the roadmap in [PLAN.MD](PLAN.MD).
