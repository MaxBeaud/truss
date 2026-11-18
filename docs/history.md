# Project history {#history_page}

truss grew out of a single written plan — thirteen phases, worked in order,
each with its own "done when" criterion. The plan itself is retired: every
phase shipped, and its design sketches have either become the code or been
superseded by [Internals](@ref internals_page) and the API reference. This
page keeps the parts that stay useful: **why** the big decisions were made,
what each phase delivered, the risks that were known going in, and how the
engine is tested.

Forward-looking work lives in the [1.0 roadmap](@ref roadmap_page); the user
guide is `README.MD`; `CHANGELOG.md` records what changed when.

## Decisions that still bind

| Topic | Decision | Why |
|---|---|---|
| Consumption | truss is a **C++26 named module** (`import truss;`): `libtruss.a` + BMIs, built by `./setup.sh bootstrap` (plain `clang++`, no build system). | One artifact, no code generator; the build script compiles against the same BMIs a user does. |
| User-project modules | Compiler's p1689 scan where available (`clang-scan-deps`, GCC `-fdeps-format`), hand-rolled text scan as fallback; results cached in `build/<mode>/scan-cache/`. | Exact partition names and macro-gated imports; the text scan cannot see them, and `import` cannot be macro-generated so the skip gate is sound. |
| Samurai | **Vendored C sources** driven in-process through `src/samu/shim.c`. Pinned commit, patches documented in `vendor/samurai/TRUSS-VENDOR.txt`. | No runtime amalgam codegen, no subprocess per build; the shim replaces `samu.c`'s `main()`. |
| Package phasing | `add_subdirectory` → CPM-style `fetch` (git/tarball + lockfile) → `conan`/`vcpkg` as `Interface` targets. | Each layer is additive; the last two only translate an existing graph. |
| Builder API | Chaining only; the pipe composition (`with_*` + `operator|`) was removed. | One obvious way to configure a target; methods are deducing-`this` templates returning the concrete type. |
| Errors | `std::expected<T, Error>` (`truss::Result<T>`) everywhere, no exceptions. | Predictable failure, and the library itself is built with exceptions disabled. |
| Terminology | Built-in **commands** (`test`/`run`/`clean`/`compdb`/`reload`); **user tasks** via `./truss task NAME`. | Commands are the tool's verbs; tasks belong to the project. |
| truss itself | Built by `./setup.sh` (clang + libc++), then dogfooded: `truss.cpp` at the repo root builds truss with truss. | The tool has to be its own hardest customer. |

## Phases as shipped

| Phase | Delivered |
|---|---|
| 0–1 | Scaffolding, vendored samurai + shim; `Error`/`Result`, logging, glob engine, `posix_spawn` runner, triple detection. |
| 2–3 | End-to-end compile/archive/link through one manifest per mode; executables, static and shared libraries; `test`/`run`/`clean`/`compdb`; user tasks. |
| 4 | C++20/26 modules: BMI edges, `clang-scan-deps` closure, `ModulesMode`; later extended to GCC (module mapper) and `import std;`. |
| 5 | Vendoring and CPM-style `fetch`: `~/.cache/truss`, sha256 pinning, `truss.lock`, parallel fetches (`fetch_all`). |
| 6 | `conan`/`vcpkg` as one `Interface` target each, with fingerprint-based configure skips. |
| 7 | Distribution: vendored source + `bootstrap.sh`, installed-tool flow — no prebuilt binaries. |
| 8 | Self-hosting (truss builds truss) and the installed-tool example. |
| 9 | Amalgamation: `build/dist/truss_all.hpp`, the single-header drop-in. |
| 10 | MSVC flag mapping, `/showIncludes` deps (vendored samurai patch), response files, `--explain`, docs, benchmarks. |
| 11 | Hardening tiers, `Config`/`Toolchain`, `Result<TargetRef>`, build hooks, Doxygen reference; xmake, justfile and Catch2 removed in favour of `setup.sh` and doctest. |
| 12 | Performance: benchmark harness, ccache/sccache launcher, package-configure skip, hardlink materialization, PCH, unity builds, precise module wiring. |
| 13 | Scripting helpers: `truss::os`, `truss::fs`, `truss::log`. |
| post-1.0 | GCC modules + `import std`, mingw/llvm-mingw cross builds, per-triple subgraphs (`proj.targets()`), merged archives, imported prebuilt libraries, custom commands (`for_host()`), scan-result caching, live progress output, a security pass over command lines and fetch. |

## Known risks (carried, not eliminated)

| Risk | Mitigation |
|---|---|
| Compiler module support churns (flag names, BMI format, p1689 details). | All module-flag emission is confined to `flags.cpp`/`modules.cpp`; `modules_mode(Manual)` and the text scan remain viable fallbacks; the scan cache is versioned so a format change just invalidates entries. |
| Vendored samurai drifts from upstream. | Pinned commit; the shim uses the documented library surface; every patch is listed in `vendor/samurai/TRUSS-VENDOR.txt` and re-applied deliberately. |
| Scan results go stale. | Compiler-scan entries are keyed by flags + compiler identity and validated by (mtime, size); the text scan by path + (mtime, size). Header content is not part of the key (documented): `clean` forces a rescan. |
| `import std;` interactions with hardening/sanitizer/LTO flags. | The std BMI is built with the consumers' exact BMI-config flags and keyed by them, so a mismatch is a rebuild, never a silent ABI hazard. |
| Scope creep. | Post-1.0 work goes through the roadmap page and is batched per feature with tests; the public API is semver-frozen. |

## Testing strategy

- **Unit** — doctest (vendored), compiled with ASan/UBSan: glob expansion, flag
  tables, ninja emission (golden strings), lockfile parsing, triple/config
  resolution, module scanner + resolver, scan cache, usage propagation, MSVC
  paths, subgraphs.
- **Integration** — scratch projects driven from the unit binary, plus the
  `examples/` flows under `./setup.sh examples` / `./setup.sh test`: exit codes,
  manifest contents, and rebuild minimality (arguing from mtimes).
- **Benchmarks** — `./setup.sh bench` tracks clean/incremental/no-op medians on
  a generated 60-TU fixture against xmake and a raw `make -j` floor, and writes
  JSON under `build/bench/`.
- **Self-host** — `./truss debug` must stay green on every change: truss is its
  own first customer, and its rebuild set (depfile-based) is part of the test.
