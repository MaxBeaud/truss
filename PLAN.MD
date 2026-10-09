# truss — Build System in C++26

> Build your C++ using C++. mate.h API style + xmake-class features + samurai backend.

**Status (2026-10-09):** Phases 0–6 complete. `xmake f -m debug && xmake` builds
`truss-mod` (BMI) + `libtruss.a` + `./truss`; Catch2 unit tests green (40 cases: glob, fs,
proc, triple, flags, golden ninja manifest, module scanner/resolver, lockfile, fetch
validation, usage-requirement propagation); `examples/hello`, `examples/shared`,
`examples/modules` (primary + partition + impl unit), `examples/fetch` (git-tag
header-only + sha256 tarball compiled lib, cached offline), `examples/conan` (conan 2
graph → Interface target, zlib) and `examples/vcpkg` (manifest mode, generated
`vcpkg.json`, zlib) build/run/test/rebuild incrementally through the vendored samurai;
shared libs + rpath + user tasks + `--target` verified; failing test fixtures exit 1.
See AGENT.MD §10–13 for verified toolchain facts.

## 0. Decisions (confirmed with user)

| Topic | Decision |
|---|---|
| Consumption | truss is a **C++20/26 named module** (`export module truss;`), built once by xmake into `libtruss.a` + BMI. User's build script does `import truss;` and is compiled to `./truss`. |
| User-project modules | **Automatic** module dependency discovery via `clang-scan-deps` (with a conservative "all BMIs of the target" fallback for v0.1). |
| Samurai | **Vendored sources** in `vendor/samurai/`, compiled as C17 directly into the truss binary, driven programmatically through a small C shim (no runtime amalgam codegen). |
| Package mgmt phasing | 1) vendored subprojects → 2) CPM-style fetch → 3) conan + vcpkg. |
| CLI | `./truss [debug|release] [tasks...] [-j N] [-v] [--target NAME] [--build-dir PATH]`; built-ins: `test`, `run`, `clean`, `compdb`; unknown names = user-defined tasks. Default mode: `debug`. |
| Errors | `std::expected<T, truss::Error>` (aliased `truss::Result<T>`) **everywhere**, internals and user API. No exceptions in the API. |
| Compilers (user projects) | GCC + Clang first; MSVC mapping designed into the `Target` abstraction, implemented in a later phase. |
| truss itself | clang trunk + libc++, `-std=c++26`, built by xmake (starter `xmake.lua` is the jumping-off point). |

## 1. Target repository layout

```
truss/
├── xmake.lua              # truss's own build (fix + extend the starter)
├── justfile               # keep; add xmake recipes (build/test/run truss)
├── PLAN.MD / AGENT.MD     # this plan + agent constraints (kept at repo root too, once implementation starts)
├── src/
│   ├── truss.cppm         # THE module interface (single public TU): export module truss;
│   ├── core/              # implementation units: module truss; (or impl partitions)
│   │   ├── error.cpp      # Error, Result helpers, fatal()
│   │   ├── log.cpp        # colored logger (info/warn/error/success)
│   │   ├── fs.cpp         # path helpers, globbing, fs ops -> Result
│   │   ├── proc.cpp       # subprocess runner (posix_spawn) -> Result
│   │   ├── cache.cpp      # truss-cache.ini read/write (key=value helper over std types)
│   │   ├── target.cpp     # Target triple: OS/arch/compiler detection + cross rules
│   │   ├── flags.cpp      # FlagBuilder + constexpr per-compiler flag tables
│   │   ├── ninja.cpp      # build.ninja emitter
│   │   ├── modules.cpp    # clang-scan-deps integration, BMI ordering
│   │   ├── packages.cpp   # vendor / fetch / conan / vcpkg
│   │   ├── cli.cpp        # argv parsing, mode/task dispatch
│   │   └── build.cpp      # graph assembly: targets -> ninja edges -> samu run
│   ├── samu/
│   │   └── shim.c         # truss_samu_build(): drives samurai's library entry points
│   └── (tests) tests/     # Catch2 unit tests + fixture projects (see §7)
├── include/               # ONLY if a non-module consumer shim is ever needed; prefer none
├── vendor/
│   ├── samurai/           # vendored samurai (ISC), unmodified upstream + shim note
│   ├── catch2/            # for truss's own tests
│   └── (nlohmann_json/)   # only if conan JSON parsing needs it (phase 6)
├── examples/hello/        # dogfood project: truss.cpp + src/, built by `./truss debug`
└── build/                 # generated (gitignored): debug/ release/
```

`build/<mode>/` internal layout (what truss generates for any project):

```
build/debug/
├── ninja/build.ninja          # single manifest per mode (all targets)
├── .ninja_log  .ninja_deps    # samurai state
├── bin/<arch>-<os>-<compiler>/    # executables (+ dlls on windows)
├── lib/<arch>-<os>-<compiler>/    # static/shared libs
├── obj/<arch>-<os>-<compiler>/<target-stem>/   # .o / .pcm / .d
├── packages/<name>/           # fetched source deps (CPM-style)
└── compile_commands.json      # via `./truss compdb`
```

## 2. Public API sketch (module `truss`)

```cpp
export module truss;

export namespace truss {

struct Error {                      // value error type, no exceptions
    enum class Kind { Io, Config, Command, Fetch, Toolchain, ... };
    Kind kind; std::string message; std::optional<std::filesystem::path> path;
};
template <class T> using Result = std::expected<T, Error>;

enum class CppStandard { cpp17, cpp20, cpp23, cpp26 };
enum class Warnings { none, minimal, all, pedantic };
enum class Optimization { none, o1, o2, size, o3 };
enum class DebugInfo { none, minimal, medium, full };
enum class Sanitizer { none, address, ub, all };
enum class Lto { none, full, thin };

class TargetTriple { /* os, arch, compiler path, family, ar; host default */ };

class TargetBase;                   // common options + ninja emission
class Executable;  class StaticLib;  class SharedLib;   // concrete targets

class Project {
public:
    Project(std::string name, std::span<char* const> argv);  // parses CLI here

    Executable&  add_executable(std::string name);
    StaticLib&   add_static_library(std::string name);
    SharedLib&   add_shared_library(std::string name);

    Result<void> add_subdirectory(std::filesystem::path dir);          // vendored deps
    Result<Package> fetch(std::string_view name, const FetchOptions&); // CPM-style (phase 5)
    Result<void> conan(std::filesystem::path conanfile);               // phase 6
    Result<void> vcpkg(std::span<const std::string> packages);         // phase 6

    void task(std::string name, TaskFn fn);        // user-defined ./truss <name>

    Result<void> build();                          // generate + run samurai + run tasks
};

}
```

Builder + pipe composition (both styles, same semantics):

```cpp
// chaining (mate-style fluent)
auto& core = proj.add_static_library("core")
                 .include_dirs("include")
                 .sources_in("src/core");

// pipe (free-function adapters, operator| on builders)
auto& app = proj.add_executable("app")
              | truss::with_sources("src/app/**.cpp")
              | truss::with_depends_on(core)
              | truss::with_cpp_standard(truss::CppStandard::cpp23);
```

Key builder methods (on every target): `sources(...)`, `sources_in(dir)`, `remove_source(...)`,
`include_dirs(...)`, `defines(...)`, `link_libs(...)`, `link_paths(...)`, `link_frameworks(...)`,
`framework_paths(...)`, `linker_flags(...)`, `flags(...)`, `cpp_standard(...)`, `warnings(...)`,
`optimization(...)`, `debug_info(...)`, `sanitizer(...)`, `lto(...)`, `pic(...)`,
`target(TargetTriple)` (cross-compile), `depends_on(Target&)`, `is_test()` (marks test exe).

Globbing: `sources("src/**.cpp")`, `sources("src/*.cpp")`; `sources_in(dir)` == `dir + "/**.cpp"`
(recursive). Matcher supports `*` (within a path component), `**` (across components), `?`,
`{a,b}` alternation. Skips hidden dirs and `build/`. Resolution happens at generation time
(so new files are picked up on re-run; samurai depfiles handle per-file header deps).

## 3. Internal architecture notes

### 3.1 Module structure
- `src/truss.cppm`: `export module truss;` — the single public interface TU. Declares all public
  types/functions. Includes only what's needed for the interface.
- Implementation units: `module truss;` implementation units (`.cpp` in `src/core/`) defining the
  declared functions; pure-internal helpers live in unnamed-namespace/detail headers or impl
  partitions (`module truss:fs;`) if cross-unit internal sharing is needed.
- Vendored samurai is C: compiled as C17 by xmake; bridged via `extern "C"` shim
  (`src/samu/shim.c` + a tiny header). Upstream samu files stay **unmodified**; the shim
  re-implements samu's `main()` driver (graphinit/envinit/parse/loginit/depsinit/buildadd/build/
  logclose/depsclose) and sets `buildopts` (maxjobs, verbose, maxfail, statusfmt) programmatically.
  `tools.c` is kept so `-t compdb` works for `./truss compdb`.

### 3.2 Ninja generation (one manifest per mode)
- Rules: `compile_c`, `compile_cxx`, `compile_bmi`, `archive`, `link_exe`, `link_shared`, plus
  `rspfile` support for long command lines.
- GCC/Clang compile rule: `-MMD -MF $depfile`, `deps = gcc`. MSVC (later): `/showIncludes`,
  `deps = msvc`.
- Cross-target deps: exe/lib edges take other targets' output paths as inputs → samurai orders
  and parallelizes everything in one run (better than mate's per-target sequential runs).
- Phony targets: `all`, `test`, `run`, user task names → `default` line selects per CLI invocation.
- Tasks (`test`, `run`, custom) are executed by truss **after** the relevant samu sub-build
  succeeds (truss runs the binaries, captures exit codes → `Result`).

### 3.3 C++ modules for user projects
- Every `.cppm` in a target's sources gets a `compile_bmi` edge producing `.pcm` + `.o`.
- v0.1 (bootstrap): every TU of a target depends on all BMIs of that target (correct, slightly
  over-depending).
- v0.2: run `clang-scan-deps` over the target's sources (parallel), parse dep tuples, map
  imported module names → BMI files, emit precise edges. Header hygiene via scan-deps too.
- BMI args: `-fmodule-file=<name>=<path.pcm>` on importers; interface units emit both BMI and
  object. Keep a `ModulesMode { off, manual, auto }` per target so behavior is debuggable.

### 3.4 Packages
- `add_subdirectory(dir)`: parses nothing — the vendored project exposes a `truss.cppm`-style
  `truss_subdir(Project&)` function, or (fallback) truss auto-creates a static lib from
  `dir/src/**.cpp` + `dir/include`. CMake-based vendored projects: later phase, optional shim.
- `fetch(name, {git|url, tag/commit, sha256})`: clone/download into `~/.cache/truss/` (content
  addressable), then materialize into `build/<mode>/packages/<name>/`. Lockfile `truss.lock`
  (simple key=value) pins resolved commits. Parallel fetches via internal jthread pool.
- conan (phase 6): run `conan install --format=json`, parse the dependency-graph JSON
  (vendor nlohmann/json), translate include dirs/libs/flags into target options.
- vcpkg (phase 6): run `vcpkg install`, consume `vcpkg_installed/<triplet>/{include,lib}` layout.

### 3.5 Parallelism (only where it pays)
- Glob scanning, `clang-scan-deps` invocations, package fetches: internal
  `parallel_for` over `std::jthread` work queue (size = hardware_concurrency).
- Compilation parallelism itself is samurai's job (do not duplicate it).
- Revisit `std::execution` once libc++ ships it unflagged; do not depend on it now.

### 3.6 Bootstrap & dogfooding
- Stage 0: `xmake f -m debug && xmake` → `libtruss.a` + BMI + `truss-cli` (compiles the
  project's own `truss.cpp` if present).
- Stage 1: `./truss debug` builds the project (and, for the truss repo itself, rebuilds truss
  with truss — dogfood target `examples/` + self-hosted `truss.cpp` in a later phase).

## 4. xmake.lua changes (starter → real)

1. Fix `set_kind("librairy")` → `set_kind("static")` (target `truss-lib`).
2. Add `vendor/samurai/**/*.c` + `src/samu/shim.c` with `set_languages("c17")` on those files.
3. Keep `c++.build.modules` rule + `build.c++.modules` policy for `truss-lib`.
4. Add `truss-cli` target: `truss.cpp` at repo root (or `src/main.cpp`) importing `truss`,
   linking `truss-lib`.
5. Add `tests` target (Catch2 from vendor) — runs with ASan/UBSan in debug (already configured).
6. Keep mold detection, hardening, sanitizers, `plugin.compile_commands.autoupdate`.
7. `justfile`: add `build [mode]`, `test`, `run` recipes wrapping xmake.

## 5. Phased implementation plan

### Phase 0 — Scaffolding (½ day)
1. `git init` layout: `src/`, `tests/`, `vendor/`, `examples/hello/`, `.gitignore` (`build/`).
2. Vendor samurai: `just add-package samurai` (or `git clone` into `vendor/samurai`, pin a tag,
   record provenance in `vendor/samurai/TRUSS-VENDOR.txt`).
3. Fix `xmake.lua` (§4 items 1–3); verify `xmake f -m debug && xmake` compiles an empty
   `truss.cppm` + hello `truss.cpp` (`import truss; int main(){}`).
4. Commit: "chore: scaffold truss module + vendor samurai".

**Done when:** `xmake` builds `libtruss.a` + `./truss` printing a placeholder message.

### Phase 1 — Core utilities (1–2 days)
1. `Error`/`Result`, `log` (colored, mate-style levels), `fatal` path.
2. `fs`: `path_join`, `normalize`, `absolute`, `exists`, `read/write file`, `mkdirs`,
   `list_dir` — all `Result`-based over `std::filesystem`/`std::string_view`.
3. Glob matcher (`*`, `**`, `?`, `{a,b}`) + recursive `glob(path) -> Result<std::vector<path>>`
   (parallel scan when > N dirs). Unit-tested heavily.
4. `proc`: `run(argv, opts) -> Result<ExitStatus>` via `posix_spawn`, output capture;
   `which(program)` helper.
5. `cache`: tiny key=value file (mate's ini equivalent) for truss state.
6. `TargetTriple`: host detection (OS/arch/compiler family from `__clang__` etc. + explicit
   override), cross-compile validation rules (mate's CreateTarget asserts → `Result` errors).

**Done when:** unit tests green for glob, fs, proc, triple.

### Phase 2 — Minimal end-to-end build (2–3 days)
1. `Project` + `Executable`/`StaticLib` targets with options + `depends_on`.
2. `FlagBuilder` + constexpr flag tables (warnings/debug/opt/std/sanitizers/error-format)
   for GCC & Clang; MSVC tables stubbed behind the same interface.
3. `ninja` emitter: variables, rules (compile/archive/link), per-source edges with depfiles,
   link edge, phony `all`, `default`.
4. `samu` shim: drive vendored samurai in-process; wire `buildopts` (-j, -v, -k).
5. CLI: mode selection, `-j`, `-v`, `--build-dir`, `--target`; build dirs `build/<mode>/...`.
6. `examples/hello`: static lib + exe, glob sources, `./truss debug` → runs.

**Done when:** `examples/hello` builds incrementally (touch a file → only it recompiles;
change a flag → relink; `./truss release` builds into `build/release/`).

### Phase 3 — Targets, tasks, shared libs (2 days)
1. `SharedLib` (soname/install_name/rpath handling like mate), `ObjectLib` if needed.
2. Cross-target linking: `depends_on(lib)` adds archive/dylib edges + `-L/-l` or direct paths;
   rpath to lib dir; MSVC import libs stubbed.
3. Tasks: `test` (build + run all `is_test()` exes, aggregate results), `run` (first exe),
   `clean` (delete `build/<mode>`), `compdb` (samu `-t compdb` → `compile_commands.json`).
4. User tasks: `proj.task("fmt", fn)` with `TaskContext` (mode, build dir, flags).

**Done when:** `./truss test` builds and runs a failing/passing test fixture correctly.

### Phase 4 — C++ modules for user projects (3–4 days)
1. `.cppm` detection per target; `compile_bmi` rule (BMI + object); v0.1 all-BMIs-per-target
   dependency mode.
2. `clang-scan-deps` integration (parallel), dep-tuple parsing, precise BMI edges, header
   deps; `ModulesMode {off, manual, auto}`.
3. `import std;` passthrough option; BMI cache dir under `obj/.../bmi/`.
4. Fixtures: partitioned module lib + importing exe; incremental edit of a partition →
   correct rebuild set.

**Done when:** fixture with `export module core:part;` + `import core;` builds, relinks
minimally, and `./truss compdb` still works.

### Phase 5 — Vendoring + CPM-style fetch (2–3 days)
1. `add_subdirectory(dir)`: convention-based vendored projects (auto static lib from
   `src/**.cpp` + `include/`, or explicit `truss_subdir(Project&)` hook).
2. `fetch(name, opts)`: git (shallow clone at tag/commit) + tarball (curl + sha256),
   `~/.cache/truss` content cache, materialize into `build/<mode>/packages/<name>/`,
   `truss.lock` pinning, parallel fetches.
3. `Package` object usable as `add_subdirectory` input.

**Done when:** fixture fetches a tiny header-only lib and a compiled lib from GitHub tags,
re-builds offline from cache.

### Phase 6 — conan + vcpkg (2–3 days) ✅
1. conan: `proj.conan("conanfile.txt")` → `conan install --format=json --build=missing`
   (stdout-only capture; progress goes to stderr) → parse graph JSON (vendored
   nlohmann/json) → one `Interface` target "conan" carrying every package node's
   `cpp_info.root` includes/libs/system_libs/defines, relative dirs resolved against
   `package_folder`. ✅
2. vcpkg: `proj.vcpkg({"zlib"})` → manifest mode (generates `vcpkg.json` when absent),
   `--x-install-root build/{mode}/vcpkg_installed`, triplet `x64-linux` → `Interface`
   target "vcpkg" from the triplet's `include/` + `lib/` trees. ✅
3. Usage semantics: includes propagate from all deps; defines/link flags only from
   `Interface` deps (CMake INTERFACE semantics) — `collect_usage` in `build.cpp`. ✅

**Done when:** `examples/conan` + `examples/vcpkg` (zlib each) build/run/test green. ✅

### Phase 7 — Distribution: vendored source + bootstrap.sh (1–2 days)
1. `bootstrap.sh`: compile `libtruss.a` + BMI with plain clang++ (no xmake required),
   vendored samurai included; `--install [prefix]` stages binary + libtruss.a + BMI.
2. Vendored-source repo stays the "simple one import" path: clone, run bootstrap, write
   `truss.cpp`.

**Done when:** a clean machine path (git clone → `./bootstrap.sh` → example builds) works
without xmake installed.

### Phase 8 — Self-hosting + installed-tool flow (2 days)
1. Dogfood: truss builds truss (`truss.cpp` at repo root) using truss's own module
   support; xmake remains only the bootstrap fallback.
2. Installed-tool flow: `truss` compiled on the user's machine (no prebuilt binaries);
   truss compiles a project's `truss.cpp` against the locally staged BMI — fixture
   `examples/installed` (contains only `truss.cpp`).

**Done when:** `examples/installed` (no vendored truss source) builds against an installed
truss.

### Phase 9 — Amalgamation (1 day)
1. `tools/amalgamate` → `dist/truss_all.hpp`: mate.h-style single-header drop-in
   (`#include "truss_all.hpp"` instead of `import truss;`).
2. CI compiles the amalgamated header against an example to keep it honest.

### Phase 10 — Polish (ongoing)
1. MSVC flag mapping + `/showIncludes` deps (design already reserved in `Target`).
2. rsp files for long lines; `--explain` passthrough to samu debug flags.
3. Docs: README with mate-style guide; `examples/` per feature.
4. Benchmarks: clean + incremental build times vs xmake on a fixture.

## 6. Risks & mitigations

| Risk | Mitigation |
|---|---|
| clang C++26 modules churn (`-std=c++26` + named modules still moving) | Pin clang trunk via justfile note; keep `ModulesMode::manual` fallback; isolate all module-flag emission in `flags.cpp`/`modules.cpp`. |
| samurai API drift on vendoring | Pin a tag; shim only uses the documented amalgam surface (same symbols mate.h relies on). |
| `import std;` + libc++ hardening + sanitizers interactions | truss's own build already configures this in xmake.lua; keep module interface includes minimal. |
| scan-deps correctness | v0.1 conservative mode stays available; fixture-based rebuild-set assertions. |
| Scope creep (conan/vcpkg) | Strict phase order; phases 5–6 are additive, never block core. |

## 7. Testing strategy

- **Unit** (Catch2, vendored): glob matcher, path utils, flag tables, ninja emitter (golden
  strings), cache/lockfile, triple detection.
- **Integration**: fixture projects under `tests/fixtures/` driven by a test harness that runs
  `./truss <mode> <task>` and asserts on outputs, exit codes, and rebuild minimality
  (file-mtime-based assertions).
- truss's own debug build keeps ASan/UBSan (already in xmake.lua).
