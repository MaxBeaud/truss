# AGENT.MD — rules for changing truss

The rulebook for anyone (human or agent) modifying this repo. Read
`README.MD` (user guide), `docs/internals.md` (architecture), `docs/history.md`
(why the design is what it is) and `docs/1.0-roadmap.md` (what comes next)
before changing behaviour.

## 1. What truss is

A C++26 build-system library: build scripts are ordinary C++ programs
(`import truss;` + a fluent builder + `proj.build()`), executed by a native
`./truss` binary and driven by the vendored samurai backend.

- truss itself: **clang + libc++ + `-std=c++26`**, built by `./setup.sh`
  (plain clang++, no build system). Never add CMake/xmake/make for truss.
- Dogfood: `truss.cpp` at the repo root builds truss with truss.
- Host scope: **Linux only**. Windows/macOS *hosts* and CI configs are out of
  scope; cross-*targeting* Windows from Linux is supported. forge is a
  separate project and out of scope here.

## 2. Hard constraints

- **Errors**: `truss::Result<T> = std::expected<T, Error>` for every fallible
  operation, internals and public API. No exceptions, no `throw`; the library
  is compiled with exceptions disabled. Messages are actionable (what is wrong,
  `correct:`/`incorrect:` examples) — that is the API's personality.
- **Memory**: no raw `new`/`delete`/`malloc` in C++. Values, containers,
  `unique_ptr` (only when a value will not do). `string_view`/`span` in,
  `std::move` on sinks, `emplace_back`, reserve-and-append in hot loops.
- **Public API is frozen** for 1.x (semver): additions are fine, breaking
  changes need a major. Document every public declaration with `///` (Doxygen).
- **Warnings stay clean** with the setup.sh set (`-Wall -Wextra
  -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast …`).
  No C-style casts (use `static_cast`, with a comment for `reinterpret_cast`),
  no `using namespace std;`.

## 3. Style

- Types `PascalCase`; functions/methods `snake_case`; constants `kPascalCase`;
  enum values match the public enums in `src/api.cppm`.
- Everything lives under `truss::`; internals in `truss::detail` or unnamed
  namespaces in implementation units.
- Builder methods are deducing-`this` templates returning `Self&`, so derived
  methods chain after base ones. One obvious way to configure a target — no
  `with_*`/`operator|` adapters.
- Parallelism only where it pays (globs, compiler scans, fetches; never
  compilation — samurai owns that). Comment each parallel site. No
  `std::execution` (not shipped by libc++).

## 4. Module layout

- `src/truss.cppm` — primary interface, re-exports `truss:api` only.
- `src/api.cppm` (`truss:api`) — the entire public surface.
- `src/impl.cppm` (`truss:impl`) — internal declarations for `truss::detail`.
- `src/core/*.cpp` — implementation units (`module truss;`).
- Dependency direction, no cycles: `fs/proc/log` ← `target/flags` ←
  `ninja/modules` ← `pkg` ← `build`.
- `truss.cpp` (repo root) is both truss's build script and the shipped CLI's
  `main` (`truss init`, outside-checkout usage); there is no separate CLI
  source. `truss-dist` is the same TU with static linking.
- Vendored samurai is C17 and **patched only as documented** in
  `vendor/samurai/TRUSS-VENDOR.txt`; all contact goes through `src/samu/shim.c`
  and `src/samu/truss_samu.h`.

## 5. Vendoring

- Third-party code for truss itself → `vendor/<pkg>/` with provenance in
  `TRUSS-VENDOR.txt` (URL, commit, license). Currently: samurai (ISC),
  doctest (MIT, `tests/doctest/`), nlohmann/json (MIT, `vendor/nlohmann/`).
  Anything else needs a justification.
- User-project packages go to `build/<mode>/packages/` and the global cache
  `~/.cache/truss/` — never into the source tree.

## 6. Workflow

- `./setup.sh build` — bootstrap `libtruss.a` + BMIs (reused when fresh) and
  compile `./truss`. `./setup.sh test` — unit + examples + installed + amalgam;
  `./setup.sh unit` — doctest with ASan/UBSan; `./setup.sh docs` — Doxygen
  (warnings must stay at 0).
- Never commit generated output: `build/`, `compile_commands.json`,
  `examples/*/truss`, `.local/`, `truss.d`. `.gitignore` covers them.
- One logical change per commit, verified (`./setup.sh test` for behaviour,
  `docs` for documentation), prefixed `feat:`/`fix:`/`test:`/`chore:`/
  `docs:`/`refactor:`. Keep `examples/*` green.
- Work is planned in `docs/1.0-roadmap.md`; finish an item before starting the
  next. Record deliberate omissions there rather than leaving them implicit.

## 7. Testing

- Every utility (glob, flags, emitter, scanner, cache) gets a doctest unit test
  in the same commit. Ninja emission is asserted on **golden manifest text**.
- Rebuild minimality is a tested property: touch a source and assert the
  expected object set is rebuilt (mtimes / dry-run manifests).
- Integration tests drive the real `Project` API in scratch dirs; no mocking of
  samurai. Real-toolchain e2e tests skip with `MESSAGE(...)` when the tool is
  missing (zig, cppfront, clang-cl, …), never fail the suite.

## 8. Public API invariants

- `Project` parses the CLI in its constructor; `build()` is the only entry
  that touches disk/network. Task functions run after a successful build.
- Validation happens in `build()` with actionable Config errors. New target
  methods must be validated there, not in the builder.
- Artifacts: bare target name → platform naming (`lib` prefix, extension) under
  `build/<mode>/{bin,lib}/<arch>-<os>-<compiler>/`. `output_name()` changes
  only the basename; it must be bare and unique per subgraph.
- `build_by_default(false)` keeps a target out of `all`; `default_target()` is
  one executable per subgraph; `not_linkable()` is shared-library-only and
  makes `depends_on()`/`uses()` a Config error; `as_shared()` picks the library
  kind. `symbols_hidden()` is a compile flag (no-op on MSVC).
- CLI: `./truss [debug|release] [command...] [-j N] [-q] [-v] [-n] [--explain]
  [--target NAME[:SUBGRAPH]] [--for SUBGRAPH] [--build-dir PATH] [-- ARGS...]`.
  Built-in commands: `test`, `run`, `clean`, `compdb`, `reload`; user tasks run
  via `task NAME`. `--for` narrows `build`/`test`/`run` to one subgraph.
  `run`/`test` build only what they execute; a bare `./truss` builds `all`.
- Languages are chosen by extension: `.cpp/.cc/.cxx` (C++), `.cppm/.ixx`
  (module interface), `.c` (C, `c_standard()`/`c_flags()`). Headers are never
  compiled and carry no language policy. Anything else is a Config error.
- Compiler flags flow through the constexpr tables in `flags.cpp`; all three
  families (GCC, Clang, MSVC) are implemented. A knob that a toolchain cannot
  honour is a Config error, never a silent wrong flag.

## 9. Verified facts and traps (do not re-derive)

### Modules

- clang: producer `-fmodule-output=<pcm>`; consumers
  `-fmodule-file=NAME=PATH`; the pcm must be a declared implicit output of the
  edge or samurai deletes/ignores it.
- GCC: `-fmodules` + a per-target module-mapper file (`-fmodule-mapper=…`,
  `$root .` relative paths); GCC rejects `--target=` and `-fmodule-output` —
  the target-prefixed driver *is* the cross selection.
- Consumers need BMIs for **every partition** of a primary they import; the
  wiring is a full transitive closure, and an implementation unit implicitly
  imports its primary.
- Scanning: clang via `clang-scan-deps` (single-entry CDB; LLVM 23 leaves
  `primary-output` empty otherwise), GCC via `-fdeps-format=p1689r5`.
  `import` cannot be macro-generated, so a TU whose raw text lacks
  `import`/`module` needs no scan. Results are cached by (path, mtime, size,
  flags, compiler identity); the text scan by (path, mtime, size).
- `import std;` is real on clang+libc++ and GCC 15+; the std BMI must be built
  with the consumers' exact BMI-config flags (hardening/sanitizer/LTO).

### samurai

- `$in` expands to explicit inputs only — archives/shared deps must be listed
  as explicit inputs after the objects.
- Rule bodies keep `$placeholders` verbatim; edge variables override rule
  variables (`description`, `depfile`, `rspfile`, …).
- It never frees its graph: `__lsan_default_options` in the shim is required
  under ASan. `logclose()`/`depsclose()` are patched to NULL their `FILE*`
  because truss re-enters them in one process.
- `deps = msvc` is a documented truss patch (`/showIncludes`).

### Emission / filesystem

- Manifest values escape `$`; paths additionally escape `:` and space.
  Paths reaching a shell (`path_is_safe`) reject spaces, quotes, `$`, `;`,
  `|`, control characters, … Include/link paths are the exception: they are
  `sh_dq`-escaped and may contain spaces.
- `write_file` writes an `O_EXCL|O_NOFOLLOW` sibling temp then renames;
  `write_file_if_changed` keeps mtimes stable (manifest, unity batches, DBs).
- `std::filesystem::create_directories("")` sets `ec` — guard empty parents.
  Throwing overloads (`remove_all`, `relative`, …) are never used.
- Setting a test file's mtime to `now()+1s` makes it perpetually dirty; tests
  step mtimes forward instead.
- `std::filesystem::relative(p, base)` canonicalizes **both** sides.

### C / MSVC

- C sources must never receive `-std=c++XX`.
- MSVC: `/Fo$out` attached (space form breaks), `/showIncludes` deps, `/Z7`,
  `/std:c++latest`; `link.exe` flags split at `/link`. C++ modules on MSVC are
  a clear error.
- MSVC has no C99/C23 mode (`C11`/`C17` only).

### libc++ / doctest / tooling

- libc++ lacks `std::generator` (truss has its own) and `std::move_only_function`
  (tasks use `std::function`).
- `std::expected` cannot hold `T&` (`TargetRef` is the never-null handle).
- Inline members defined in a module interface are emitted **once per
  program** — keep non-template definitions out of `api.cppm`/`impl.cppm`.
- doctest: `CHECK(a || b)` → "Expression Too Complex"; hoist to a `bool`.
  Extra args need `_MESSAGE`/`INFO`.
- Doxygen 1.18: `/*` inside `/** */` nests; `\defgroup … \{` must sit inside
  the namespace it brackets; `@copydoc` breaks on deducing-this signatures.

### Performance / output

- No-op `./truss` must stay ~single-digit ms: scans and text reads are cached,
  the manifest is written only when changed, globs walk literal prefixes.
- `ccache`/`sccache` prefix C/C++ compiles only (BMI and MSVC rules stay raw).
- Phase lines print after their phase (configure → build → program output);
  the live progress block is tty-only and `TRUSS_PROGRESS=plain` disables it.
- `./setup.sh` serializes bootstraps (`flock build/.bootstrap.lock`) and works
  from a per-PID snapshot, so concurrent runs cannot wipe each other; it must
  never hold that lock while driving `./truss` (self-rebuild would deadlock).

### Security boundaries

- `path_is_safe` for sources/targets/build dirs; `sh_dq` for include/link
  paths; `link_libs`/`link_frameworks` names are charset-validated (no leading
  `-`). `defines()`/`flags()`/`compiler()` remain raw text by design — trust
  boundary is the build script.
- `fetch()` values never touch a shell, must not start with `-`, and the git
  `ext::` transport is refused. Fetched trees are scanned for escaping symlinks
  before publication; tarballs are sha256-pinned and extracted without
  ownership/permission bits.
- `reload` refuses to replace the executable when euid != uid.

## 10. Out of scope (until the roadmap says otherwise)

CI configuration, Windows/macOS hosts, MSVC C++ modules, WASI/emscripten, CMake
interop for vendored deps, package publishing, IDE integration beyond
`compile_commands.json`, `std::execution`, custom allocators/arenas, and the
shelved cppfront/Carbon language surface.
