# AGENT.MD — Constraints & Guidelines for working on truss

This file is the rulebook for any agent/human modifying this repo. Background:
`README.MD` (user guide), `docs/internals.md` (architecture),
`docs/history.md` (why decisions were made; all original phases are complete),
`docs/1.0-roadmap.md` (what comes next).

## 1. Project identity

- truss = a C++26 build-system library ("build your C++ using C++"), fluent builder
  API inspired by mate.h, samurai backend.
- truss itself: **clang (trunk-pinned) + libc++ + `-std=c++26`**, built by **`./setup.sh`**
  (plain clang++, no build system). Never introduce CMake or another build system for
  truss itself.
- Dogfood target: truss's own build script is `truss.cpp` at repo root (phase 7).

## 2. Language & style constraints

- **C++26, modern idioms required**: concepts, `constexpr`/`consteval` where feasible,
  `std::expected`, `std::format`, ranges, structured bindings, `std::span`, designated
  initializers, `[[nodiscard]]` on fallible functions.
- **Errors**: `truss::Result<T> = std::expected<T, truss::Error>` for ALL fallible operations
  (fs, subprocess, fetch, config validation) — internals AND public API.
  - No exceptions across module boundaries; no `throw` in library code.
  - mate.h-style rich multi-line error messages (with correct/incorrect examples) go in
    `Error::message`.
- **Memory**: no raw `new`/`delete`/`malloc` in C++ code. Prefer values, `std::vector`,
  `std::string`, `std::string_view`, `std::filesystem::path`; smart pointers
  (`std::unique_ptr` default, `std::shared_ptr` only for genuine shared ownership) only when
  a value type won't do. **Minimize copies**: pass by `const&`/`string_view`/`span`,
  `std::move` on sinks, `emplace_back`, string building via a single reserve-and-append
  helper (ninja emitter), never `std::string + std::string` in hot loops.
- **No custom string/arena types** — std/STL covers it. Helper *functions* over std types
  are encouraged (e.g. `join_path`, `glob`, `starts_with` polyfills only if missing).
- **API style**: classes + builder pattern. Every builder method returns `Self&` for
  chaining. (Pipe adapters (`with_*` + `operator|`) were removed in phase 11 — one
  obvious way to configure a target.)
- **Parallelism only where it pays**: glob scanning, `clang-scan-deps`, package fetches use
  the internal `parallel_for` (jthread pool). Do NOT parallelize what samurai already
  parallelizes (compilation). Do NOT use `std::execution` (not stable in libc++ yet).
  Every parallel site needs a comment justifying why.
- **Naming**: types `PascalCase`, functions/methods `snake_case` (mate.h heritage + xmake
  friendliness), constants `kPascalCase` or `UPPER_SNAKE` for enum values matching the
  public enums in the API reference (`src/api.cppm`). Namespaces: everything under
  `truss::`, internals in
  `truss::detail` or unnamed namespaces in implementation units.
- **Warnings are errors-adjacent**: the setup.sh warning set (`-Wall -Wextra -Wpedantic
  -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast ...`) must stay clean. No C-style
  casts (use `static_cast`/`reinterpret_cast` with comment). No `using namespace std;`.

## 3. Module architecture rules

- `src/truss.cppm` is the **single public module interface TU** (`export module truss;`).
  - Public API additions go here; keep includes minimal (only what the interface needs).
  - Never `export` internals. Implementation lives in `src/core/*.cpp` as
    `module truss;` implementation units; cross-unit internal sharing uses impl partitions
    (`module truss:xxx;`) — sparingly.
- Dependency direction inside the module (no cycles):
  `error/log` ← `fs/proc/cache` ← `target/flags` ← `ninja/modules` ← `packages` ←
  `build/cli` (Project). If a cycle appears, split the type, don't forward-declare around it.
- Vendored samurai is C: compiled as C17, **never modified**. All contact goes through
  `src/samu/shim.c` + one `extern "C"` header. Upstream provenance recorded in
  `vendor/<pkg>/TRUSS-VENDOR.txt` (repo URL, tag, commit, license).

## 4. Vendoring & packages

- Third-party code truss itself needs → `vendor/` (setup.sh `add-package` symlink flow or
  git clone pinned to a tag). Currently allowed: `samurai` (ISC); tests use vendored
  `doctest` (MIT, single header in `tests/doctest/`).
  Anything else needs a justification comment in the vendor note file.
- Do not vendor a JSON parser until phase 6 actually needs it.
- Generated/fetched artifacts for USER projects go under `build/<mode>/packages/` — never
  into the source tree. truss's global download cache: `~/.cache/truss/`.

## 5. Build & workflow

- Build truss: `./setup.sh build` (bootstrap + self-host `./truss`). Everything generated
  lives under `build/` (`build/bootstrap` = libtruss.a + BMIs, `build/dist` = amalgamation).
- Run tests: `./setup.sh test` (doctest unit tests with ASan/UBSan + all example flows).
- Never commit `build/`, `compile_commands.json`, `truss.lock` churn (gitignore covers).
- **Batch discipline**: planned work is tracked in `docs/1.0-roadmap.md`; finish one
  item (with tests) before starting the next. Small commits, prefix: `feat:`, `fix:`,
  `test:`, `chore:`, `docs:`, `refactor:`.
- Every change keeps `examples/*` and `tests/fixtures/*` building green.

## 6. Testing rules

- New utility (glob, flag table, ninja emitter, triple detection) ⇒ doctest unit test in the
  same commit. Ninja emitter tests assert on **golden manifest strings**.
- Rebuild-minimality is a tested property: integration fixtures assert that touching file X
  recompiles exactly the expected object set (via samu `-d explain` output or mtimes).
- Integration harness invokes the real `./truss` binary; no mocking of samurai.
- Keep ASan/UBSan green in debug CI-equivalent (`./setup.sh test`).

## 7. Public API invariants (do not break casually)

- `Project` constructor parses CLI; `build()` is the only entry that touches disk/network.
- All target option methods are validated at `build()` time with mate-style actionable
  errors (e.g. output name must not contain path separators or platform extensions).
- Output naming rules (from mate.h, keep): user gives bare name (`"app"`), truss adds
  platform extension + `lib` prefix for libs; outputs land in
  `build/<mode>/{bin,lib}/<arch>-<os>-<compiler>/`.
- Build dirs: default `build/debug/` and `build/release/`, overridable via
  `--build-dir` / Project options. Packages: `build/<mode>/packages/`.
- CLI contract: `./truss [debug|release] [tasks...] [-j N] [-v] [--target NAME]
  [--build-dir PATH] [-- ARGS...]`; built-ins `test`, `run`, `clean`, `compdb`, `reload`;
  `-h`/`--help`/`help` print `kHelp` (build.cpp) and exit. User tasks run via
  `task NAME` (stored as `"task:NAME"` in requested_tasks_); `task` / `task list`
  list them; a bare unknown word is an error (with a hint when it names a task).
  `run` honours `--target` and forwards the args after `--`.
- Compiler flag enums map through constexpr tables in `flags.cpp`; GCC + Clang complete,
  MSVC stubs return a `Toolchain` "not yet supported" `Error` (never silent wrong flags).

## 8. Things explicitly OUT of scope (until the roadmap says otherwise)

- MSVC/cl.exe execution path, WASI/emscripten targets, CMake interop for vendored deps,
  package *publishing*, IDE integration beyond `compile_commands.json`,
  `std::execution`/P2300, custom allocators/arenas.

## 9. Open questions to resolve with the user before the relevant phase

- ~~Phase 5: exact `fetch` option surface~~ — resolved with the plan's defaults: both
  git+tag and tarball+sha256 are supported; lockfile is the tiny key=value `truss.lock`
  (mate's ini equivalent).
- ~~Phase 6: conan 2 assumed (v1 EOL) — confirm; vcpkg manifest vs classic mode~~ —
  confirmed: conan 2 only, vcpkg manifest mode.
- Distribution phases: whether `import std;` becomes the default inside truss's own
  module once libc++ + clang trunk combination is verified stable with sanitizers +
  hardening.

## 10. Verified toolchain facts (learned during phases 0–2 — do not re-derive)

- (Historical — xmake was removed in phase 11; truss now builds with plain clang++ via
  `setup.sh`.) xmake 3.1 + clang 23: cross-target named-module consumption requires a
  `set_kind("moduleonly")` target providing the BMIs (`truss-mod`), consumers `add_deps` it.
  Plain `add_deps` on a static lib does NOT propagate the BMI.
- Module units: `#include` must live in the global module fragment (`module;` … includes …
  `module truss;`). Includes in the module purview attach std to `truss` (clang errors).
- Partition layout: `truss:api` (public types, re-exported by the primary interface via
  `export import :api;`) + `truss:impl` (internal helpers, NOT re-exported). Implementation
  units cannot import the primary interface of their own module — that is why all public
  types (incl. `Project`) live in `:api`.
- (Historical, xmake removed.) xmake: `set_config("builddir", ".xmake")` (the `buildir` key
  is deprecated). File exclusion syntax: `add_files("dir/**.c|samu.c")`.
- samurai: `$in` expands to **explicit inputs only** — dependency artifacts (archives,
  shared libs) must be explicit inputs of link edges, ordered after the objects.
- Ninja manifest emission: rule bodies carry `$placeholder` verbatim (never escape them);
  variable *values* must escape `$` → `$$`. `$ORIGIN` in rpaths needs single quotes so the
  shell passes it to ld.
- samurai never frees its graph (process-lifetime design): `__lsan_default_options` in
  `src/samu/shim.c` disables LSan for ASan debug builds. Keep it unconditional (inert
  without ASan).
- Paths containing spaces, `:` or `$` are rejected with a Config error (partial ninja
  escaping); full support is a later hardening item.
- `.cppm`/`.ixx` sources are supported since phase 4 (see §11); with `modules_mode(Off)`
  they are rejected with an actionable Config error.

## 11. Verified toolchain facts (learned during phase 4 — C++ modules for user projects)

- clang 23 BMI flags: producer `clang++ ... -fmodule-output=<pcm> -c x.cppm -o x.o` (object
  AND pcm from one invocation); consumer `-fmodule-file=<name>=<pcm>` — `<name>` may be a
  partition (`math:part`).
- samurai: any file a command produces beyond `$out` (e.g. the `$bmi` pcm) MUST be declared
  as an **implicit output** (`build obj | pcm: rule ...`), else consumers fail with
  "file is missing and not created by any action".
- Consumers of a primary interface need BMIs for **all its partitions** too (the primary BMI
  references them via `export import :part`); truss expands primary imports to partitions.
- Module import wiring is a **full transitive closure** (`resolve_module_imports` +
  `module_imports` map: module -> its imports, for every .cppm in the target and its
  depends_on): direct, re-exported (`export import`) and indirectly imported (plain `import`)
  modules all get `-fmodule-file=`, because clang needs every BMI a consumed BMI refers to.
  `std.compat` pulls `std` via the same mechanism (`module_imports["std.compat"] = {"std"}`).
- An implementation unit (`module m;` in a .cpp) implicitly imports its primary interface —
  it always needs `-fmodule-file=m=...` even with no explicit import.
- v0.1 module scanner (`detail::scan_module_unit`) is a line-anchored regex (no
  preprocessor): `import`/`module` inside block comments or string literals can confuse it;
  clang-scan-deps integration replaces this in a later phase.
- C++ pitfall (caused an infinite loop): `for (...; std::getline(std::istringstream(s), p, ',');)`
  constructs a fresh stream every iteration and never advances. Build the stream once outside.

## 12. Verified toolchain facts (learned during phase 5 — vendoring + fetch)

- C sources must NOT get the C++ `-std=c++XX` flag (clang rejects it: "not allowed with
  'C'"). `FlagSet` carries a separate `compile_c` (`-std=gnu17` + shared flags); the
  `compile_c` ninja edge uses it.
- `add_subdirectory` conventions (validated against real repos):
  - `src/` present -> static lib, sources globbed recursively under `src/`;
  - else top-level `*.{c,cpp,cc,cxx}` present -> static lib from top-level sources only
    (recursive globbing pulls in `cli/`+`tests/` `main()`s, e.g. xxHash — don't do that);
  - else -> `Interface` target (header-only, no artifact; include dirs still propagate).
  - include dirs: `include/` when present, else the package root (root-layout libs).
- `depends_on` now propagates include dirs transitively (`collect_include_dirs`), so
  consumers of fetched/vendored packages get their headers automatically.
- Interface targets: empty output path, no compile/link edges, excluded from `all` and
  from link dep_outputs; must be skipped explicitly in the emitter.
- fetch cache layout: `~/.cache/truss/git/<name>/<tag>` (shallow clone; 40-hex refs use
  init+fetch+checkout FETCH_HEAD because `--branch` rejects commit SHAs) and
  `~/.cache/truss/tarballs/<sha256>.<ext>`; materialized into `build/<mode>/packages/<name>`;
  `truss.lock` (`pkg.<name> = <pin>`) makes re-materialization offline.
- GitHub auto-tarballs (`.../archive/refs/tags/<tag>.tar.gz`) need `--strip-components=1`
  (top dir is `<repo>-<version>/`).
- Vendored git copies: copy the cache tree then delete `.git` (std::filesystem::copy has
  no filter; fine for small deps).
- Test-vector lesson: don't hardcode library constants from memory (XXH64/XXH32 vectors
  were wrong); compute them against the real library first.

## 13. Verified toolchain facts (learned during phase 6 — conan + vcpkg)

- JSON parsing: vendored nlohmann/json 3.12.0 single header (`vendor/nlohmann/json.hpp`,
  sha256 in `vendor/nlohmann/TRUSS-VENDOR.txt`), included from `pkg.cpp`'s GMF; the
  include root is `vendor/` (added to truss-lib's `add_includedirs`).
- conan 2.32 `install --format=json` graph shape: `graph.nodes` is an object keyed by
  node id. The root node has `recipe: "Consumer"` and `package_folder: null`; package
  nodes carry `ref` (NOT `reference`), absolute `package_folder`, and
  `cpp_info.root.{includedirs,libdirs,libs,system_libs,defines}`. Filter packages by
  "package_folder is a string"; resolve relative dirs against it anyway (defensive).
- conan prints JSON on **stdout** and progress on **stderr** — merged capture corrupts
  the JSON. `run_process_stdout` (stdout-only, stderr passthrough) exists for this.
- conan needs `conan profile detect` once on a fresh machine; zlib has no prebuilt for
  this box's profile (gcc 16 / libstdc++11) so `--build=missing` is mandatory.
  `conan install` re-runs on every `./truss` invocation (~100 ms when cached) — fine for
  v0.1, conan caches internally.
- vcpkg: the Arch-packaged `/usr/bin/vcpkg` is a bare binary with NO tree; it errors
  "Could not detect vcpkg-root" unless `VCPKG_ROOT` points at a cloned vcpkg tree
  (`~/vcpkg`). truss prefers `$VCPKG_ROOT/vcpkg` when it exists, else `vcpkg` on PATH.
- `vcpkg install` has **no `--manifest-path` flag** (current versions); manifest mode is
  auto-detected from `vcpkg.json` in the current directory — truss always runs from the
  project root, so no flag is needed.
- vcpkg layout under `--x-install-root`: `<triplet>/{include,lib}`; `lib/` mixes
  `libz.a`, `libz.so*` (symlinks — `is_regular_file` follows them) and a `pkgconfig/`
  subdir; strip `lib` prefix + `.a`/`.so...` suffix and dedupe for `-l` names.
- Usage semantics (CMake INTERFACE-style, `collect_usage` in build.cpp): include dirs
  propagate from ALL deps transitively; defines/link_paths/link_libs propagate only from
  `Interface` deps, so user static libs never leak private defines. Integration-tested
  by `tests/unit/usage_test.cpp` (scratch dir + chdir + real `proj.build()`; doctest
  `INFO` needs the ternary parenthesized: `<<` binds tighter than `?:`).

## 14. Verified toolchain facts (learned during phase 7 — bootstrap; now `setup.sh bootstrap`)

- Plain clang++ can build truss's module without xmake: compile each interface unit once
  with `-c -fmodule-output=<pcm>` (object + BMI in a single pass — no `--precompile`
  two-pass dance). Order: `truss:api` -> `truss:impl` (needs :api) -> primary `truss`
  (needs :api + :impl); implementation units need all three via `-fmodule-file`.
- **Consumers of `import truss;` need TWO flags**: `-fmodule-file=truss=...` AND
  `-fmodule-file=truss:api=...`. clang does not embed imported BMIs into the emitted
  BMI; the primary re-exports :api but references it by name. `:impl` is NOT needed by
  consumers (the primary never imports it). `setup.sh install` stages both pcms.
- truss is built against libc++ (`-stdlib=libc++`); the staged BMI + libtruss.a ABI must
  match the stdlib used to compile the consumer TU.
- samurai C files compile with `-std=gnu17 -w -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L`;
  `samu.c` excluded (shim provides main).

## 15. Verified toolchain facts (learned during phase 8 — self-hosting + installed tool)

- **Brace-list API footgun**: `.defines({"a","b"})` bound to the single-`string_view`
  overload (the `StringRange R&&` template can't deduce from a braced-init-list, and
  libc++ C++26 accepts `string_view{"a","b"}` producing a garbage-sized view →
  `std::bad_alloc`). Fixed with explicit `std::initializer_list<std::string_view>`
  overloads on all 7 paired builder methods (sources/include_dirs/defines/link_libs/
  link_paths/link_frameworks/framework_paths); guarded by a unit test.
- **Transitive static-lib linking**: `dep_outputs` now walks `depends_on` transitively
  (post-order, deduped via a seen-set, then reversed) so archives land
  consumers-before-providers on the link line: every archive is ordered to the
  right of everything that needs it, no matter which `depends_on()` edge reached
  a shared provider first (a diamond exe -> runtime -> ir, exe -> compiler -> ir
  puts libir.a after BOTH consumers). Regression tests:
  `usage_test.cpp "link: transitive static-lib deps are linked in order"` and
  `"link: a shared provider is ordered after every consumer (diamond)"`.
- **samurai in-process re-entry bug**: the shim runs multiple builds per process, but
  upstream `logclose()`/`depsclose()` fclose their FILE* without NULLing it → the next
  `loginit`/`depsinit` double-frees. Vendored one-line patches (`/* truss patch */`),
  recorded in `vendor/samurai/TRUSS-VENDOR.txt`.
- Self-host (`truss.cpp` at repo root): targets `samu` (C, `remove_source` drops
  `samu.c`), `truss-lib` (`sources_in("src")` picks up .cppm/.cpp/.c), `truss`
  (this file; `import truss;` resolved cross-target via `collect_bmis` + depends_on).
  Must set `.cpp_standard(CppStandard::Cpp26)` explicitly — `Default` maps to c++23.
  The self-build uses clang's default stdlib (libstdc++) consistently; `setup.sh`
  builds against libc++ — the two graphs are independent and each is internally
  consistent.
- Installed-tool flow: `setup.sh install <prefix>` stages
  `<prefix>/lib/truss/{libtruss.a,truss.pcm,truss_api.pcm}`; a project compiles its
  script with both `-fmodule-file` flags (§14) — fixture `examples/installed`, command
  `./setup.sh installed`.

## 16. Verified toolchain facts (learned during phase 9 — amalgamation)

- `tools/amalgamate.cpp` generates `build/dist/truss_all.hpp` (~188 KB): samurai .c +
  shim + api/impl/core units concatenated; module directives/partition imports
  stripped; `export namespace` -> `namespace`; local `#include "x.h"` inlined once
  (samurai headers have no guards — the tool dedupes); system includes hoisted.
  `build/dist/` is gitignored (regenerate: `./setup.sh amalgam`).
- samurai compiles as C++ after vendored patches (see TRUSS-VENDOR.txt): pragma
  once, `_Bool`->`bool`, flags enum out of `struct edge` + `unsigned flags`,
  ~50 void* casts, `new`->`new_`, `#ifndef _POSIX_C_SOURCE`. All valid C too, so
  the normal C17 build is unaffected (verified: full suite + self-host green).
- The amalgam wraps the samurai section in diagnostic pragmas
  (-Wwritable-strings, -Wmissing-designated-field-initializers,
  -Wmissing-field-initializers, -Wc99-extensions) so consumers compile it
  warning-free even under `-Wall -Wextra -Wpedantic`.
- Single-TU gotchas handled: shim/samu.cpp duplicated `struct truss_samu_options`
  -> shared header `src/samu/truss_samu.h` (extern "C"-aware; the extern "C"
  declaration also gives shim.c's definitions C linkage when compiled as C++);
  `__lsan_default_options` guarded with `extern "C"`; no static/anon-namespace
  name collisions across samurai or core files (verified).
- Amalgam TU compiles in ~1s (clang++ -O0 default).

## 17. Verified toolchain facts (learned during phase 10 — MSVC + samurai deps=msvc)

- MSVC flag mapping lives in `flags.cpp` (`k*FlagsMsvc` tables), cl.exe/clang-cl
  syntax: no C++11 mode (maps to `/std:c++14`), C++23/26 spell `/std:c++latest`,
  `O3` maps to `/O2`, ThinLTO maps to `/GL`+`/LTCG`, debug info is `/Z7` (embedded
  in .obj — no PDB contention under parallel ninja) with `/DEBUG` at link, C is
  `/std:c17`, always-on `/utf-8 /EHsc /nologo`, no `-fPIC` (Windows has no PIC).
- `link_flags` returns `LinkFlags {driver, linker}`: driver-side flags go before
  the compiler driver's `/link`, linker-side after. For MSVC: `/fsanitize=...` is
  driver-side; `/INCREMENTAL:NO` (ASan), `/LTCG`, `/DEBUG`, `/LIBPATH:` and
  `name.lib` are linker-side. clang-cl treats unknown `/FLAGS` *before* `/link`
  as input files — user `linker_flags()` must land after `/link`.
- samurai feature patch (TRUSS-VENDOR.txt item 3): `deps = msvc`.
  `depsrecord_msvc()` parses `/showIncludes` lines (prefix from the
  `msvc_deps_prefix` manifest var, default `Note: including file:`) out of the
  job's captured stdout, strips them from the echoed output, records into
  .ninja_deps like a gcc depfile. GOTCHA: it runs in `jobdone` *before*
  `edgedone`, so it must `nodestat(e->out[0])` first — recording the pre-build
  (missing) mtime makes the record read as outdated and the edge rebuilds forever.
- samurai supports rule-level `rspfile`/`rspfile_content` (edgevar falls back to
  rule bindings; `$in`/`$out` expand inside them). truss uses them on MSVC
  archive/link rules (`@$out.rsp`) for Windows command-line length.
- `TargetTriple.linker` (new field): link-driver override passed as
  `-fuse-ld=<name>` (driver-side, works for gcc/clang/clang-cl). Needed on Linux:
  clang-cl cannot find `link.exe` without a VS installation and does NOT fall
  back to PATH; `-fuse-ld=lld-link` works. `lld-link` accepts `/OUT:` `/SUBSYSTEM:`
  `/ENTRY:` `/NODEFAULTLIB` `/LIBPATH:` `/DEBUG` `/LTCG` `/INCREMENTAL:NO`.
- clang-cl specifics: `/Fo$out` must be attached (space form makes the path an
  input); `-MF` is rejected; `-MD` is an alias for `/MD` (CRT selection), NOT a
  depfile flag; `/EHsc /utf-8 /nologo` accepted silently; `/showIncludes` prints
  `Note: including file: ./header.h` (cwd-relative, English locale).
- MSVC emitter specifics: objects `.obj`, exe `name.exe`, static `name.lib`,
  shared `name.dll` + implicit-output import lib `name.lib`; consumers link the
  import lib, never the dll; no rpath on Windows (skip `$ORIGIN` rpath);
  archive via `lib.exe`/`llvm-lib` (`/NOLOGO /OUT:`).
- Modules + MSVC: hard Config error (BMI wiring relies on clang's
  `-fmodule-output`/`-fmodule-file`).
- `glob_multi(patterns)`: one tree walk per distinct scan root (sources_in adds
  6 patterns → 1 walk, not 6). GOTCHA: a fully literal pattern's literal prefix
  is the file itself — clamp the scan root to its parent directory.
- `./setup.sh unit` includes `tests/unit/msvc_test.cpp`: end-to-end clang-cl compile +
  /showIncludes dep tracking + llvm-lib archive + lld-link freestanding link
  (`/NODEFAULTLIB /SUBSYSTEM:CONSOLE /ENTRY:main`), incremental + no-op checks;
  self-skips when clang-cl/lld-link/llvm-lib are absent. 49 cases / 202 assertions.

## 18. Verified toolchain facts (learned during phase 11 — consolidation + doctest)

- doctest works inside C++20 modules: test units keep `module truss;` and include
  `<doctest/doctest.h>` in the global module fragment; the main TU (`tests/unit/main.cpp`)
  is a plain (non-module) TU defining `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`. TEST_CASE
  registrars in module implementation units register into the main TU's registry at
  static-init time — same mechanics Catch2 used. Macros map 1:1 (TEST_CASE, CHECK,
  CHECK_FALSE, REQUIRE, REQUIRE_FALSE, INFO, WARN).
- **Inline member functions defined in a module *interface* are emitted once into the
  interface unit's object** — implementation units reference them, they are NOT re-emitted
  locally. So any link of the implementation-unit objects must also link `api.o`/
  `impl.o`/`truss.o` (setup.sh unit links them from the bootstrap obj dir).
- `-stdlib=libc++` must appear on the **link** line too, not just compiles — clang
  defaults to libstdc++ at link time otherwise (undefined `std::__1::` refs).
- ASan/UBSan coexist with the module path: BMIs are stdlib/codegen-agnostic, so the
  bootstrap BMIs are shared and only the implementation/test TUs are recompiled with
  `-fsanitize=address,undefined -fno-sanitize-recover=all`; vendored samurai stays
  unsanitized (shim's `__lsan_default_options` suppresses its leak noise).
- Hardening tiers (`harden(Hardening)`): `Default` resolves to **Light**, so every
  target gets `-fstack-protector-strong` + `-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
  -Wl,--as-needed` unless `harden(None)`. Golden flag tests pass `Hardening::None` to
  keep the unhardened baselines. `-fcf-protection`/`-fstack-clash-protection` (GCC/
  Clang) and `/guard:cf`//`/CETCOMPAT` (MSVC) are x86-64-only — gated on
  `triple.arch == X64`; `/sdl` and `/NXCOMPAT /DYNAMICBASE` are arch-independent.
  libc++/libstdc++ hardening modes are emitted as plain defines
  (`_LIBCPP_HARDENING_MODE=…`, `_GLIBCXX_ASSERTIONS`) — inert against the other
  stdlib, so no stdlib detection is needed (compile-side only, never `compile_c`).
- Config/toolchain: `proj.configure(Config)` overlays compiler (family re-detected
  from basename via `detail::family_from_compiler_name`), linker (`-fuse-ld=`), ar
  (follows family unless set), stdlib onto `host_triple()`; per-target
  `cross_target()` still wins completely. `TargetTriple` gained a `StdLib stdlib`
  field; `-stdlib=` emitted compile + link (never MSVC — validated away:
  g++ + libc++ and MSVC + stdlib() are Config errors). Implementation units using
  `std::unexpected` need `#include <expected>` in their GMF (reachable ≠ visible).
- **`std::expected` cannot hold `T&`** (reference types are ill-formed — libc++
  asserts via a reference-typed union member). Package returns use
  `Result<TargetRef>`: a tiny non-owning handle (`operator*`, `operator->`,
  implicit `operator Target&`) — never null, `depends_on(*r)` reads naturally.
- Module-purview quirk: `std::initializer_list` parameters only get the builtin
  `size()`/`begin()`/`end()` — `empty()` is "no member named" inside a module
  unit even with `<initializer_list>` included. Compare `size() == 0`.
- doctest: `CHECK(a || b)` → "Expression Too Complex" — parenthesize:
  `CHECK((a || b))`.
- Doxygen 1.18 parses C++20 module files (`module;`, `export module truss:api;`,
  `import :api;`) natively — no PREDEFINED hacks; INPUT = src/api.cppm only.
  Gotchas: `/*` sequences inside `/** */` blocks open *nested* comments (glob
  examples like `src/*.c` silently swallow the rest of the file — use `///` line
  comments for those); angle brackets in doc comments need `\<mode\>` escapes;
  markdown anchors (`#getting-truss`) don't resolve as `\ref` — keep README.MD
  out of INPUT.

## 19. Verified toolchain facts (learned during phase 12 — performance)

### Step 0: benchmark harness (`./setup.sh bench`)
- Baseline (2026-10-09, 60-TU fixture, 32 jobs, medians, JSON at
  `build/bench/results.json`): truss clean 539 ms / incremental 297 ms / no-op 3 ms;
  `make -j` floor 543 ms (truss overhead ≈ noise); `ninja -n` graph recompute 1–2 ms;
  xmake clean 1463 ms / incremental 131 ms; examples/modules touch `math.cppm`
  69 ms (Auto mode rebuilds the whole module target — the step-7 gap).
- truss incremental (297 ms) vs xmake incremental (131 ms): truss re-runs its
  configure (source scan + manifest regen) on every invocation; samu itself is the
  fast part (no-op 3 ms). Later phase-12 steps attack per-invocation overhead.
- bench tool gotchas: `xmake -m` is only valid on `xmake f` (mode persists, plain
  `xmake` builds); GNU make accepts one-line `target: prereq; recipe` (no tabs
  needed) — handy for generated floor Makefiles; inside bash heredocs/echoes the
  make automatics `$@`/`$<` must be escaped or the shell expands them.
- Box tooling: xmake, make, ninja installed; ccache/sccache NOT installed (step 1
  e2e must use a noop launcher script, not real ccache).

### Step 1: compiler-cache launcher
- `Config::launcher(name)` (override; "" opts out; missing program = Config
  error) + auto-detect ccache→sccache when unset (`resolve_launcher` in
  target.cpp). Injected as a literal prefix in the compile rule *command
  string* — samurai's support for ninja's RULE_LAUNCH_COMPILE is unverified.
- Launcher prefixes `compile_cxx`/`compile_c` only: `compile_bmi` stays raw
  (ccache's C++20 module support is unreliable) and MSVC rules never launch.
- e2e proof without ccache installed: a noop `#!/bin/sh; exec "$@"` script
  used as launcher — manifest shows `command = ./lk/launcher.sh $cxx` and the
  script's hit-marker file appears after the build.

### Step 2: conan/vcpkg configure-skip
- conan: sha256 of the conanfile (or the conanfile.txt/.py inside a dir arg)
  + usage-requirements spec cached in `build/<mode>/packages/conan/truss-spec.json`;
  fingerprint match + all cached include/link dirs still exist => skip
  `conan install`. vcpkg: sha256(vcpkg.json)+"-"+triplet in
  `build/<mode>/packages/vcpkg-truss-spec.json`; match + install tree exists
  => skip `vcpkg install` (tree rescan is cheap and always runs).
- The spec is only written after a successful tool run, so a failed run never
  poisons the cache (verified: invalid ref re-runs conan every time).
- IMPORTANT toolchain trap (ASan false positive, cost hours): the unit build
  recompiled impl units with -fsanitize but reused the non-ASan bootstrap BMIs.
  Inline libc++ bodies (vector container annotations) baked into a non-ASan
  BMI are no-ops; mixing them with ASan-instantiated ones poisons fresh
  allocations that never get unpoisoned -> bogus container-overflow on plain
  `std::vector<std::string> argv {...}` code. Fix: cmd_unit regenerates the
  BMIs WITH sanitizers (build/unit-bmi) and compiles everything against those.
  Any future sanitizer flow must regenerate BMIs the same way.

### Step 3: hardlink materialization
- `copy_tree` (now `detail::copy_tree`, declared in impl.cppm) walks the tree
  and `create_hard_link`s each file (copy fallback on cross-fs/EMLINK),
  skipping `.git` via `disable_recursion_pending()` instead of copying then
  deleting it. Verified: fetched git packages show link count 2 in
  `build/<mode>/packages/`; tarball packages stay count 1 (tar extracts fresh).

### Step 4: parallel fetch
- `proj.fetch_all({{name, FetchOptions{...}}, ...})`: validates all entries
  first (duplicate names = Config error), then runs cache+materialize on up
  to 8 `std::jthread`s (round-robin job chunks), writes the lockfile once,
  returns Packages in request order, first error aborts.
- Thread safety: each job owns its dest dir; caches are per-package; log
  lines are single `printf` calls (libc-atomic, verified interleaved-but-
  intact under concurrent fetch).
- Verified: cold-cache fetch example downloads both packages concurrently
  (0.9 s total), lockfile gets both pins.

### Step 5: precompiled headers
- `target.pch(path)`: one `compile_pch` edge (`-x c++-header -MMD`), output
  under `build/<mode>/pch/<target>/`; every C++ object edge gets the use-flag
  + the PCH as an implicit input. clang: `-include-pch <out.pch>`; GCC:
  `<base>.gch` single file + `-I <pchdir> -include <base>` (verified on
  g++ 16.2). MSVC family = Config error. compile_pch rule has NO launcher
  (ccache+PCH unreliable).
- Pre-existing bug fixed on the way: `Config().toolchain(GCC)` emitted
  `-stdlib=libstdc++`, which this GCC 16.2.1 rejects (GCC's default is
  libstdc++; `-stdlib=` is a clang driver flag). flags.cpp now emits
  `-stdlib=` only for the Clang family (GCC+Libstdcxx = no flag, GCC+Libcxx
  already rejected by apply_config).
- Test gotcha: setting a source's mtime to `now()+1s` makes it perpetually
  newer than the outputs built right after, so every later build rebuilds.
  Use `now()` (the build itself takes ms, so outputs land strictly newer).

### Step 6: unity builds
- `target.unity()`: batch TUs of 16 (`build/<mode>/unity/<target>/unity_N.cpp`,
  each `#include`s absolute member paths), compiled instead of the originals;
  `.cppm` units never batched (cxx_sources already excludes them). Batch BMI
  wiring = union of members' imports (merge_imports dedupes by BMI path).
  `detail::write_file_if_changed` keeps batch mtimes stable so unchanged
  builds stay no-ops.
- Measured on the 60-TU bench fixture: clean 539 ms -> ~285 ms (~1.9x).
- Object paths nest the absolute unity path under objdir
  ("objdir//home/...") — valid POSIX (double slash collapses); tests must
  locate unity objects by filename search, not by path guess.

### Step 7: precise module wiring (ModulesMode::Manual)
- clang-scan-deps on LLVM 23: the trailing `-- <cmd>` driver form and
  `-deprecated-driver-command` produce NO "requires" entries; only
  `-compilation-database=<cdb>` does. In CDB mode "primary-output" is empty
  and rule order is nondeterministic -> scan ONE TU per single-entry CDB
  (rules[0] is that TU). ~10 ms per invocation; parallelized (8 jthreads).
- scan-deps requires = direct imports only (partition names fully qualified,
  impl units report their implicit primary). Consumers of a primary still
  need the partition-expansion heuristic for the -fmodule-file closure.
- Macro-expanded `import` is rejected by clang itself (not a differentiator);
  line-spliced `import \` continuations ARE found by scan-deps and missed by
  the line-anchored regex — that's the real Auto gap.
- jthread pool gotcha: locals captured by reference that are declared AFTER
  the pool vector are destroyed BEFORE the pool's join (reverse destruction
  order) -> stack-use-after-scope. Declare captured locals above the pool.
- write_file: create_directories("") sets ec -> bare relative filenames
  failed; now guarded by has_parent_path().

### Phase 12 wrap-up (bench re-run, all steps in)
- truss clean 500 ms (baseline 539), no-op 4 ms, ninja dry-run floor 1 ms,
  make floor 525 ms, xmake clean 1447 ms.
- Opt-in wins measured on the same fixture: unity ~285 ms clean (~1.9x),
  PCH/launcher/scan-deps reduce per-TU and configure cost further but the
  fixture does not enable them by default.
- No regressions: 80 unit cases + all examples green.

## 20. Final QC pass (post phase 12)
Reviewed every library source for perf sinks, duplication, anti-patterns,
dead code. Fixed:
- Dead code: FlagSet::link (never written), log_is_verbose + g_verbose +
  log_set_verbose (write-only channel), unused includes (cstdio/cctype/
  functional). glob() kept (test-only convenience wrapper).
- program_exists forked `sh -c command -v` on EVERY call (~4-6 per build:
  llvm-ar, ccache, sccache, clang-scan-deps, conan, vcpkg). Memoized
  (static map + mutex). Self-host no-op: 57 ms -> ~20 ms.
- Manifest was rewritten every run (mtime churn): now write_file_if_changed;
  NinjaWriter::str() returns const& (was a copy).
- `./truss clean` resolved sources, scanned files and emitted a manifest
  just to delete it: clean-only invocations now short-circuit (and work on
  broken projects).
- truss.lock was rewritten on every no-op fetch/fetch_all: now only when a
  pin actually changes.
- Exception-safety: throwing std::filesystem overloads (remove_all/remove/
  exists without ec) violated the no-exceptions policy in pkg.cpp,
  modules.cpp, build.cpp -> all use error_code overloads now.
- Emitter: unit_for() deref without null check (UB if invariant breaks) ->
  Internal error guard; unity batch members copied into a vector -> span;
  dep_outputs copied into edge inputs -> moved.
- API: flags()/linker_flags() REPLACED on repeat calls despite "appended
  verbatim" docs -> accumulate (space-joined); the four add_* factories
  were copy-paste -> one private add_target<T>; stale TargetTriple::stdlib
  comment fixed.
- conan() had the same JSON string-array lambda twice -> json_str_array.
Noted, not changed (deliberate): regex module scanner (Manual mode is the
precise escape hatch); per-edge flag-string copies (O(TU x flags),
negligible); O(n^2) duplicate-name checks (n small); test executables run
serially (output interleaving); unity obj paths embed the absolute unity
path under objdir (valid, tested by filename search).
Tests: 82 cases (new: clean-only short-circuit, flags accumulation).

## 21. Doxygen coverage pass (docs audit follow-up)

Public API (src/api.cppm): 100% member coverage (WARN_IF_UNDOCUMENTED=YES,
zero warnings — now a permanent guard in the Doxyfile), plus @param/@return
on all parameterized entry points and per-enumerator `///<` docs on every
enum. Internals: src/impl.cppm is fully documented (every truss::detail
declaration, brief-level) and grouped via `\ingroup grp_internals` into a
"Internals" group (nav: Topics -> Internals); docs/internals.md adds an
"Internals" page (nav: Pages -> Internals) describing module structure,
build flow, subsystems (incl. anonymous-namespace machinery in src/core/*.cpp,
which doxygen cannot see) and invariants. Doxyfile INPUT = api.cppm +
impl.cppm + docs/internals.md.

Verified doxygen 1.18 facts:
- `/*` inside a `/** */` block comment NESTS (doxygen counts nesting): a doc
  block containing "src/**.cpp" swallows the rest of the file. Use `///` line
  comments for text containing `/*`.
- A `{` at end of a doc-comment line opens a verbatim block (unmatched </tt>
  warnings). Inline `{a,b}` mid-line is safe; escape or rephrase trailing ones.
- `\memberof <group>` on namespace members fails ("class 'grp_internals' ...
  is not documented") — doxygen wants a class. `\ingroup <group>` on each
  declaration works and keeps members on the namespace page too.
- A `\defgroup ... \{` around a namespace only adds the NAMESPACE to the
  group, not its members; the group page stays thin without per-member
  \ingroup.
- Doxygen 1.18 nav calls groups "Topics" (topics.html), not "Modules";
  no modules.html is generated.
- WARN_NO_PARAMDOC=NO + WARN_IF_UNDOCUMENTED=YES: params are documented by
  choice, members are enforced.

## 22. Phase 13 verified facts (scripting helpers)

- libc++ 23.1.1 (Arch, matches clang 23.1.1) has NO <generator>: P2502R2 is
  still unimplemented in libc++ (llvm issue #105226, Cxx23 status blank).
  GCC 16 libstdc++ and MSVC STL have it. truss's bootstrap uses libc++, and
  BMIs are built on the user's machine -> std::generator is NOT usable in the
  public API. Resolution: truss::generator<T> in :api, shape adapted from
  cppcoro::generator (MIT, Lewis Baker): coroutine-based, range-for
  compatible, T-by-value in the promise. Swap for std::generator when libc++
  ships it (alias, call sites unchanged). Maintained cppcoro fork for
  reference: https://github.com/andreasbuhr/cppcoro (upstream lewissbaker
  is unmaintained); we borrow only the generator design, not the library.
- os::find_program replaces the `sh -c command -v` probe entirely: manual
  $PATH split + access(X_OK), memoized incl. misses. No shell fork during
  configure.
- Native sha256 (FIPS 180-4) in fs.cpp replaces the `sha256sum` shell-out in
  tarball_fetch; vectors verified against coreutils (abc, empty, 100x'x').

- Hand-typed constant tables are a trap: the first sha256 pass had a K[23]
  typo + a dropped entry (aggregate init silently zero-fills, no warning).
  Generated K from the spec (cube roots of primes, Decimal) and pinned three
  coreutils-verified vectors in fs_test (abc / empty / 100x'x').

- Phase 13 done: os::run/capture gained initializer_list sugar overloads
  (brace lists in user code; span stays the canonical form). TaskFn takes
  const TaskContext& (not Project&) — examples/README snippets must match.
  Bench after phase 13: no-op 2 ms, clean 489 ms, incremental 279 ms.

## 23. Full-codebase audit QC pass (post phase 13)

Line-by-line audit of every source unit; fixes only where there was a real
gain (D-list deliberately left alone: 2-use patterns, std::regex scanner,
merge_imports string round-trip, read_lines double-open).

- std::filesystem::relative(p, base) weakly-canonicalizes BOTH sides — one
  stat/readlink family of syscalls per call. Drop-in replacement when paths
  are built lexically from the same base (directory iterators do):
  p.lexically_relative(base), pure string arithmetic. Verified semantics:
  "src/a.cpp" rel "." -> "src/a.cpp"; "./x" rel "." -> "x"; iterator rooted
  at "." yields "./x" entries, so lexically_relative(".") strips the prefix
  exactly like relative() did. Applied in glob_multi (per-entry hot loop) and
  copy_tree. Bench: no-op 2 ms -> 1 ms.
- sha256 is now an incremental context (Sha256Ctx: update/final_hex) and
  sha256_file streams 64 KiB chunks — bounded memory for tarballs. The
  in-memory sha256_hex wrapper became unused (-Wunused-function caught it)
  and was deleted; if a string digest is ever needed again, wrap a ctx.
  New fs_test pins 65536 / 65537 / 200000-byte digests (chunk-boundary and
  partial-final-block paths) from coreutils sha256sum + hashlib.
- pkg.cpp file_sha256 (sha256sum shell-out) deleted; conan + vcpkg
  configure-skip fingerprints use fs::sha256_file natively.
- Dead code removed: ResolvedTarget::outdir (never read); scan-deps scratch
  now a dogfooded os::temp_scope (was build_dir/scan, never cleaned);
  resolve_hardening moved to flags.cpp anonymous namespace.
- Target builder: 7 fields x {single, ilist, range} overloads share one
  private static append_strings(vector<string>&, R&&); overload shape kept
  (deliberate overload resolution), bodies are one-liners.
- Bench after QC: no-op 1 ms, clean 491 ms, incremental 275 ms.
  Suite: 96 cases / 556 assertions, examples + installed + amalgam green,
  docs zero warnings.

## 24. Static linking, zig, cross targets, `truss init` (post phase 13)

- **`static` is a keyword**: the per-dependency opt-in is `static_link()`.
  Getters are `static_link_enabled()`/`static_deps_enabled()` because the
  setters default their bool: `t.static_link()` on a non-const Target picks
  the setter (compile error "Target not convertible to bool"). Same trap as
  `unity()`.
- `static_link()` on a SharedLib flips `kind_` to StaticLibrary (reversible via
  `demoted_shared_`); every `kind()` consumer then just works.
- `static_deps()`: SharedLib deps get a *static twin* archive (`lib<name>.a`
  next to the `.so`, same PIC objects, extra `archive` edge only when some
  static consumer reaches it). Libs route through `-Wl,-Bstatic ... -Bdynamic`
  emitted BEFORE dynamic libs (GNU ld single pass); `detail::is_runtime_lib`
  keeps c/m/pthread/dl/rt/... and the C++ runtime dynamic.
- Verified on this box (clang 23.1.4, gcc 16.2.1, glibc): `-static-libstdc++
  -static-libgcc` works for g++ and clang+libstdc++ (ldd: libc/libm only).
  clang + libc++ `-static-libstdc++` swaps libc++ but NOT libc++abi (undefined
  `__cxxabiv1::__si_class_type_info`); the working recipe is driver
  `-nostdlib++ -static-libgcc` + after-the-objects `-Wl,-Bstatic -lc++
  -lc++abi -Wl,-Bdynamic` (link_exe puts `$flags $link` after `$in`).
- `link_shared` rule takes `$soname_flag` (ELF `-Wl,-soname,`, macOS
  `-Wl,-install_name,@rpath/`, Windows none). GNU-ld hardening flags
  (`-z relro` ...) are gated by `detail::is_elf(os)` (unknown OS = ELF).
- zig: `zig c++` is clang underneath (family Clang, `detail::is_zig` for the
  differences): `-target <t>` (clang: `--target=<t>`), no `-stdlib=`, no
  ccache launcher (any zig target disables it for the manifest — rules are
  shared), `triple_dir` family part "zig". zig 0.17.0 is installed at
  /usr/bin/zig; the paths below were verified for real on 2026-11-02 (see §28).
- C driver for `.c` sources: `detail::c_driver` (clang++-19 -> clang-19,
  `zig c++` -> `zig cc`, *-g++ -> *-gcc). The old exact-match on "clang++"
  compiled C as C++ for any other spelling.
- `#embed` (clang 23, C++26): works in constexpr char arrays, warns
  `-Wc23-extensions` only under -Wpedantic (silenced locally). Paths resolve
  relative to the including file (cli/). Templates must stay ASCII (bytes
  >127 narrow into `char`).
- `TRUSS_INSTALLED_BIN` is defined on the *CLI TU* only (truss.cpp), never on
  the library: amalgam/vendored users don't carry the templates.
- Review fixes: pipes are O_CLOEXEC (threads spawn concurrently in fetch_all /
  scan-deps), write_file is temp+rename atomic, depends_on(Result) keeps the
  error for build() (it used to drop it), fetch refuses '-'-prefixed
  url/git/tag (git/curl option injection), `-j` uses from_chars, $CXX drives
  the compiler family.
- Tests: tests/unit/static_test.cpp (113 cases total). `./setup.sh installed`
  now also runs installed-CLI `init` -> bootstrap.sh -> `./truss test`.

## 25. CLI quick wins + build-script self-rebuild

- `-h`/`--help`/`help` print `kHelp` (build.cpp) and exit 0; `help` is a
  reserved task name. `--` forwards the rest of argv to `run`; `run` honours
  `--target` and builds only the executable it starts.
- Self-rebuild (`refresh_build_script`, Project ctor, nob-style): compares
  `<exe dir>/truss.cpp` with the running executable (`/proc/self/exe`, else
  argv[0]). Newer script + `bootstrap.sh` present ⇒ run it in that dir, set
  `TRUSS_REBUILT=1`, `execv` with the same argv. No bootstrap.sh ⇒ Config
  error through `cli_error_` (reported by build()). Still newer after a
  re-exec (clock skew) ⇒ warning, no loop. `TRUSS_NO_REBUILD=1` disables it.
  Executables without a truss.cpp next to them (unit tests, installed CLI)
  are untouched. Not compared: headers the script includes, libtruss/BMIs.
- Motivation (user feedback): a stale `./truss` built the old graph silently;
  with a system copy of the new dependency's headers, only the link failed,
  pointing away from the cause. bootstrap.sh now prints
  `bootstrap: rebuilt ./truss from truss.cpp`.
- `./setup.sh installed` touches the init project's truss.cpp and checks the
  rebuild happened.

## 26. System includes + PUBLIC usage requirements (feedback #3)

- New Target builders: `system_include_dirs()`, `public_defines()`,
  `public_flags()`; all three are collected by `UsageWalk` from the consumer
  itself and every transitive dep (any kind, not only Interface).
- `detail::build_flags` gained a trailing `system_includes` span (default
  empty): `-isystem "dir"` after the `-I`s; MSVC `/external:W0` once +
  `/external:I"dir"`.
- Compile flag order: deps' + own `public_flags()` (dedup'd) then the target's
  private `flags()`, so private flags win.
- `conan()` / `vcpkg()` Interface targets now use `system_include_dirs` for
  package headers (`add_subdirectory` keeps `-I`: often first-party code).
- Tests: flags_test (-isystem order), msvc_test (/external:I), usage_test
  (transitive app -> mid -> core with `-Wall -Werror` against a warning in a
  system header; private `flags()` must not leak). 116 cases.

## 27. User tasks behind `./truss task`

- `proj.task(name, description, fn)` (description optional); `UserTask`
  struct replaces the pair. `./truss task NAME` runs one (repeatable:
  `task a task b`), `./truss task [list]` prints name + description and
  returns before validation/build. User task names no longer clash with
  built-ins (the shadow check is gone); `list`, empty and `-`-prefixed names
  and duplicates are Config errors raised at the top of build().
- Bare `./truss NAME` matching a user task ⇒ error "run it with ./truss task NAME".
- Tests: usage_test "tasks:" cases (118 total); setup.sh examples run
  `task stats` and grep `task list`.


## 28. Zig toolchain verified for real (zig 0.17.0)

- zig 0.17.0 at /usr/bin/zig (bundled clang 22.1.8). Verified empirically 2026-11-02.
- Host (`Config().toolchain(Toolchain::Zig)`): `zig c++`/`zig cc`/`zig ar`
  compile, link, run, test all green. libc++ static by default (ldd: libc only).
- Cross targets (checked with `file`/`ldd`/`objdump`):
  - x86_64-linux-musl -> `x64-linux-musl-zig`; static by default (even without
    static_stdlib); runs on host.
  - aarch64-linux-musl -> `arm64-linux-musl-zig`; ELF aarch64, static.
  - x86_64-windows-gnu -> `x64-windows-zig`; app.exe PE32+ console, libcore.dll PE32+ DLL.
  - x86_64-linux-gnu.2.17 -> `x64-linux-gnu.2.17-zig`; dynamic glibc, max symbol
    version GLIBC_2.17 (`objdump -T | grep GLIBC`); runs on host. Portability lever.
- C sources via `zig cc`; `zig ar` archives work.
- Static: `static_deps().static_stdlib(StaticStdlib::Musl)` on a musl target ->
  "not a dynamic executable". `StaticStdlib::Llvm` on zig is a no-op (zig always
  links libc++/libc++abi statically), emits no flag; gnu.2.17 + Llvm = dynamic
  glibc<=2.17 binary, no dynamic libstdc++/libc++.
- Shared libs, ELF: SONAME set (libcore.so), consumer gets RUNPATH
  `$ORIGIN/../../lib/<triple>` + NEEDED libcore.so; runs.
- Shared libs, windows-gnu (MinGW): .dll in bin/<triple>/, import lib in
  lib/<triple>/lib<name>.dll.a (via `-Wl,--out-implib`); exe links the import lib.
- BROKEN, now Config errors:
  - C++20 modules: `zig c++` does not treat .cppm/.ixx/.ccm/.cxxm/.c++m as C++, so
    module compiles get host system headers + clang default triple x86_64-pc-linux-gnu.
    `-x c++` restores headers/triple but clang then emits no BMI (-fmodule-output
    "unused"); `-Xclang -fmodule-output=`/`-emit-module-interface` fail (zig cache
    discards the BMI or misroutes it to the linker). Zig + `.cppm` -> Config error
    pointing to Toolchain::LLVM.
  - PCH: `zig c++ -x c++-header` output goes to the linker ("unknown file type").
    Zig + `pch()` -> Config error.
  - `ModulesMode::Manual`: clang-scan-deps reads "zig" as compiler, "c++" as an
    input. Upfront Config error (was a garbled runtime scan failure).
- Phase 5 (building truss itself with zig): direct build BLOCKED (truss is .cppm
  modules, `import truss;`). Amalgam path (examples/amalgam, non-module) is close:
  `zig c++ -std=c++26 -target x86_64-linux-musl -Ibuild/dist -Ivendor
  examples/amalgam/truss.cpp` compiles + links fully static (truss's C++ DOES
  build against zig's libc++ 22 — first catch resolved). Shipping truss itself as
  a portable binary is a SECONDARY follow-up (musl/zig/cross support in the tool is
  done + verified). Recipe: `static_deps()` for truss's own deps, libc++ stays
  DYNAMIC (not necessarily fully static). Two blockers:
  (A) `truss-samu: reallocarray: No such file or directory` only when RUNNING a
  truss built with ZIG for a musl target: quirk of ZIG'S BUNDLED MUSL, not musl in
  general. Measured: zig-musl `realloc(NULL,0)` and `realloc(p,0)` both NULL; real
  musl (`x86_64-linux-musl-gcc -static`) both non-null; glibc `realloc(NULL,0)`
  non-null, `realloc(p,0)` NULL. gdb: samurai `depsinit` (deps.c) calls
  `xreallocarray(NULL,0,32)` on an empty `.ninja_deps` (fresh build dir); NULL ->
  `fatal()`. The ENOENT text is a stale errno. No repro on Alpine/musl-gcc. Toolchain
  reality here: `musl-clang` and `x86_64-linux-musl-gcc` are C-only (no C++ headers),
  no musl g++; `zig c++ -target x86_64-linux-musl` is the only musl C++ toolchain.
  PENDING fix (not applied): one-line sanctioned truss patch in
  vendor/samurai/util.c `xreallocarray`: `if (!p && n && m) fatal(...)` (NULL ok for
  a zero-size request); valid C/C++, inert on glibc/real musl; precedent in
  vendor/samurai/TRUSS-VENDOR.txt. (B) gnu.2.17 link: `posix_spawn_file_actions_
  addchdir_np` (glibc 2.29) only matters for a glibc-floor artifact <2.29 (irrelevant
  to musl); raise the floor or add a child-side chdir fallback.

## 29. `import std;` support

- clang + libc++ only, C++23 or later (default std is C++23). zig, g++, MSVC and
  clang+libstdc++ get a Config error pointing at `Toolchain::LLVM`; `cpp_standard()`
  below C++23 is a Config error too. Detection scans each unit's imports for `std` /
  `std.compat` (also for plain `.cpp`, with no `.cppm` in the target).
- The std BMI and its object come from one call driven by libc++'s module manifest
  (`<cxx> -std=c++NN -stdlib=libc++ -print-library-module-manifest-path`; paths
  are relative to the manifest dir, JSON parsed with nlohmann, cached per compiler).
  Compile flags are minimal (`-std`, `-stdlib=libc++`, `--target`, `-fPIC`,
  `-Wno-reserved-module-identifier`, manifest `-isystem`s): only the C++ standard
  affects BMI compatibility, so -O/-g/-D/warnings/sanitizers need not match.
- Deduped per (triple dir, -std): ONE `compile_bmi` edge per key for std (and one for
  std.compat, emitted lazily), outputs under `build/<mode>/std/<triple>-c++NN/`
  (`std.pcm std.o std.compat.pcm std.compat.o`). Consumers get
  `-fmodule-file=std=<pcm>` via the normal bmis map.
- `std.compat` has `export import std;`: its edge takes the std pcm as input and
  consumers of `std.compat` get both module mappings (modules.cpp adds `std`).
- Linking: std.o (and std.compat.o) is linked EXACTLY ONCE per exe/shared lib, when
  the target or any transitive dep uses it, and NEVER put in static archives (two
  copies => "multiple definition of initializer for module std"). Dependencies reached
  through a shared lib are skipped (the .so carries its own copy).
- First build pays a one-time compile of std.cppm (~38 MB BMI, several seconds);
  afterwards it is cached by samu like any edge.

## 30. Verified toolchain facts (1.0 push: for_host, import_library, phase-3 CLI/runner, custom commands)

- `Target::for_host()` pins a target to `apply_config(config-with-target-cleared, host_triple())`
  — compiler/stdlib/ar overlays apply, the cross `target` string never does. The host/target
  boundary check compares resolved `triple.target` strings across every `depends_on` edge
  (host "" vs cross "aarch64-linux-musl", or two different cross triples) and fails with a
  mate-style Config error naming both sides. Interface/ImportedLibrary deps are exempt (no artifact).
- `import_library(name)` (`TargetKind::ImportedLibrary`): no sources resolved, no edges, no `all`
  entry; `import_archive()` files are validated at build() (non-empty, not '-'-prefixed,
  path_is_safe, exists under cwd) and land verbatim on dependents' link lines via the dep walk
  (GNU ld order); `.so`/`.so.N`/`.dylib` archives also add `-Wl,-rpath,<dir>`. `--target` on an
  artifact-less target (Interface/ImportedLibrary) is a Config error. UsageWalk treats
  ImportedLibrary like Interface for defines/link_paths/link_libs propagation.
- Phase 3: `--cross TRIPLE` is parsed in the Project ctor and overlaid onto the Config before
  apply_config (kHelp documents it); `Config::runner(cmd)` (validated: non-empty, not
  '-'-prefixed) is split on spaces and prepended when executing cross test/run exes
  (`argv_to_run` helper); without a runner, cross test exes are built then skipped with a
  warning and `run` on a cross exe is a Config error. `proj.targets({...})` matrices were
  DEFERRED: the emitter resolves one project triple, so per-triple subgraphs would re-resolve
  the whole graph — documented as remaining in docs/cross-compilation.md.
- `add_custom_command(name)` (`TargetKind::CustomCommand`): `.command({...})` tokens support
  `$tool` (the `.uses(exe)` executable path), `$out` (per-output expansion), `$in` (depends_on
  file inputs); one ninja edge carries all outputs as explicit outputs, the tool exe and file
  inputs as explicit inputs. Consumers get outputs as implicit compile inputs + `-I` of each
  output's dir via UsageWalk. Validation: no sources/is_test, non-empty command, `$tool` needs
  uses(), uses() must be an Executable, outputs relative + path-safe + deduped. In a cross
  project the used tool must be for_host() (else Config error); the host/target mismatch check
  covers custom-command edges.
- Public API ergonomics: `depends_on`/`merge` accept `Result<TargetRef>` by value — call sites
  are deref-free (`depends_on(proj.conan({...}))`); failed Results defer into `deferred_error_`
  and build() reports them ("depends_on()/merge() was given a failed result").
- Fetch hardening: after extraction, `verify_no_escaping_links(dest)` walks the tree
  (without following links) and rejects any symlink whose normalized target lands outside dest;
  tar also gets `--no-same-owner`; a refused tarball's partial dest is removed.
- proc.cpp: failed `waitpid` now yields exit_code -1 (was silently 0); failed
  `posix_spawn_file_actions_init` with a non-empty cwd is an Io error instead of silently
  running in the wrong directory.
- Tests: 167 doctest cases green (`./setup.sh unit`); full `./setup.sh test` green incl.
  examples/imported and examples/cross-host (wired into setup.sh cmd_examples).
- Doxygen gotcha (found via docs-warning hunt): a backtick code span starting with
  `./` (e.g. `` `./truss` ``) emits unmatched <tt> warnings — write ./truss without
  backticks in doc comments. `./setup.sh docs` is now warning-free again (0 warnings).

## 31. Review & hardening round (Nov 2026)

All items below are covered by the unit suite (`./setup.sh unit`, 178 cases).

- **Deducing-`this` builders (API)**: every `Target`/`CustomCommand` builder method is
  now `template <class Self> Self& name(this Self& self, ...)` and returns the
  *derived* type, so derived-only methods chain after base ones in any order
  (`proj.add_custom_command("g").for_host().command({...})` compiles now; it used
  to fail because `for_host()` returned `Target&`). Called through a `Target&`
  the return is still `Target&` — source-compatible for existing scripts.
  `sources_in`/`remove_source`/`depends_on(const Target&)` moved inline into
  api.cppm (templates must live in the interface unit). AGENT.MD §2's "every
  builder method returns Self&" is now literally true.
- **`Warnings::Default` was silently no flags**: `kWarningFlags` was indexed by the
  raw enum (Default = 0 → ""), while every other `Default` option resolves first.
  Now `resolve_warnings(Default → All)`: the documented `-Wall -Wextra` is applied.
  Every default build gained warnings — expect new diagnostics in older projects.
- **PCH on GCC actually engages now**: the `-I<pch_dir>` went in *after* the
  target's own include dirs, so GCC found the plain header (no adjacent .gch)
  and silently skipped the PCH. The PCH dir is now the first include; `pch()`
  paths are validated path-safe at configure (new Config error case).
- **Archive link order**: `dep_outputs` walks post-order then reverses —
  consumers-before-providers on the link line, fixing diamonds
  (exe -> A -> X, exe -> B -> X used to emit `A X B`, leaving B's refs to X
  unresolved) and the forge runtime/compiler/ir case.
- **Module scanner skips non-code**: comments, plain strings, char literals and
  raw strings (any encoding prefix, delimiters respected, `abcR"x"` still a
  plain string) are blanked before the module/import regexes; newlines are
  preserved so the line-anchored regexes keep working. Embedded-TS/HTML test
  files no longer produce phantom imports.
- **Transitive module imports are checked**: an unresolved module anywhere in a
  unit's import closure is a Config error naming it and its importer (was:
  silently dropped, opaque clang error later). `std`/`std.compat` exempt (wired
  from the toolchain manifest, not target BMIs).
- **Duplicate module names are a Config error** (two .cppm units declaring the
  same module, within or across targets) — was: samurai "multiple rules" abort
  or silent first-wins BMI wiring.
- **Dry run (`-n`) has no side effects**: pre/post_build hooks don't fire and
  `test`/`run` don't execute binaries (they report what would run).
- **`clean` cannot be combined with other tasks** (it deleted the manifest the
  rest of the run needed) — clear Config error instead.
- **Fetch hardening**: staging names are unique per invocation (pid+seq) — two
  concurrent fetches of the same package can no longer rename each other's
  *unverified* bytes into the permanent cache; cached tarballs are re-verified
  against the sha256 on every run (corrupt cache → refetch); lockfile
  read-modify-write is serialized with an advisory `flock` (POSIX, best-effort
  elsewhere); tarballs extract with `--no-same-permissions` (no setuid bits from
  hostile archives); package-spec values (conan/vcpkg JSON → -I/-L/-l/-D) are
  whitelist-validated — a hostile recipe cannot reach command execution; lockfile
  comments are line-leading/whitespace-preceded `#` only (git tags may contain `#`).
- **proc/fs hardening**: `write_file`'s temp is pid+counter and pre-unlinked (no
  symlink-following on a predictable name); `reload`'s staging copy likewise and
  keeps the binary's original mode (was: predictable name + 0700 downgrade);
  `posix_spawn` file actions are RAII-guarded; a failed `file_actions_init` is
  fatal (was: silently dropped the requested capture when cwd was empty);
  `addchdir` failures report `Io` + `strerror` (was: `Config`); captured output
  is capped at 16 MiB (drain-to-EOF past the cap so the child never blocks).
- **Glob semantics**: a full `**` component matches whole components only
  (`**/x.cpp` no longer matches `fox.cpp`, `a/**/b` no longer matches `a/xb`);
  non-directory scan roots match nothing instead of erroring; mid-walk
  increment errors degrade to the matches collected (like `fs::walk`).
- **vcpkg/conan**: the vcpkg manifest is built with nlohmann (names with quotes
  can't corrupt/inject JSON); unmapped host architectures are a Toolchain error
  (was: silent x64 fallback); vcpkg install runs in the project cwd explicitly;
  MSVC `/LIBPATH:` is quoted (spaces); `-fPIC` is not emitted for non-MSVC
  Windows targets (MinGW ignores it); `flags: Warnings::Default` and the quoted
  `/LIBPATH` are pinned by tests.
- **Path safety**: `path_is_safe` also rejects quotes, backticks, shell
  metacharacters (`; | & < > ( ) \ # ~ * ?`) and control characters — these
  strings end up in ninja statements run via `/bin/sh -c` and in `ar -M` MRI
  scripts. `sources_in("")` no longer globs `/**.cpp` (contributes nothing →
  'has no sources' validation fires); a bare `import_archive("libfoo.so")` no
  longer emits a dangling `-Wl,-rpath,`.
- **Modernization notes**: `std::function` is gone from src (deducing-this
  recursive lambdas everywhere; `TaskFn` stays `std::function` because this
  libc++ does not ship `std::move_only_function` yet — revisit when it does);
  `split_words`/partition-list splitting via `std::views::split`; duplicate
  checks (`fetch_all`, target names, task names, custom-command outputs) via
  sort + `adjacent_find`; `ranges::contains`/`views::keys`/`ranges::to` where
  they read better; one shared `detail::split_env_list` for the PATH-style
  walks (os.cpp + fs.cpp); `os::spawn` unwraps via `expected::transform`.
  NOT done on purpose: wholesale `and_then`-chaining of the error checks
  (churn without behavioral gain), `views::join_with` for `join_spaced`
  (libc++ support uncertain).

## 32. Second review round (docs + hardening, Nov 2026)

Documentation: main page (docs/mainpage.dox) mirrors the README with links into
the generated reference; grp_api/grp_internals groups; 0 doxygen warnings
(./setup.sh docs). Gotchas: a /** sequence INSIDE a comment or a string literal
in an INPUT file makes doxygen count nested comment opens (write patterns as
"**.cpp" + "/" concatenation in sources_in); a standalone \defgroup + \{ comment
must sit INSIDE the namespace it brackets, or the \} pairing fails ("end of file
while inside a group"); deducing-this templates break @copydoc targets — write
self-contained briefs on overload families instead.

Review round (all verified by ASan/unit suite):

- **fetch_all worker pool lifetime**: `nthreads` was declared after the
  `jthread` pool vector, so at scope exit it was destroyed BEFORE the pool
  joined — workers read a dead stack variable every loop iteration
  (`i += nthreads`). Latent race (timing-dependent under ASan), caught as a
  deterministic stack-use-after-scope once the per-job stamp write changed
  worker timing. Rule: every variable a worker reads is declared BEFORE the
  pool vector (same rule as the §13 jthread note; the scan-deps pool in
  build.cpp already did this).
- **fetch validation**: sha256 must be 64 hex (it names the cache file) — a
  first cut reused looks_like_commit (40-hex git commits) and broke every
  tarball fetch; tags/urls reject whitespace/control characters (they land in
  argv and in the one-pin-per-line lockfile).
- **fetch_all integrity**: a `.truss-ok` completion marker is written after a
  full materialize and required by the offline fast path (a dest left partial
  by an interrupted run is no longer served forever); the lockfile guard takes
  O_CLOEXEC (an inherited fd carries the flock into children and would wedge
  the project's lockfile after a SIGKILL) and reports failure instead of
  silently racing; a lost clone race (rename over an existing dest) is a win,
  not an error; failed clones/fetches clean up their staging trees.
- **git-sourced packages get the same symlink-escape check as tarballs**, and
  copy_tree recreates symlinks as links (branching on is_symlink BEFORE the
  is_regular/is_directory checks — following them hardlinked escaping targets
  into the build tree and turned link-to-dir into empty real directories).
- **write_file creates its temp exclusively** (::open O_CREAT|O_EXCL|O_NOFOLLOW
  + write + rename) — the old remove-then-ofstream had a symlink-planting
  TOCTOU window and the comment overclaimed protection.
- **fs::walk takes the directory BY VALUE** — a coroutine frame stores
  reference parameters as references, and the lazy generator first touches
  `dir` only when the caller advances begin(); a temporary argument dangled.
  Same discipline as lines_of. Also: read_lines opens once (stream moved into
  the frame) and strips CR (CRLF); read_file returns early for empty files;
  is_hidden/walk use path::native() components (no per-entry allocation).
- **Manifest injection closed at the boundary**: validate() rejects control
  characters (above all \n — ninja ends a binding there and the rest injects
  statements) in flags/defines/dirs/libs; resolve_sources rejects them in
  globbed file paths; path_is_safe already covers the rest.
- **find_truss_modules**: BMI-presence check factored into try_add (all three
  branches now require both BMIs), results deduped, and a moved-from path
  push_back (insert(std::move(p)) then push_back(p)) fixed.
- **proc**: adddup2/addclose return codes checked (a silent failure ran the
  child without the redirect); capture buffer 64 KiB.
- **Small fixes**: generator::begin() guards a null handle; redundant
  operator!= removed (C++20 rewrite); ModulesMode::Auto docs match the fixed
  scanner; launcher() rejects shell metacharacters (it prefixes every compile
  command); libc++ manifest -isystem dirs go through path_is_safe;
  cross-config target triples are charset-validated; output_path_for ends in
  std::unreachable(); the dead clean/compdb branches, kind_name fallback,
  <sstream> include removed; has_runner factored; the compdb task calls
  Project::compile_commands(); the cross-test skip warning reads like a
  sentence (setup.sh cross-host asserts the new text); MSVC /I and .lib names
  quoted/normalized; empty defines skipped; unreachable switch case removed;
  sanitize driver flags table aliased to the compile table; resolve_debug
  hoisted; flag-table indexing shared; kGray renamed kCyan (ANSI 36);
  amalgamate uses string::starts_with; init checks the chmod result; setup.sh
  usage() parses the header instead of a fixed line range, cmd_dist uses
  find -print -quit (SIGPIPE under pipefail), add-package creates nested
  vendor dirs, cmd_unit compiles echo through job().

Deliberately left (documented, not bugs): the five dep-graph DFS walks are not
unified into one generic walker (each has different policy/error plumbing —
consolidation is a refactor, not a cleanup); resolve_module_imports keeps the
space-joined flags string (BMI paths are path-is-safe relative paths under the
project invariant; a vector-of-pairs return would ripple through tests);
glob's regex-per-entry matching and depth pruning (the literal-prefix rooting
already bounds the common case); std::exit(0) in the CLI --help path (fine for
the bundled CLI); esc_value/esc_path remain as the unit-tested escaping seam.
