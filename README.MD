# truss

**Build your C++ using C++.** A small, fast build system whose build scripts
are ordinary C++26 programs: a fluent builder API (inspired by
[mate.h](https://github.com/ander000/mate)), first-class C++20/26 modules,
cross-compilation to other platforms from your Linux host, and a
[samurai](https://github.com/michaelforney/samurai) (ninja) backend — no DSL,
no config files, no separate configure stage.

**Highlights**

- **C++ modules that just work** — automatic BMI/CMI wiring with the full
  transitive closure across `depends_on`, discovered by the compiler itself
  where possible; real `import std;` on clang + libc++ **and** GCC 15+
  (libstdc++).
- **Cross-compilation, first-class** — zig (bundled sysroots), mingw-w64 and
  llvm-mingw for Windows targets (llvm-mingw: modules + `import std` cross),
  or any target-prefixed GCC driver. Per-triple subgraphs build them all in
  one invocation; wine/qemu runners execute the outputs.
- **Static & portable binaries** — per-dependency `static_link()`,
  `static_deps()` and `static_stdlib()` (fully-static musl, or drop just the
  C++ runtime).
- **Packages** — `fetch` (git/tarball, sha256-pinned), `conan` and `vcpkg`,
  consumed as targets.
- **Fast & incremental** — compiler-cache launcher, unity builds, PCH,
  precise module wiring.
- **Hardened by default** — stack protector, RELRO, non-exec stack and
  libc++ hardening tiers, per target.

This README is the user guide; the in-depth API reference is generated with
`./setup.sh docs` (Doxygen → `build/docs/html/`). `CHANGELOG.md` tracks
changes; the public API is semver-stable as of 1.0.0.

## How truss works

A truss project has no build-system files. Its build script (`truss.cpp`) is
a C++26 program that declares targets and calls `proj.build()`; it is
compiled once into a `./truss` binary. From then on, every invocation:

```text
./truss [debug|release] [command...] [flags]
   │
   ├─ the script runs          it IS the configuration: targets are declared
   │                           by executing ordinary C++ code
   ├─ resolve                  sources globbed, module imports scanned
   │                           (by the compiler itself where possible)
   ├─ manifest written         build/<mode>/ninja/build.ninja — derived data,
   │                           regenerated every run, but written only when
   │                           its content changed (mtime-stable)
   ├─ samurai build            the incremental compile + link (depfiles,
   │                           build-log reuse, correct rebuild sets)
   └─ commands                 test / run / clean / compdb / reload / tasks
```

The regeneration is cheap because the script is cheap to run and the
manifest is derived from its declarations; samurai then does the incremental
work. Module-import scanning — ~10 ms per TU through the compiler — is
**cached**: each result is keyed by (source path, mtime, size, compile flags,
compiler) under `build/<mode>/scan-cache/`, so an unchanged tree skips the
scan entirely and a re-run costs a few milliseconds.

**The script rebuilds itself.** On startup, truss compares `truss.cpp` (next
to the executable) *and every header it includes, plus the truss library and
BMIs it was built against* (a depfile written at bootstrap) with the
executable. When any of them is newer, truss runs `bootstrap.sh` from the
same directory and re-executes with the same arguments — an edited script or
a changed header never builds a stale graph. Without a `bootstrap.sh` it
stops with an error asking for a rebuild instead. `TRUSS_NO_REBUILD=1` turns
the check off.

## Table of contents

- [How truss works](#how-truss-works)
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
  - [Static linking](#static-linking)
  - [Merged archives](#merged-archives)
  - [User tasks](#user-tasks)
  - [Build hooks](#build-hooks)
- [Scripting helpers (os / fs / log)](#scripting-helpers-os--fs--log)
- [C++ modules](#c-modules)
- [Dependencies](#dependencies)
  - [add_subdirectory](#add_subdirectory)
  - [fetch](#fetch)
  - [conan](#conan)
  - [vcpkg](#vcpkg)
  - [Imported prebuilt libraries](#imported-prebuilt-libraries)
  - [Custom commands (codegen)](#custom-commands-codegen)
- [Toolchain selection (Config)](#toolchain-selection-config)
- [Subgraphs: several triples, one invocation](#subgraphs-several-triples-one-invocation)
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

> **Note:** The `setup.sh` script places the `truss` binary in the **directory where you invoked it** (not in the truss source directory). For example, running `./vendor/truss/setup.sh build` from your project root will create `./truss` (not `./vendor/truss/truss`).

**2. Installed tool.** `./setup.sh install /usr/local` stages
`libtruss.a` + module BMIs + the `truss` CLI; compile your project's
`truss.cpp` against them (see `examples/installed/`, which contains only a
`truss.cpp`). The installed CLI scaffolds new projects:

```sh
truss init my-app        # my-app/{truss.cpp, include/, src/core, src/app, tests/, bootstrap.sh}
cd my-app && ./bootstrap.sh && ./truss test
```

`bootstrap.sh` is pre-wired to the modules next to the `truss` binary
(`TRUSS_LIB=... ./bootstrap.sh` overrides it). `init` only exists in installed
binaries (`-DTRUSS_INSTALLED_BIN`): the templates are `#embed`ded from
`cli/templates/init/`, so vendored and single-header users never carry them.

**3. Single header (drop-in).** No modules, no link step:

```sh
./setup.sh amalgam               # generates build/dist/truss_all.hpp
clang++ -std=c++26 -Ibuild/dist truss.cpp -o truss   # #include "truss_all.hpp"
```

Requirements: **clang 19+ with libc++** to build truss itself (the bootstrap
and the module BMIs are clang+libc++; it is the reference compiler), and for
your *project's targets*: clang, or GCC 15+ (named modules, `import std`),
or the cross toolchains below. POSIX hosts only (Linux is the tested one).

## Command line

```
./truss [debug|release] [command...] [-j N] [-v] [-n] [--explain] [--target NAME[:SUBGRAPH]] [--for SUBGRAPH] [--build-dir PATH]
```

truss has **built-in commands** and your **user tasks** — they are different
things: commands are truss's own verbs; tasks are yours, registered with
`proj.task(...)` and always invoked through `task`.

| Command | What it does |
|---|---|
| `test` | Builds, then runs every target marked `.is_test()` (per subgraph, each with its own runner). |
| `run` | Builds, then runs the first non-test executable (or the one named by `--target`), forwarding the arguments after `--`. |
| `clean` | Deletes the mode's build dir (cannot be combined with other commands). |
| `compdb` | Writes `compile_commands.json` (via samurai's compdb tool). |
| `reload` | Rebuilds the `truss` executable from source and replaces the current binary. Only works from the truss repository root. |
| `task NAME` | Builds, then runs a task registered with `proj.task(...)` (see [User tasks](#user-tasks)). |
| `task list` | Lists the registered tasks with their descriptions; builds nothing. |

| Flag | Meaning |
|---|---|
| `debug` / `release` | Build mode (default `debug`). One build dir per mode. |
| `-j N` | Parallel jobs (default: samurai's auto, ≈ nproc+2). |
| `-q, --quiet` | Errors and warnings only (same as `TRUSS_LOG=quiet`). |
| `-v` | Echo every command samurai runs (disables the live progress block). |
| `-n` | Dry run: print what *would* run, change nothing (no hooks fire, no test/run binary executes). |
| `--explain` | Print *why* each edge is (re)built — samurai's explain mode. |
| `--target NAME` | Build only this target (repeatable). With subgraphs: `NAME:SUBGRAPH` picks one subgraph's target; a bare name applies to every subgraph that has it. |
| `--for SUBGRAPH` | With subgraphs: run `test`/`run` for one subgraph only. |
| `--build-dir PATH` | Relocate the build root (default `./build`). |
| `--version` / `-V` | Print truss version and exit. |
| `--help` / `-h` / `help` | Print the full usage and exit. |
| `-- ARGS...` | Everything after `--` is passed to the program started by `run`. |

Commands compose with modes and flags: `./truss release test -j8 -v`,
`./truss run --target server -- --port 8080`.

### What the output looks like

On a terminal, truss prints phase lines with timings and scan-cache counters,
and samurai's progress is rendered as a live block of the files currently
compiling — finished files disappear instead of scrolling:

```text
▸ configure  4 ms · scan 287 hot / 2 cold / 3 skipped
  [42/301] 14% ▸ CXX src/net/tcp.cpp
                 ▸ CXX src/net/tls.cpp
▸ build      2.1 s
```

Piped output (CI, editors) stays plain and greppable: one line per step,
no cursor control. Related knobs: `TRUSS_LOG=quiet|warn|info|debug`,
`TRUSS_PROGRESS=plain` (disable the live block), and the standard
`NO_COLOR` / `CLICOLOR_FORCE` environment variables.

## The build script

A truss program is an ordinary C++ `main()`. `Project` parses the command
line, you declare targets, and `proj.build()` runs the flow described in
[How truss works](#how-truss-works). Everything returns `truss::Result<T>`
(a `std::expected<T, Error>`) — no exceptions.

### Targets

| Factory | Artifact (GCC/Clang) | Artifact (MSVC) |
|---|---|---|
| `add_executable(name)` | `bin/<triple>/<name>` | `bin/<triple>/<name>.exe` |
| `add_static_library(name)` | `lib/<triple>/lib<name>.a` | `lib/<triple>/<name>.lib` |
| `add_shared_library(name)` | `lib/<triple>/lib<name>.so` (+ soname) | `lib/<triple>/<name>.dll` + import `.lib` |
| `add_interface_library(name)` | none — usage requirements only | same |
| `import_library(name)` | none — links prebuilt files you name (see [below](#imported-prebuilt-libraries)) | same |
| `add_custom_command(name)` | generated files under `build/<mode>/gen/<name>/` | same |

Target names must be bare names (no path separators, no platform extensions —
truss adds them). Every builder method is a deducing-`this` template returning
the target's own type (`StaticLib&`, `Executable&`, `CustomCommand&`, ...), so
you can chain — including derived-only methods after base ones, in any order:

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
requirements (interface/imported/custom), and orders the build correctly —
archives land consumers-before-providers on every link line, so diamond
dependency graphs resolve in a single ld pass. Handles from
`add_subdirectory()`/`conan()`/`vcpkg()` pass straight in — no
dereferencing:

```cpp
proj.add_executable("app").sources_in("src").depends_on(proj.conan({"zlib/1.3.1"}));
```

### Sources & globbing

`sources(...)` takes a pattern, a brace list, or any range of strings.
`sources_in(dir)` is the common case: every `.cpp/.cc/.cxx/.c/.cppm` under
`dir`, recursively. Patterns are relative to the project root and support:

| Syntax | Meaning |
|---|---|
| `*` | anything within one path component |
| `**` | anything across components (`src/**.cpp` = all C++ under `src/`); as a full component (`**/x.cpp`) it matches whole components only |
| `?` | exactly one character |
| `{a,b}` | alternation (`src/*.{cpp,c}`) |

Hidden files (any wildcard-matched component starting with `.`) are skipped.
A pattern that matches nothing is fine; a *literal* source that doesn't exist
is a config error. Source paths must be path-safe (no spaces, shell or ninja
metacharacters — they land in shell-run command lines). `remove_source("path")`
excludes one resolved file (truss itself uses this to drop samurai's `samu.c`).

### Usage requirements & propagation

- `include_dirs(...)` propagate to everything that `depends_on()` a target,
  transitively.
- `system_include_dirs(...)` propagate the same way but as `-isystem`
  (MSVC: `/external:I` + `/external:W0`): warnings inside those headers are
  silenced, so third-party code doesn't trip a consumer's `-Werror`.
  `conan()` and `vcpkg()` targets use them for their package headers.
- `public_defines(...)` and `public_flags(...)` apply to the target **and**
  every dependent, transitively — CMake's `PUBLIC`. Use them for macros or
  ABI flags (`-pthread`, `-fno-rtti`) the library's headers need to agree on.
  `flags(...)` stays private; dependents' public flags come first on the
  command line, so a target's own `flags()` win.
- `defines(...)`, `link_libs(...)`, `link_paths(...)` propagate **only from
  `Interface` targets** — the CMake `INTERFACE` semantics. A static library's
  own `-D` flags stay private to it; use `public_defines()` or an
  `add_interface_library("config")` target to share them.

Wrapping a third-party library once, instead of in every consumer:

```cpp
auto& yoga = proj.add_static_library("yoga")
                 .sources_in("vendor/yoga/yoga")
                 .warnings(truss::Warnings::None)
                 .system_include_dirs("vendor/yoga")      // consumers: -isystem
                 .public_defines("YG_ENABLE_EVENTS=0");   // consumers: -D too
proj.add_executable("app").sources_in("src").depends_on(yoga);
```
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
| `modules_mode(...)` | `Off, Auto, Manual` | `Auto` (see [C++ modules](#c-modules)) |
| `pch("hdr.hpp")` | precompiled header for this target | off (see below) |
| `unity(bool)` | batch sources into 16-source TUs | off (see below) |
| `cross_target(triple)` | see [Cross-compiling](#cross-compiling--msvc) | host |
| `is_test(bool)` | mark as a test for the `test` command | false |
| `flags("...")` | raw compiler flags, verbatim (repeat calls accumulate) | — |
| `linker_flags("...")` | raw linker flags, verbatim (repeat calls accumulate) | — |

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
`-I`/`-include`, with the PCH directory searched **ahead of every other
include dir** so the `.gch` is picked up even when the header's own directory
is also an include dir. The PCH output lives under `build/<mode>/pch/<target>/` and
is dependency-tracked like any other edge (editing the PCH header rebuilds
the target's objects; editing one source rebuilds only that object). Module
BMI rules and C sources never use the PCH, and MSVC targets are a clear
`Config` error. The header path must be path-safe (no spaces, shell or
ninja metacharacters — a `Config` error otherwise).

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

### Static linking

Three opt-ins, validated at `build()` against the toolchain:

```cpp
auto zlib = proj.vcpkg({"zlib"});
zlib->static_link();                       // this dependency: -Wl,-Bstatic -lz -Wl,-Bdynamic

proj.add_executable("tool")
    .sources_in("src")
    .depends_on(zlib)
    .static_deps()                         // every dependency, transitively
    .static_stdlib(truss::StaticStdlib::Gcc);   // + libstdc++/libgcc -> only glibc stays dynamic
```

- `static_link()` (on the dependency; `static` is a keyword): Interface
  targets get their `link_libs` wrapped in `-Bstatic`/`-Bdynamic`; a
  `SharedLib` is built as a static archive instead of a `.so`.
- `static_deps()` (on the consumer): its own and inherited `link_libs` go
  through `-Bstatic`, and `SharedLib` dependencies are linked from a *static
  twin* — an extra archive over the same PIC objects, no recompilation. The C
  runtime family (`c`, `m`, `pthread`, `dl`, `rt`, …) stays dynamic.
- `static_stdlib(StaticStdlib)`: `Gcc` = `-static-libstdc++ -static-libgcc`;
  `Llvm` = libc++ + libc++abi archives (needs clang + `stdlib(Libcxx)`, or zig);
  `Musl` = fully static (`-static`) and needs a musl toolchain — zig with a
  `*-linux-musl` target, or a `*-musl-g++` driver. Mixing runtimes is refused
  (e.g. `Gcc` on a libc++ toolchain) since it would be an ABI break.

`-Bstatic` needs GNU-ld semantics (Linux, BSD, Android, MinGW): macOS and MSVC
targets get a Config error. The `.a` files must exist — distro packages often
ship only the `.so`.

### Merged archives

`merge()` combines several static libraries' archives into one `.a` (via an
`ar -M` MRI script — GNU ar / llvm-ar / zig ar), consumable like any static
library:

```cpp
auto& a = proj.add_static_library("a").sources_in("src/a");
auto& b = proj.add_static_library("b").sources_in("src/b");
proj.add_static_library("combined").merge(a).merge(b);   // one libcombined.a
proj.add_executable("app").sources_in("src/app").depends_on(combined);
```

Members are merged exactly as listed (their own transitive deps are not pulled
in — `depends_on()` those separately). A merged library needs no sources of its
own; each member must itself be a static library. MSVC targets are a Config
error.

### User tasks

`proj.task(name, description, fn)` registers a **user task**, run with
`./truss task <name>`; the description is optional and shown by
`./truss task list`:

```cpp
proj.task("hello", "print the task context", [](const truss::TaskContext& ctx) -> truss::Result<void> {
    std::printf("[TASK] project=%s mode=%s build_dir=%s\n", ctx.project.c_str(),
                ctx.mode == truss::BuildMode::Debug ? "debug" : "release",
                ctx.build_dir.string().c_str());
    return {};
});
```

```text
$ ./truss task list
tasks (run with ./truss task <name>):
  hello  print the task context
$ ./truss release task hello -j8   # builds first, then runs the task
```

User tasks live in their own namespace, so a task may be called `test` or
`run` without clashing with the built-in commands. Typing a task name without
`task` (`./truss hello`) is an error that points to the right command; `list`
is a reserved name and registering a name twice is a Config error.

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
./truss [debug|release] [--target X] <command>
   │
   ├─ resolve: targets resolved, Config applied, manifest written
   ├─ pre_build hooks   (registration order; a failure aborts)
   ├─ samurai build     (the actual compile + link)
   ├─ post_build hooks  (once, after the first successful build)
   └─ command execution (test binaries, run, named tasks)
```

Hooks fire once per invocation — including partial builds (`--target`,
`test`): a hook that reads artifacts outside the requested graph must
tolerate them being absent. `clean` and `compdb` never trigger them, and a
dry run (`-n`) has no side effects at all.

### Compilation Database

`proj.compile_commands(path)` generates a `compile_commands.json` file (Clangd, VSCode, etc.):

```cpp
proj.compile_commands();  // writes to project root
proj.compile_commands("build/compile_commands.json");  // custom path
```

This is also available as the built-in `compdb` command: `./truss compdb`.
Named tasks (`./truss task <name>`) stay manual commands — hooks are for what
must always run around the build itself.

## Scripting helpers (os / fs / log)

Your build script is C++, so truss also gives you the boring parts of shell
scripting as plain library calls — for `task()` bodies, hooks, and configure
logic. Everything is `std`-compatible (`path`, `optional`, `span`): "not
found" is `nullopt`, never an error; `Result<T>` only appears where I/O or
process spawning can genuinely fail. No exceptions.

```cpp
proj.task("dist", [](const truss::TaskContext&) -> truss::Result<void> {
    // os: platform facts, programs, environment, processes
    if constexpr (truss::os::is_linux()) { /* compiled out elsewhere */ }
    if (!truss::os::any_program({"git", "tar"}))
        return std::unexpected(Error {Error::Kind::Config, "need git + tar"});
    auto version = truss::os::capture({"git", "describe", "--tags"});
    if (!version) return std::unexpected(version.error());
    auto jobs = truss::os::env("TRUSS_JOBS", 8u);          // lenient parse

    // fs: find, walk, read/write, hash
    for (auto f : truss::fs::walk("src", {.depth = 2}))    // lazy, std::ranges-ready
        truss::log::info(truss::fs::generic_str(f));
    auto readme = truss::fs::read_text("README.MD");       // Result<string>
    truss::fs::write_text_if_changed("dist/VERSION", version->output); // mtime-stable

    // log: same colored channel truss itself uses
    truss::log::success("dist ready for " + *truss::os::hostname());
    return {};
});
```

At a glance:

| Helper | Notes |
| --- | --- |
| `os::host_os/host_arch/host_family`, `is_linux/is_windows/is_macos` | `consteval` — usable in `if constexpr` |
| `os::find_program` / `program_exists` / `all_programs` / `any_program` | fork-free `$PATH` walk, memoized |
| `os::env(name)` / `os::env(name, def)` | `optional<string>`; typed lenient parse with default |
| `os::cpu_count` / `memory_bytes` / `os_version` / `hostname` / `temp_dir` | runtime machine facts |
| `os::run` / `os::capture` (+ `RunOptions{.cwd, .timeout}`) | spawn; non-zero exit is a result, spawn failure an `Error`; the timeout (default 600 s) kills the child's whole process group |
| `fs::glob` / `glob({...})` | `*` `**` `?` `{a,b}`, sorted, forgiving |
| `fs::walk(dir, {.depth, .skip_hidden})` | lazy `truss::generator`, composes with ranges |
| `fs::find_upward` / `first_existing` / `find_library` | workspace-root discovery, library file lookup |
| `fs::read_text` / `write_text` / `write_text_if_changed` / `read_lines` | whole-file text I/O |
| `fs::sha256_file` | the same digest `fetch()` pinning uses |
| `fs::current_dir` / `copy_tree` / `generic_str` | misc path work |
| `log::info/warn/success/error` | truss's own colored output channel |

Windows-specific probes currently return `nullopt`/`0` — truss is a
POSIX-host tool (Linux is the tested host).

## C++ modules

Your project's `.cppm` files are handled automatically. Each interface unit
gets a BMI/CMI compile edge, and every other unit in the target — and in
targets that `depends_on()` it — is wired with the **full transitive
closure**: if you `import A;` and `A` imports `B` (whether `export import`
or a plain `import`), `B`'s BMI is threaded through too, because the
compiler needs every BMI a consumed BMI refers to.

**How imports are discovered** (`modules_mode`, default `Auto`):

1. **The compiler's own scan (authoritative, the default when available)** —
   clang via `clang-scan-deps`, GCC via `-fdeps-format=p1689r5`, run at
   configure time in parallel (~10 ms per TU, then cached: see below). The
   import lists are the compiler's own: exact partition names, imports the
   text scan cannot see (line-spliced `import \` continuations, macro-gated
   imports). A TU that includes a not-yet-generated header (the codegen
   pattern) cannot be preprocessed — truss falls back to the text scan for
   that unit with a warning.
2. **The text scan (fallback, and the GCC<14/clang-without-scan-deps path)** —
   a hand-rolled line scanner over each file, after comments, strings, char
   literals and raw strings have been blanked: `import`/`module`
   declarations only count when they are real code. Documented limitation:
   line-spliced imports are missed (the compiler scan finds them).

`modules_mode(Manual)` demands the compiler scan (clang or GCC; a clear
error otherwise). `modules_mode(Off)` disables the wiring (`.cppm` then
errors rather than being silently miscompiled). Header-unit imports
(`import <vector>;` / `import "x.h";`) are **not supported** — truss fails
with an explicit Config error telling you to `#include` the header instead
(the compilers' scanners cannot report header units reliably yet).

**Scan results are cached** in `build/<mode>/scan-cache/`, keyed by source
path, mtime, size, compile flags and compiler identity. An unchanged tree
performs no scans at all, and a TU whose raw text contains neither `import`
nor `module` is never handed to the compiler (the keywords cannot come from a
macro, so the answer is provably empty). Editing a source rescans only that
source.

**GCC specifics**: CMIs (`.gcm`) are routed through a per-target
module-mapper file (`build/<mode>/modmap/<triple>/<target>.map`) — no
`gcm.cache` in your source tree.

**`import std;`** is built for real on clang + libc++ **and GCC 15+**
(libstdc++), C++23 or later: truss discovers the standard library's module
manifest, compiles the `std` (and `std.compat`) BMI/CMI + object once per
(triple, `-std`, hardening/sanitizer/LTO config) and caches it, wires the
consumers and links the std object once per binary — no manual setup. zig,
MSVC and clang + libstdc++ give a clear Config error today.

```cpp
proj.add_static_library("math").sources_in("src/math"); // src/math/math.cppm inside
proj.add_executable("calc").sources("src/calc.cpp").depends_on(math);
// calc.cpp's `import math;` just works
```

## Dependencies

### add_subdirectory

Convention-based vendoring — point it at a directory in your tree:

```cpp
auto zlib = proj.add_subdirectory("vendor/zlib");   // Result<TargetRef>
if (!zlib) return 1;
proj.add_executable("app").sources_in("src").depends_on(zlib);
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
proj.add_executable("app").depends_on(fmt);

// several packages at once (downloads overlap):
auto pkgs = proj.fetch_all({
    {"fmt",  truss::FetchOptions{.git = "https://github.com/fmtlib/fmt", .tag = "11.0.2"}},
    {"zlib", truss::FetchOptions{.url = "https://zlib.net/zlib-1.3.1.tar.gz",
                                 .sha256 = "9a93b2b7df..." }},
});
```

Exactly one of `git+tag` (shallow clone pinned to a tag/branch/commit) or
`url+sha256` (`.tar.gz`/`.tar.xz` verified against the hash) must be set.
Downloads are cached in `~/.cache/truss` (the cached tarball's hash is
re-verified on every run — a corrupted cache entry is re-downloaded, never
reused), materialized into `build/<mode>/packages/<name>`, and pinned in a
`truss.lock` file next to your build script — so CI gets the same tree without
a network round trip. Lockfile updates are serialized with an advisory file
lock (a stuck holder errors after 120 s instead of blocking forever).

Downloads are bounded by `FetchOptions::timeout` (default 900 s, `0` =
unlimited) with stall detection: a transfer crawling under 1 KiB/s for 30 s
is aborted, so a stalled mirror cannot hang the build.

Git trees are materialized with hardlinks (instant, no extra disk; real copies
across filesystems). Caveat: a hardlinked fetched file shares its inode with
the cache, so editing it *in place* inside `build/` also edits the cached copy
— compilers replace files rather than write in place, so normal builds never
hit this. Fetched trees (git and tarball alike) are scanned for symlinks
escaping the package directory and refused.

### conan

Conan 2 graphs, consumed as one `Interface` target:

```cpp
auto deps = proj.conan({"zlib/1.3.1", "fmt/11.0.2"}); // list form (refs are versioned)
// or: proj.conan("conanfile.txt") / "conanfile.py" / "."
proj.add_executable("app").sources_in("src").depends_on(deps);
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
proj.add_executable("app").sources_in("src").depends_on(deps);
```

Installs into `build/<mode>/vcpkg_installed/` and returns an `Interface`
target. Requires `VCPKG_ROOT` pointing at a vcpkg tree. As with conan, an
unchanged `vcpkg.json` (sha256 fingerprint) + surviving install tree skips
`vcpkg install` on later invocations.

### Imported prebuilt libraries

Link a library you already have as a file — a vendor drop, an SDK — instead
of raw `-L`/`-l`. `import_library()` names the prebuilt `.a`/`.so`/`.dylib`/
`.lib` file(s); dependents get them on their link line (consumers before
providers, after their objects) plus the usage requirements you set, and an
imported `.so`/`.dylib` adds an rpath to its directory:

```cpp
auto& greet = proj.import_library("greet")
                  .import_archive("prebuilt/libgreet.a")
                  .system_include_dirs("prebuilt/include");
proj.add_executable("app").sources_in("src").depends_on(greet);  // no deref needed
```

The files must exist by `build()` time; truss links them but never builds
them. See `examples/imported`.

### Custom commands (codegen)

Run a command during the build — typically a `for_host()` tool — and feed its
outputs to other targets. `uses(tool)` builds the tool first and expands to
its path as `$tool`; outputs become implicit inputs of every consumer's
compile edges, and each output's directory is added as an include dir:

```cpp
auto& gen = proj.add_executable("gen").sources("tools/gen.cpp").for_host();
auto& out = proj.add_custom_command("generated")
                .command({"$tool", "-o", "$out", "data.txt"})
                .uses(gen)
                .outputs({"gen/config.h"});   // default: build/<mode>/gen/generated/output
proj.add_executable("app").sources_in("src").depends_on(out);
```

See `examples/cross-host` for the host-tool-in-a-cross-build pattern.

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
`toolchain(Toolchain::Zig)` uses `zig c++` / `zig cc` / `zig ar` (clang flag
dialect, zig's bundled libc++; `stdlib()` is refused, the compiler cache is
skipped since ccache does not recognize zig). Explicit calls after
`toolchain()` override it. Selections are validated:
`g++` + `libc++` and any `stdlib()` on MSVC targets are clear Config errors.
`-stdlib=` is emitted on both compile and link lines. Per-target
`cross_target(triple)` still overrides the project default completely, and
`TargetTriple` has a `stdlib` field for per-target selection too.

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

## Subgraphs: several triples, one invocation

`proj.targets({{name, Config}, ...})` builds the whole graph once per named
configuration — one manifest, one `./truss` invocation, outputs in their
per-triple directories:

```cpp
proj.targets({
    {"linux", Config{}.toolchain(Toolchain::LLVM)},
    {"win",   Config{}.toolchain(Toolchain::Zig).target("x86_64-w64-mingw32")},
    {"arm",   Config{}.toolchain(Toolchain::Zig).target("aarch64-linux-musl")},
});
```

- Every target is built for every subgraph; each subgraph's artifacts land in
  their own `bin/<triple>/`, `lib/<triple>/` dirs.
- `./truss test` runs every subgraph's tests, each through that subgraph's
  runner (`Config::runner`): the Linux tests run natively, the Windows ones
  through wine, the musl ones through qemu — and a cross test without a
  runner is built but skipped with a warning (never silently "passed").
- `./truss --for win test` narrows the commands to one subgraph.
- `--target win:app` builds one subgraph's target; a bare `--target app`
  applies to every subgraph that has it.
- The first declared subgraph is the default for `run`.

Subgraph names follow target-name rules (no `:`, `all` reserved) and must be
unique; two subgraphs may not share a triple (their outputs would collide —
a Config error). `targets()` is mutually exclusive with `configure()`: the
per-subgraph Configs *are* the project configuration. `for_host()` tools are
pinned to the host in every subgraph.

## Cross-compiling & MSVC

Cross-compilation is first-class with four routes, all selected through
`Config` (or per-target `cross_target()`):

1. **zig** — `Toolchain::Zig` + `target("aarch64-linux-musl")`: bundled
   sysroots for musl, glibc and Windows-gnu targets; no toolchain to install.
2. **mingw-w64** — `compiler("x86_64-w64-mingw32-g++")
   .target("x86_64-w64-mingw32")`: the target-prefixed GCC driver *is* the
   target selection.
3. **llvm-mingw** — `compiler("x86_64-w64-mingw32-clang++")`: clang-family,
   so **modules and `import std` cross to Windows**; its libc++ runtime dlls
   go on `WINEPATH` for wine.
4. **plain clang / GCC + sysroot** — `.flags("--sysroot=...")`, or any
   prefixed driver (`aarch64-linux-gnu-g++`).

Outputs get the target's naming (`app.exe`, `libx.dll` + import `.dll.a`,
`libx.dylib`) and link conventions (no `-z relro`/`-soname`/`$ORIGIN` off
ELF).

**Host tools in a cross build.** Mark build-time tools with `for_host()` —
they always compile for the host triple, even when the project cross-compiles,
so generators and embedders run on the build machine. A `depends_on()` edge
may not cross the host/target boundary (or join two different cross triples):
different ABI, and truss fails with a Config error naming both sides. Run
cross-compiled tests through a runner — `Config().runner("qemu-aarch64 -L
/sysroot")` or `runner("wine")` — otherwise `./truss test` builds them but
skips them with a warning (never silently "passed"):

```cpp
proj.configure(truss::Config{}.toolchain(truss::Toolchain::Zig).target("aarch64-linux-musl")
                                 .runner("qemu-aarch64"));
auto& gen = proj.add_executable("gen").sources("tools/gen.cpp").for_host();
proj.add_executable("app").sources_in("src");              // cross
proj.add_executable("test_app").sources("tests/app.cpp").is_test();  // runs via qemu
```

Every target defaults to that project triple (`truss::host_triple()` + the
Config). Override per target with a `TargetTriple`:

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
    obj/<triple>/<target>/…   # .o/.obj (+ .d depfiles, bmi/*.pcm | *.gcm)
    modmap/<triple>/          # GCC module-mapper files (per target)
    scan-cache/               # module-scan results (path+mtime+size+flags+compiler)
    bin/<triple>/<exe>        # executables
    lib/<triple>/…            # archives, shared libs (+ import libs)
    std/<triple>-<std>-<cfg>/ # the std module's BMI/CMI + object, cached
    packages/<name>           # fetch() checkouts (packages/conan for conan)
    vcpkg_installed/          # package-manager installs
```

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

`./setup.sh dist` produces the shippable CLI, `build/dist/bin/truss`: the
self-host `truss-dist` target in release mode (`truss init` built in,
`.static_deps().static_stdlib(StaticStdlib::Gcc)`), whose only runtime
dependency is glibc.

`./setup.sh install [prefix]` stages `libtruss.a`, the module BMIs, and the `truss`
binary into `prefix` (default `.local/` relative to the invocation directory). An
installed truss automatically discovers its module files from the installation location.

The discovery can be overridden with the `TRUSS_INSTALL_PREFIX` environment variable,
which should point to the installation prefix (not the lib/truss subdirectory).

### Examples

`./setup.sh examples` compiles every example's `truss.cpp` and runs its flows
end-to-end:

| Example | Shows |
|---|---|
| `examples/hello` | the basic script: static lib + app + test + a custom `task` |
| `examples/shared` | shared libraries, soname + rpath |
| `examples/modules` | C++20/26 modules (`import math;`, `import std;`) with transitive BMI wiring |
| `examples/fetch` | `fetch()` git/tarball packages, pinned in `truss.lock` |
| `examples/conan` / `examples/vcpkg` | package-manager graphs as one `depends_on()` |
| `examples/imported` | `import_library("name").import_archive(...)`: linking a prebuilt `.a` with `system_include_dirs()` + deref-free `depends_on()` |
| `examples/cross-host` | a zig cross build with a `for_host()` codegen tool; host↔cross `depends_on()` is a Config error, cross tests build then skip with a warning |
| `examples/installed` / `examples/amalgam` | consuming an installed truss / the single-header drop-in |

## Status & support matrix

v1.0.0 — the engine is feature-complete and the public API is frozen behind
semver. Windows/macOS *hosts* remain out of scope for now; cross-*targeting*
Windows from Linux is first-class.

| Feature | GCC (15+) | Clang | MSVC (cl.exe / clang-cl) |
|---|---|---|---|
| Executable / static / shared / interface | ✅ | ✅ | ✅ (built via clang-cl cross-build; not yet run on Windows) |
| Globbing, commands, compdb, incremental | ✅ | ✅ | ✅ |
| Sanitizers, LTO, per-target options | ✅ | ✅ | ✅ (no ThinLTO — maps to `/GL`) |
| C++ modules for your project | ✅ (module mapper, compiler p1689 scan) | ✅ (clang-scan-deps) | ❌ (clear error) |
| `import std;` / `import std.compat;` | ✅ (GCC 15+) | ✅ (libc++, C++23+) | ❌ (clear error) |
| `merge()` combined archives | ✅ | ✅ | ❌ (clear error) |
| self-rebuild tracks truss.cpp + its headers + truss BMIs | ✅ | ✅ | — |
| fetch / add_subdirectory / conan / vcpkg | ✅ | ✅ | host-side, same |
| static_link / static_deps / static_stdlib | ✅ | ✅ | ❌ (clear error) |
| per-triple subgraphs (`proj.targets()`) | ✅ | ✅ | ✅ |
| zig cross (`Toolchain::Zig` + `target`) | — | ✅ verified: musl / aarch64-musl / windows-gnu / gnu.2.17 (modules: ❌, Config error) | — |
| mingw cross (Linux → Windows) | ✅ `x86_64-w64-mingw32-g++` + target | ✅ llvm-mingw: modules + `import std`; runs under wine | — |

Known limitations: paths containing spaces or `:` are rejected for sources,
target names and other manifest paths (include dirs are quoted and may
contain spaces); truss runs on POSIX hosts only; zig cannot build C++20
modules or PCH (Config error); header-unit imports are refused with a clear
error (the compilers' scanners cannot report them yet).
