# Changelog

All notable changes to truss are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/); the project adheres to
[Semantic Versioning](https://semver.org/) from 1.0.0 on: the public module
surface (`import truss;` — everything in the API reference) is stable within
a major version.

## [Unreleased]

### Added

- **Module-scan result cache** — compiler scans (~10 ms per TU) are cached
  under `build/<mode>/scan-cache/`, keyed by source path, mtime, size,
  compile flags and compiler identity. An unchanged tree now rescans nothing:
  a no-op `./truss` invocation on a 300-TU project drops from ~750 ms to
  ~5 ms. Corrupt entries are misses; write failures never fail the build;
  `clean` (or deleting the directory) forces a rescan.
- **Scan-skip gate** — a TU with no `import`/`module` in its raw text is
  never handed to the compiler (the keywords cannot be macro-generated), so
  a 300-TU tree of plain TUs now configures in ~5 ms even cold.
- **Build output** — phase lines with timings and scan-cache counters
  (`▸ configure  4 ms · scan 287 hot / 2 cold / 3 skipped`), samurai edge
  descriptions that name the source (`CXX src/net/tcp.cpp`), and a live
  in-flight progress block on a terminal (finished files disappear; FIFO
  eviction, compiler diagnostics print verbatim above it). Disable with
  `TRUSS_PROGRESS=plain`; piped output is unchanged/plain.
- **Log controls** — `-q/--quiet`, `TRUSS_LOG=quiet|warn|info|debug`, and
  `NO_COLOR` / `CLICOLOR_FORCE` / `CLICOLOR` are honoured.
- `proj.compile_commands()` / `./truss compdb` now writes through a temp
  file and only replaces `compile_commands.json` when its content changed
  (no mtime churn, no "wrote ..." line on unchanged runs).
- **Download progress** — on a terminal, `fetch()` streams git's and curl's
  native progress bars (curl runs with `--progress-bar` and without `-s`;
  git's detached-HEAD advice is suppressed). Piped output still captures the
  tools' diagnostics for error messages.

## [1.0.0] — 2026-11-16

The engine was feature-complete at 0.1.0 (all thirteen PLAN.MD phases); 1.0
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

Initial public state: all thirteen PLAN.MD phases — targets and globbing,
usage requirements, C++20/26 modules with transitive BMI closure and
`import std` (clang + libc++), static linking, zig cross-compilation,
`fetch`/`conan`/`vcpkg`, MSVC flag mapping, hardening tiers, merged
archives, custom commands, and the self-rebuilding `./truss`.
