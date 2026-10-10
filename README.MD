# truss

**Build your C++ using C++.** A small, fast build system with a
[mate.h](https://github.com/ander000/mate)-style API, xmake-class features, and a
[samurai](https://github.com/michaelforney/samurai) (ninja) backend.

This README is the user guide; the in-depth API reference is generated with
`./setup.sh docs` (Doxygen → `build/docs/html/`).

Write your build script in C++ with truss's module API, compile it once to
`./truss`, and run your builds from there:

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

There is no configure step and no config file: the build script *is* the
configuration, and it re-runs (cheaply) on every invocation, regenerating a
ninja manifest that samurai then executes incrementally.

---

## Table of contents

- [Getting truss](#getting-truss)
- [Command line](#command-line)
- [The build script](#the-build-script)
  - [Targets](#targets)
  - [Sources & globbing](#sources--globbing)
  - [Usage requirements & propagation](#usage-requirements--propagation)
  - [Compiler options](#compiler-options)
  - [Hardening](#hardening)
  - [Precompiled headers](#precompiled-headers)
  - [Unity builds](#unity-builds)
  - [Custom tasks](#custom-tasks)
  - [Build hooks](#build-hooks)
- [C++ modules](#c-modules)
- [Dependencies](#dependencies)
  - [add_subdirectory](#add_subdirectory)
  - [fetch](#fetch)
  - [conan](#conan)
  - [vcpkg](#vcpkg)
- [Toolchain selection (Config)](#toolchain-selection-config)
- [Cross-compiling & MSVC](#cross-compiling--msvc)
- [Output layout](#output-layout)
- [Errors](#errors)
- [Building truss itself](#building-truss-itself)
- [Status & support matrix](#status--support-matrix)

---

## Getting truss

Everything is built **on your machine** — truss ships no prebuilt binaries.
Pick whichever style fits your project (all three are exercised by `./setup.sh test`):

**1. Vendored source (easiest).** Drop this repo into `vendor/truss/`, then
build truss's library + BMI with plain `clang++` (no build system needed):

```sh
./vendor/truss/setup.sh bootstrap      # -> build/bootstrap/libtruss.a + BMIs
clang++ -std=c++26 -fmodule-file=truss=build/bootstrap/bmi/truss.pcm \
        -fmodule-file=truss:api=build/bootstrap/bmi/truss_api.pcm \
        truss.cpp build/bootstrap/libtruss.a -o truss
```

**2. Installed tool.** `./setup.sh install /usr/local` stages
`libtruss.a` + module BMIs; compile your project's `truss.cpp` against them
(see `examples/installed/`, which contains only a `truss.cpp`).

**3. Single header (mate.h-style drop-in).** No modules, no link step:

```sh
./setup.sh amalgam               # generates build/dist/truss_all.hpp
clang++ -std=c++26 -Ibuild/dist truss.cpp -o truss   # #include "truss_all.hpp"
```

Requirements: a C++26 compiler (clang 19+ or GCC 15+; clang is the reference)
and a POSIX-ish OS. truss currently builds and runs its test suite on Linux;
Windows hosts are on the roadmap (see [Status](#status--support-matrix)).

## Command line

```
./truss [debug|release] [task...] [-j N] [-v] [-n] [--explain] [--target NAME] [--build-dir PATH]
```

| Flag | Meaning |
|---|---|
| `debug` / `release` | Build mode (default `debug`). One build dir per mode. |
| `task...` | Any number of tasks, built-in or yours (see below). |
| `-j N` | Parallel jobs (default: samurai's auto, ≈ nproc+2). |
| `-v` | Echo every command samurai runs. |
| `-n` | Dry run: print what *would* run, change nothing. |
| `--explain` | Print *why* each edge is (re)built — samurai's explain mode. |
| `--target NAME` | Build only this target (repeatable). |
| `--build-dir PATH` | Relocate the build root (default `./build`). |

Built-in tasks:

| Task | What it does |
|---|---|
| `test` | Builds, then runs every target marked `.is_test()`. |
| `run` | Builds, then runs the first executable target. |
| `clean` | Deletes the mode's build dir. |
| `compdb` | Writes `compile_commands.json` (via samurai's compdb tool). |

Tasks are composable with modes and flags: `./truss release test -j8 -v`.

## The build script

A truss program is an ordinary C++ `main()`. `Project` parses the command line,
you declare targets, and `proj.build()` generates the manifest and runs the
build. Everything returns `truss::Result<T>` (a `std::expected<T, Error>`) —
no exceptions.

### Targets

| Factory | Artifact (GCC/Clang) | Artifact (MSVC) |
|---|---|---|
| `add_executable(name)` | `bin/<triple>/<name>` | `bin/<triple>/<name>.exe` |
| `add_static_library(name)` | `lib/<triple>/lib<name>.a` | `lib/<triple>/<name>.lib` |
| `add_shared_library(name)` | `lib/<triple>/lib<name>.so` (+ soname) | `lib/<triple>/<name>.dll` + import `.lib` |
| `add_interface_library(name)` | none — usage requirements only | same |

Target names must be bare names (no path separators, no platform extensions —
truss adds them). Every builder method returns `Target&`, so you can chain:

```cpp
auto& net = proj.add_static_library("net")
                .sources({"src/net/tcp.cpp", "src/net/tls.cpp"})
                .include_dirs({"include", "third_party/openssl/include"})
                .defines("NET_EXPORTS")
                .cpp_standard(truss::CppStandard::Cpp23)
                .warnings(truss::Warnings::All);

proj.add_executable("server").sources_in("src/server").depends_on(net);
```

`depends_on()` links the dependency (static/shared) or merges its usage
requirements (interface), and orders the build correctly.

### Sources & globbing

`sources(...)` takes a pattern, a brace list, or any range of strings.
`sources_in(dir)` is the common case: every `.cpp/.cc/.cxx/.c/.cppm` under
`dir`, recursively. Patterns are relative to the project root and support:

| Syntax | Meaning |
|---|---|
| `*` | anything within one path component |
| `**` | anything across components (`src/**.cpp` = all C++ under `src/`) |
| `?` | exactly one character |
| `{a,b}` | alternation (`src/*.{cpp,c}`) |

Hidden files (any component starting with `.`) are skipped. A pattern that
matches nothing is fine; a *literal* source that doesn't exist is a config
error. `remove_source("path")` excludes one resolved file (truss itself uses
this to drop samurai's `samu.c`).

### Usage requirements & propagation

- `include_dirs(...)` propagate to everything that `depends_on()` a target,
  transitively.
- `defines(...)`, `link_libs(...)`, `link_paths(...)` propagate **only from
  `Interface` targets** — the CMake `INTERFACE` semantics. A static library's
  own `-D` flags stay private to it; a `add_interface_library("config")`
  target is how you share them.
- `link_frameworks(...)` / `framework_paths(...)` are Apple-only (rejected on
  MSVC targets).

This is what makes package managers ergonomic: `conan()`/`vcpkg()` return an
`Interface` target carrying the whole dependency graph's flags, and one
`depends_on()` wires it up.

### Compiler options

| Method | Values | Default |
|---|---|---|
| `cpp_standard(...)` | `Cpp11 … Cpp26` | C++23 |
| `warnings(...)` | `None, Minimal, All, Pedantic` | `-Wall -Wextra` |
| `debug_info(...)` | `None, Minimal, Medium, Full` | Full in debug, none in release |
| `optimization(...)` | `None, O1, O2, Size, O3` | `-O0` debug, `-O2` release |
| `sanitizer(...)` | `None, Address, Ub, All` | off |
| `lto(...)` | `None, Full, Thin` | off |
| `harden(...)` | `None, Light, Medium, Max` | **Light** (see below) |
| `pic(bool)` | position-independent code | on for libs |
| `modules_mode(...)` | `Off, Auto, Manual` | `Auto` |
| `cross_target(triple)` | see [Cross-compiling](#cross-compiling--msvc) | host |
| `is_test(bool)` | mark as a test for the `test` task | false |
| `flags("...")` | raw compiler flags, verbatim | — |
| `linker_flags("...")` | raw linker flags, verbatim | — |

`Default` enum values always resolve per mode (e.g. `DebugInfo::Default` →
`Full` in debug, `None` in release), so a single build script does the right
thing in both modes.

### Hardening

Every target gets truss's **default hardening stack** (`Hardening::Light`)
unless you opt out with `harden(Hardening::None)`:

| Tier | GCC / Clang | MSVC |
|---|---|---|
| `Light` (default) | `-fstack-protector-strong`; link `-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,--as-needed` | `/NXCOMPAT /DYNAMICBASE` |
| `Medium` | + `-ftrivial-auto-var-init=pattern`, `-fcf-protection=full -fstack-clash-protection` (x86-64), libc++ `_LIBCPP_HARDENING_MODE_HARDENED` | + `/guard:cf` (compile + link, x86-64) |
| `Max` | Medium + libc++ `_LIBCPP_HARDENING_MODE_DEBUG` + `-D_GLIBCXX_ASSERTIONS` | + `/sdl`, `/CETCOMPAT` (x86-64) |

The stdlib hardening modes are emitted as defines, so they apply whichever
stdlib you link (each stdlib ignores the other's macro).

```cpp
proj.add_executable("app").sources_in("src")
   .harden(truss::Hardening::Max);   // maximum paranoia
```

### Precompiled headers

`target.pch(header)` precompiles one header for that target's C++ sources —
one extra `PCH` build edge, and every object of the target carries the use
flag:

```cpp
proj.add_static_library("core").sources_in("src").pch("include/stdinc.hpp");
```

clang uses `-include-pch`; GCC generates a `.gch` and finds it through
`-I`/`-include`. The PCH output lives under `build/<mode>/pch/<target>/` and
is dependency-tracked like any other edge (editing the PCH header rebuilds
the target's objects; editing one source rebuilds only that object). Module
BMI rules and C sources never use the PCH, and MSVC targets are a clear
`Config` error in v0.

### Unity builds

`target.unity()` batches that target's C/C++ sources into generated batch
TUs (16 sources each) under `build/<mode>/unity/<target>/`, compiled
instead of the originals:

```cpp
proj.add_static_library("core").sources_in("src").unity();
```

Clean builds get much faster (one preprocess + one compiler warm-up per
batch instead of per TU — the 60-TU benchmark fixture builds ~2x faster
with unity). The trade-off is coarser incrementality: editing one source
rebuilds its whole batch, and same-named `static` functions or anonymous
namespace entities in different sources can now collide. Module units
(`.cppm`) are never batched; batch files are only rewritten when their
membership changes, so unchanged builds stay no-ops.

### Custom tasks

`proj.task(name, fn)` registers `./truss <name>`:

```cpp
proj.task("hello", [](const truss::TaskContext& ctx) -> truss::Result<void> {
    std::printf("[TASK] project=%s mode=%s build_dir=%s\n", ctx.project.c_str(),
                ctx.mode == truss::BuildMode::Debug ? "debug" : "release",
                ctx.build_dir.string().c_str());
    return {};
});
```

`TaskContext` carries the project name, mode, build dir, cwd, and job count.

### Build hooks

`proj.pre_build(fn)` / `proj.post_build(fn)` wrap the compile step — codegen
before, staging/notifications after:

```cpp
proj.pre_build([](const truss::TaskContext&) -> truss::Result<void> {
    /* generate sources, bump a version file, ... */
    return {};
});
proj.post_build([](const truss::TaskContext& ctx) -> truss::Result<void> {
    std::printf("built into %s\n", ctx.build_dir.string().c_str());
    return {};
});
```

When things run, in order:

```text
./truss [debug|release] [--target X] <task>
   │
   ├─ configure: targets resolved, Config applied, manifest written
   ├─ pre_build hooks   (registration order; a failure aborts)
   ├─ samurai build     (the actual compile + link)
   ├─ post_build hooks  (once, after the first successful build)
   └─ task execution    (test binaries, run, named tasks)
```

Hooks fire once per invocation; `clean` and `compdb` never trigger them.
Named tasks (`./truss <name>`) stay manual commands — hooks are for what must
always run around the build itself.

## C++ modules

Your project's `.cppm` files are handled automatically (`modules_mode(Auto)`,
the default): each interface unit gets a BMI compile edge (`-fmodule-output`),
and every other unit in the target — and in targets that `depends_on()` it —
receives the matching `-fmodule-file=NAME=path` flags. `import std;` passes
straight through. Import scanning is a conservative v0.1 regex pass over each
unit (module/import declarations only).

```cpp
proj.add_static_library("math").sources_in("src/math"); // src/math/math.cppm inside
proj.add_executable("calc").sources("src/calc.cpp").depends_on(math);
// calc.cpp's `import math;` just works
```

`modules_mode(Off)` disables the wiring (`.cppm` then errors rather than being
silently miscompiled). Module BMI wiring is GCC/Clang-only for now; MSVC
targets with `.cppm` sources get a clear config error.

`modules_mode(Manual)` replaces the regex import scan with `clang-scan-deps`
(p1689 JSON, run at configure time, ~10 ms per TU, parallel): the import
lists become the compiler's own — exact partition names, and imports the
regex cannot see (e.g. line-spliced `import \` continuations). Clang-family
only. Use it when Auto mis-scans your sources.

## Dependencies

### add_subdirectory

Convention-based vendoring — point it at a directory in your tree:

```cpp
auto zlib = proj.add_subdirectory("vendor/zlib");   // Result<TargetRef>
if (!zlib) return 1;
proj.add_executable("app").sources_in("src").depends_on(*zlib);
```

It creates a static library named after the directory, with sources from
`src/` (recursively) and `include/` added as an include dir when present.
No manifest required.

### fetch

CPM-style downloads, pinned and cached:

```cpp
auto pkg = proj.fetch("fmt", {
    .git = "https://github.com/fmtlib/fmt",
    .tag = "11.0.2",
});
auto fmt = proj.add_subdirectory(pkg->dir);
proj.add_executable("app").depends_on(*fmt);

// several packages at once (downloads overlap):
auto pkgs = proj.fetch_all({
    {"fmt",  truss::FetchOptions{.git = "https://github.com/fmtlib/fmt", .tag = "11.0.2"}},
    {"zlib", truss::FetchOptions{.url = "https://zlib.net/zlib-1.3.1.tar.gz",
                                 .sha256 = "9a93b2b7df..." }},
});
```

Exactly one of `git+tag` (shallow clone pinned to a tag/branch/commit) or
`url+sha256` (`.tar.gz`/`.tar.xz` verified against the hash) must be set.
Downloads are cached in `~/.cache/truss`, materialized into
`build/<mode>/packages/<name>`, and pinned in a `truss.lock` file next to your
build script — so CI gets the same tree without a network round trip.

Git trees are materialized with hardlinks (instant, no extra disk; real copies
across filesystems). Caveat: a hardlinked fetched file shares its inode with
the cache, so editing it *in place* inside `build/` also edits the cached copy
— compilers replace files rather than write in place, so normal builds never
hit this.

### conan

Conan 2 graphs, consumed as one `Interface` target:

```cpp
auto deps = proj.conan({"zlib/1.3.1", "fmt/11.0.2"}); // list form (refs are versioned)
// or: proj.conan("conanfile.txt") / "conanfile.py" / "."
proj.add_executable("app").sources_in("src").depends_on(*deps);
```

The list form generates a `conanfile.txt` under
`build/<mode>/packages/conan_generated/` for you. Either way truss runs
`conan install --build=missing` (JSON graph parsed from stdout), installs
under `build/<mode>/packages/conan/`, and propagates every package's include dirs,
defines, link libs and link paths.

Repeated invocations are cheap: truss fingerprints the conanfile (sha256) and
caches the usage requirements in `build/<mode>/packages/conan/truss-spec.json`;
an unchanged conanfile skips `conan install` entirely (~1–3 s saved). Editing
the conanfile re-runs it.

### vcpkg

Manifest mode, same consumption model:

```cpp
auto deps = proj.vcpkg({"zlib", "fmt"});   // or proj.vcpkg({}) with your own vcpkg.json
proj.add_executable("app").sources_in("src").depends_on(*deps);
```

Installs into `build/<mode>/vcpkg_installed/` and returns an `Interface`
target. Requires `VCPKG_ROOT` pointing at a vcpkg tree. As with conan, an
unchanged `vcpkg.json` (sha256 fingerprint) + surviving install tree skips
`vcpkg install` on later invocations.

## Toolchain selection (Config)

Pick the project-wide compiler, linker, and standard library with
`truss::Config` — the compiler *family* is detected from the name:

```cpp
truss::Config conf;
conf.compiler("clang++-21")       // name or path (family auto-detected)
    .linker("mold")               // passed as -fuse-ld=
    .stdlib(truss::StdLib::Libcxx);
proj.configure(conf);

// or one call: expands to clang++ + libc++ + llvm-ar
proj.configure(truss::Config{}.toolchain(truss::Toolchain::LLVM));
```

`toolchain(Toolchain::GCC)` is the matching `g++` + libstdc++ + `ar` bundle;
explicit calls after `toolchain()` override it. Selections are validated:
`g++` + `libc++` and any `stdlib()` on MSVC targets are clear Config errors.
`-stdlib=` is emitted on both compile and link lines. Per-target
`cross_target(triple)` still overrides the project default completely, and
`TargetTriple` gained a `stdlib` field for per-target selection too.

### Compiler cache

truss auto-detects `ccache` (then `sccache`) on `PATH` and prefixes it into
the C/C++ compile commands — no setup needed if a cache is installed. Override
or disable it with `Config::launcher`:

```cpp
proj.configure(truss::Config{}.launcher("sccache"));  // pick a specific cache
proj.configure(truss::Config{}.launcher(""));         // never use a launcher
```

The launcher applies to GCC/Clang-family compile rules only: MSVC rules and
module BMI rules stay raw (ccache's C++20 module support is unreliable). A
named launcher that isn't on `PATH` is a Config error.

## Cross-compiling & MSVC

Every target defaults to the host triple (`truss::host_triple()`), detected
from the compiler. Override per target with a `TargetTriple`:

```cpp
truss::TargetTriple win;
win.os       = truss::Os::Windows;
win.arch     = truss::Arch::X64;
win.family   = truss::CompilerFamily::Msvc;
win.compiler = "clang-cl";      // or "cl.exe"
win.ar       = "llvm-lib";      // or "lib.exe"
win.linker   = "lld-link";      // optional, passed as -fuse-ld=

proj.add_executable("app").sources_in("src").cross_target(win);
```

MSVC-family targets get the full flag mapping (`/std:c++latest`, `/W4`, `/Z7`,
`/O2`, `/fsanitize=address`, `/GL`+`/LTCG`, `/D`, `/I`, `/DNDEBUG`, …), COFF
naming (`.obj`/`.exe`/`.lib`/`.dll` + import libs), `lib.exe` archives,
`/link`-split link lines, and response files (`@out.rsp`) for long object
lists. Header dependencies come from `/showIncludes`: the vendored samurai
carries a documented feature patch implementing ninja's `deps = msvc`, so
incremental builds are correct with `cl.exe` *and* `clang-cl`.

Notes:
- On non-Windows hosts, `clang-cl` needs `linker = "lld-link"` (no VS
  installation to discover `link.exe`).
- C++ modules are not supported on MSVC targets yet (clear error, not silent
  miscompilation).
- The MSVC path is tested end-to-end on Linux with clang-cl/llvm-lib/lld-link
  (objects, archives, PE link, `/showIncludes` dep tracking, incremental
  rebuilds); the produced `.exe` is not run.

## Output layout

```
build/
  debug/                      # one dir per mode
    ninja/build.ninja         # generated manifest
    obj/<triple>/<target>/…   # .o/.obj (+ .d depfiles, bmi/*.pcm)
    bin/<triple>/<exe>        # executables
    lib/<triple>/…            # archives, shared libs (+ MSVC import libs)
    packages/<name>           # fetch() checkouts (packages/conan for conan)
    vcpkg_installed/          # package-manager installs
```

The manifest is regenerated on every run (cheap, ~ms) and samurai does the
incremental work: depfiles (or `.ninja_deps` records), build-log reuse, and
correct rebuild sets — verified by tests that touch a file and assert exactly
which edges rerun.

## Errors

Every fallible call returns `truss::Result<T> = std::expected<T, Error>`:

```cpp
struct Error {
    enum class Kind { Io, Config, Command, Fetch, Toolchain, Internal };
    Kind kind;
    std::string message;
    std::optional<std::filesystem::path> path;
};
```

`proj.build()` logs the error (red, with the offending path) and returns it;
the exit code convention from the quick start (`return proj.build() ? 0 : 1;`)
is all you need. truss itself is built with exceptions disabled.

## Building truss itself

truss builds itself (`truss.cpp` at the repo root is its own build script).
`setup.sh` is the single entry point — plain `clang++`, no build system:

```sh
./setup.sh build               # bootstrap libtruss.a + BMI, compile ./truss
./truss debug                  # truss, built by truss
./setup.sh test                # unit tests + all example flows
```

Other useful commands: `./setup.sh unit` (doctest, ASan/UBSan), `./setup.sh
examples`, `./setup.sh installed`, `./setup.sh amalgam`, `./setup.sh install
[prefix]`, `./setup.sh docs` (Doxygen API reference → `build/docs/html/`),
`./setup.sh clean`. All generated output lives under `build/`.

## Status & support matrix

v0.1.0 — all eleven planned phases complete except benchmarks.

| Feature | GCC | Clang | MSVC (cl.exe / clang-cl) |
|---|---|---|---|
| Executable / static / shared / interface | ✅ | ✅ | ✅ (tested via clang-cl cross-build) |
| Globbing, tasks, compdb, incremental | ✅ | ✅ | ✅ |
| Sanitizers, LTO, per-target options | ✅ | ✅ | ✅ (no ThinLTO — maps to `/GL`) |
| C++ modules for your project | ✅ | ✅ | ❌ (clear error) |
| fetch / add_subdirectory / conan / vcpkg | ✅ | ✅ | host-side, same |

Known limitations: paths containing spaces or `:` are rejected (ninja
escaping, conservative v0.1); Windows *hosts* (running truss on Windows) are
not yet exercised.

`PLAN.MD` tracks the roadmap; `AGENT.MD` records verified toolchain facts.
The vendored samurai patches are documented in `vendor/samurai/TRUSS-VENDOR.txt`.
