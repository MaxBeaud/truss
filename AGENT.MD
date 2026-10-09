# AGENT.MD — Constraints & Guidelines for working on truss

Read `PLAN.MD` first. This file is the rulebook for any agent/human modifying this repo.

## 1. Project identity

- truss = a C++26 build-system library ("build your C++ using C++"), mate.h API style,
  xmake-class features, samurai backend.
- truss itself: **clang (trunk-pinned) + libc++ + `-std=c++26`**, built by **xmake**
  (debug/release modes). Never introduce CMake or another build system for truss itself.
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
  chaining AND a matching free-function pipe adapter (`with_*` + `operator|`) exists.
  Both styles must stay in lockstep — adding an option means adding both forms.
- **Parallelism only where it pays**: glob scanning, `clang-scan-deps`, package fetches use
  the internal `parallel_for` (jthread pool). Do NOT parallelize what samurai already
  parallelizes (compilation). Do NOT use `std::execution` (not stable in libc++ yet).
  Every parallel site needs a comment justifying why.
- **Naming**: types `PascalCase`, functions/methods `snake_case` (mate.h heritage + xmake
  friendliness), constants `kPascalCase` or `UPPER_SNAKE` for enum values matching the
  public enums in PLAN.MD. Namespaces: everything under `truss::`, internals in
  `truss::detail` or unnamed namespaces in implementation units.
- **Warnings are errors-adjacent**: the xmake.lua warning set (`-Wall -Wextra -Wpedantic
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

- Third-party code truss itself needs → `vendor/` (justfile `add-package` symlink flow or
  git clone pinned to a tag). Currently allowed: `samurai` (ISC), `catch2` (BSL).
  Anything else needs a justification comment in the vendor note file.
- Do not vendor a JSON parser until phase 6 actually needs it.
- Generated/fetched artifacts for USER projects go under `build/<mode>/packages/` — never
  into the source tree. truss's global download cache: `~/.cache/truss/`.

## 5. Build & workflow

- Build truss: `xmake f -m debug && xmake` (or `just build debug`). Release: `-m release`.
- Run tests: `just test` (Catch2 target, ASan/UBSan active in debug).
- Never commit `build/`, `compile_commands.json`, `truss.lock` churn (gitignore covers).
- **Phase discipline**: implement PLAN.MD phases in order; each phase ends with its
  "Done when" criterion demonstrably passing before starting the next. Small commits,
  prefix: `feat:`, `fix:`, `test:`, `chore:`, `docs:`, `refactor:`.
- Every phase adds/keeps `examples/hello` (and later `tests/fixtures/*`) building green.

## 6. Testing rules

- New utility (glob, flag table, ninja emitter, triple detection) ⇒ Catch2 unit test in the
  same commit. Ninja emitter tests assert on **golden manifest strings**.
- Rebuild-minimality is a tested property: integration fixtures assert that touching file X
  recompiles exactly the expected object set (via samu `-d explain` output or mtimes).
- Integration harness invokes the real `./truss` binary; no mocking of samurai.
- Keep ASan/UBSan green in debug CI-equivalent (`just test`).

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
  [--build-dir PATH]`; built-ins `test`, `run`, `clean`, `compdb`; unknown ⇒ user task.
- Compiler flag enums map through constexpr tables in `flags.cpp`; GCC + Clang complete,
  MSVC stubs return a `Toolchain` "not yet supported" `Error` (never silent wrong flags).

## 8. Things explicitly OUT of scope (until a phase says otherwise)

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

- xmake 3.1 + clang 23: cross-target named-module consumption requires a
  `set_kind("moduleonly")` target providing the BMIs (`truss-mod`), consumers `add_deps` it.
  Plain `add_deps` on a static lib does NOT propagate the BMI.
- Module units: `#include` must live in the global module fragment (`module;` … includes …
  `module truss;`). Includes in the module purview attach std to `truss` (clang errors).
- Partition layout: `truss:api` (public types, re-exported by the primary interface via
  `export import :api;`) + `truss:impl` (internal helpers, NOT re-exported). Implementation
  units cannot import the primary interface of their own module — that is why all public
  types (incl. `Project`) live in `:api`.
- xmake: `set_config("builddir", ".xmake")` (the `buildir` key is deprecated). File
  exclusion syntax: `add_files("dir/**.c|samu.c")`.
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
  by `tests/unit/usage_test.cpp` (scratch dir + chdir + real `proj.build()`; Catch2
  `INFO` needs the ternary parenthesized: `<<` binds tighter than `?:`).

## 14. Verified toolchain facts (learned during phase 7 — bootstrap.sh)

- Plain clang++ can build truss's module without xmake: compile each interface unit once
  with `-c -fmodule-output=<pcm>` (object + BMI in a single pass — no `--precompile`
  two-pass dance). Order: `truss:api` -> `truss:impl` (needs :api) -> primary `truss`
  (needs :api + :impl); implementation units need all three via `-fmodule-file`.
- **Consumers of `import truss;` need TWO flags**: `-fmodule-file=truss=...` AND
  `-fmodule-file=truss:api=...`. clang does not embed imported BMIs into the emitted
  BMI; the primary re-exports :api but references it by name. `:impl` is NOT needed by
  consumers (the primary never imports it). `--install` stages both pcms.
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
  (pre-order, deduped via a seen-set) so exe -> libA -> libB links in GNU ld order
  (direct deps first, their deps to the right). Regression test:
  `usage_test.cpp "link: transitive static-lib deps are linked in order"`.
- **samurai in-process re-entry bug**: the shim runs multiple builds per process, but
  upstream `logclose()`/`depsclose()` fclose their FILE* without NULLing it → the next
  `loginit`/`depsinit` double-frees. Vendored one-line patches (`/* truss patch */`),
  recorded in `vendor/samurai/TRUSS-VENDOR.txt`.
- Self-host (`truss.cpp` at repo root): targets `samu` (C, `remove_source` drops
  `samu.c`), `truss-lib` (`sources_in("src")` picks up .cppm/.cpp/.c), `truss`
  (this file; `import truss;` resolved cross-target via `collect_bmis` + depends_on).
  Must set `.cpp_standard(CppStandard::Cpp26)` explicitly — `Default` maps to c++23.
  The self-build uses clang's default stdlib (libstdc++) consistently; the xmake build
  uses libc++ — the two graphs are independent and each is internally consistent.
- Installed-tool flow: `bootstrap.sh --install <prefix>` stages
  `<prefix>/lib/truss/{libtruss.a,truss.pcm,truss_api.pcm}`; a project compiles its
  script with both `-fmodule-file` flags (§14) — fixture `examples/installed`, recipe
  `just installed`.

## 16. Verified toolchain facts (learned during phase 9 — amalgamation)

- `tools/amalgamate.cpp` generates `dist/truss_all.hpp` (~188 KB): samurai .c +
  shim + api/impl/core units concatenated; module directives/partition imports
  stripped; `export namespace` -> `namespace`; local `#include "x.h"` inlined once
  (samurai headers have no guards — the tool dedupes); system includes hoisted.
  `dist/` is gitignored (regenerate: `just amalgam`).
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
