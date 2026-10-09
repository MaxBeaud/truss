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

- Phase 5: exact `fetch` option surface (git-only vs tarball+sha256 both? lockfile format
  ini vs json?).
- Phase 6: conan 2 assumed (v1 EOL) — confirm; vcpkg manifest vs classic mode.
- Phase 7: whether `import std;` becomes the default inside truss's own module once
  libc++ + clang trunk combination is verified stable with sanitizers + hardening.

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
