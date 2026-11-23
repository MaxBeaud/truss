# Changelog

All notable changes to truss are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/); the project adheres to
[Semantic Versioning](https://semver.org/) from 1.0.0 on: the public module
surface (`import truss;` — everything in the API reference) is stable within
a major version.

## [Unreleased]

### Added

- MIT `LICENSE` (2026, Maxime Beaudoin).
- Target switches, all validated at `build()`: `as_shared()` (library kind),
  `output_name()` (artifact basename; unique per subgraph), `build_by_default(false)`
  (opt-in, outside `all`), `default_target()` (the executable `run` starts),
  `symbols_hidden()` (visibility, no-op on MSVC) and `not_linkable()` (a shared
  library loaded at runtime: `depends_on()` on it is a Config error).
  `examples/dlopen` shows the pattern.
- First-class C: `c_standard(C99…C23)` and `c_flags()` (C sources only; MSVC
  accepts C11/C17), and `.ixx` is part of `sources_in()`.
- Module-scan cache and skip gate: unchanged files are neither scanned nor read
  (300-TU no-op: ~750 ms → ~5 ms); the cache is capped at 4096 entries.
- Build output: `▸ configure/build` phase lines with scan counters, per-edge
  source descriptions, a live in-flight block on a terminal (`TRUSS_PROGRESS=plain`
  disables it), `-q`, `TRUSS_LOG`, `NO_COLOR`/`CLICOLOR_FORCE`; downloads show
  git/curl progress bars.

### Changed

- `truss init` is compiled into every truss binary (templates `#embed`ed,
  ~3 KB); the internal `TRUSS_INSTALLED_BIN` switch is gone and `truss-dist`
  now differs from the dev binary only in static linking.
- `./setup.sh install` defaults to `~/.local` (PATH-visible) and warns when the
  staged `bin` is not on `PATH`.
- `--for SUBGRAPH` restricts plain builds too, not just `test`/`run`.
- `compile_commands()`/`compdb` and a dry run no longer write anything when the
  content is unchanged (`-n` writes nothing at all).

### Fixed

- Self-rebuild works in a truss checkout again (`setup.sh build` fallback for
  the removed `bootstrap.sh`); `setup.sh build` reuses the bootstrap (script-only
  rebuild 25 s → 0.2 s) and writes `truss.d`, so headers/BMIs are tracked.
- Concurrent `setup.sh` runs no longer wipe each other's bootstrap/unit objects
  (flock + per-PID snapshots).

### Security

- Include/link paths are shell-escaped inside their quotes; link library and
  framework names are charset-validated; git's `ext::` transport is refused;
  `reload` refuses to replace the binary with raised privileges.

## [1.0.0] — 2026-11-16

The engine was feature-complete at 0.1.0 (all thirteen original-plan phases); 1.0
adds the cross-build, module-scanning and robustness work, hardens the
outward-facing surfaces, and freezes the public API behind semver.

### Added

- **Removed `--cross`** — superseded by per-triple subgraphs
  (`proj.targets({...})` + `--for` / compound `--target name:subgraph`).
- **Per-triple subgraphs** — `proj.targets({{"linux", Config{}}, {"win", ...}})`
  builds the whole graph once per named configuration in one manifest:
  per-triple output dirs, per-subgraph `name:all` phony targets, per-subgraph
  runners, `--for NAME` task selection, `--target name:subgraph` compound
  build selection (a bare name applies to every subgraph that has it).
  Mutually exclusive with `configure()` (a Config error).
- **GCC named modules + `import std`** (GCC 15+): GCC targets compile modules
  through a per-target module-mapper file; the authoritative scan runs the
  compiler itself (`-fdeps-format=p1689r5`); the std CMIs build once per
  (triple, `-std`, BMI config) and are cached.
- **Linux→Windows cross builds with mingw-w64**: target-prefixed GCC drivers
  (`x86_64-w64-mingw32-g++` + `target("x86_64-w64-mingw32")`) are first-class;
  llvm-mingw (clang family) supports modules and `import std` cross; wine
  runs the outputs when configured as the runner.
- **Process timeouts** — `RunOptions::timeout` (default 600 s, 0 = none):
  children spawn into their own process group and the whole group is killed
  on expiry; a daemonizing grandchild inheriting a capture pipe can no longer
  wedge a build. `FetchOptions::timeout` (default 900 s) bounds downloads,
  with stall detection (abort under 1 KiB/s for 30 s) and connect timeouts.
- **Scanner lexer** — the module scanner's regex passes are replaced by a
  hand-rolled line scan (~50× faster); clang-scan-deps (clang) and the
  compiler itself (GCC) are the default authoritative scanners, with the
  lexer as fallback and for header-unit/spliced-import edge cases.
- **Lockfile discipline** — `fetch`/`fetch_all` serialize their pin
  read-modify-write under an advisory `flock` (120 s wait then a clear
  error); a `.truss-ok` completion marker keeps partially-materialized
  packages out of the offline fast path; cached tarballs are re-verified
  against their sha256 on every run.
- Documentation: a Doxygen main page mirroring the README, `grp_api` /
  `grp_internals` groups, and this changelog.

### Changed

- **Deducing-`this` builders**: every builder method returns the target's own
  type (`StaticLib&`, `CustomCommand&`, ...), so derived-only methods chain
  after base ones in any order. Source-compatible for existing scripts.
- `Warnings::Default` now actually emits the documented `-Wall -Wextra`.
- Archive link lines are ordered consumers-before-providers (post-order,
  reversed): diamond dependency graphs resolve in a single ld pass.
- Dry runs (`-n`) have no side effects: no hooks fire, no test/run binary
  executes.
- The std module's BMI is built with its consumers' hardening/sanitizer/LTO
  flags (a config mismatch is a hard error on some toolchains, e.g.
  llvm-mingw).
- The cross-test skip warning reads as a sentence and names the remedy.

### Security

- Control characters (notably newlines) are rejected in flags, defines,
  include/link dirs and globbed source paths — they would inject ninja
  manifest statements executed via the shell.
- `path_is_safe` additionally rejects quotes, backticks and shell
  metacharacters; package-spec values (conan/vcpkg JSON) are
  whitelist-validated before reaching any command line.
- `write_file` creates its temp file exclusively (`O_EXCL|O_NOFOLLOW`) —
  no symlink-following on predictable names; `reload` stages the new binary
  the same way and preserves the original mode.
- Fetched git trees get the same symlink-escape rejection as tarballs, and
  `copy_tree` recreates symlinks as links instead of following them.
- Tarballs extract with `--no-same-permissions` (no setid bits from hostile
  archives).

### Fixed

- GCC module targets never actually built (clang-only flags were emitted).
- The std module's BMI config mismatch on llvm-mingw.
- `fetch`'s sha256 validation, duplicate-cache races, staging cleanup on
  failure, and lockfile pins containing `#`.
- Glob: `**` as a full component matches whole components only; absolute
  patterns and non-directory roots behave predictably; CRLF files read via
  `read_lines` lose their `\r`.
- `fs::walk` no longer dangles on a temporary directory argument (coroutine
  frames hold references).
- A latent use-after-scope in `fetch_all`'s worker pool (a stack variable
  destroyed before the pool joined).

## [0.1.0] — 2026

Initial public state: all thirteen original-plan phases — targets and globbing,
usage requirements, C++20/26 modules with transitive BMI closure and
`import std` (clang + libc++), static linking, zig cross-compilation,
`fetch`/`conan`/`vcpkg`, MSVC flag mapping, hardening tiers, merged
archives, custom commands, and the self-rebuilding `./truss`.
