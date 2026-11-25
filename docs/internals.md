# Internals {#internals_page}

How truss is built internally. Everything here is **not public API**: the
`truss:impl` partition is not re-exported by the primary module, so `import
truss;` never sees it, and its contents may change between releases. The
[Internals group](@ref grp_internals) lists the internal declarations; this
page explains the architecture around them.

## Module structure

- `src/truss.cppm` — primary module interface; re-exports `truss:api` only.
- `src/api.cppm` (`truss:api`) — the public API: Project, Target hierarchy,
  Config, Error/Result, enums.
- `src/impl.cppm` (`truss:impl`) — internal declarations in `truss::detail`,
  imported by the implementation units but never re-exported.
- `src/core/*.cpp` — implementation units (`module truss;`) that define both
  partitions' functions. File-local machinery lives in anonymous namespaces
  there, so it is described below rather than in the group.

## Build flow (`Project::build()`)

1. **Validate** — target names, duplicate detection, clean-only invocations
   short-circuit before any resolution.
2. **Resolve** — once per subgraph (`proj.targets()`, defaulting to one
   unnamed subgraph): glob sources, scan module units (hand-rolled lexer, or
   the compiler's authoritative p1689 scan when available: clang-scan-deps /
   GCC `-fdeps-format`; unchanged units are served from `scan-cache/`), apply
   `Config` overlays to triples, resolve dependency graphs and link order.
3. **Emit** — `emit_manifest()` (anonymous namespace in `build.cpp`) writes
   the ninja graph: rules per language/BMI/PCH/link, unity TU generation,
   launcher (`ccache`) prefixing, per-edge variables; GCC module targets get
   a per-target module-mapper file; each subgraph gets its own `name:all`
   phony. The manifest is written with `write_file_if_changed` so an
   unchanged graph never dirties samurai.
4. **Execute** — vendored samurai runs in-process (`samu_build`; a live
   in-flight progress block on a terminal), then
   registered tasks (`test`, custom `task()`s) run.

## Subsystems

| Unit | Role |
|---|---|
| `src/core/fs.cpp` | glob engine (brace expansion -> linear-time DP matcher, depth-capped literal-prefix walks shared across targets with ancestor subsumption, hidden entries and `build_root` skipped), file helpers, `walk`/`read_lines` coroutines (`truss::generator`; libc++ lacks `std::generator`), native FIPS-180-4 sha256, `find_library` system dirs, truss.lock parsing |
| `src/core/modules.cpp` | module scanner: non-code blanking (comments/strings/raw strings) + hand-rolled line scan; BMI name resolution; per-compiler authoritative p1689 scanning (clang-scan-deps / GCC `-fdeps-format`); per-target module-mapper files for GCC's CMI paths |
| `src/core/scancache.cpp` | `build/<mode>/scan-cache/` store: keys a compiler scan result by (cwd, source, compiling flags, compiler binary size+mtime, cache format) and validates it against the source's (mtime, size); corrupt/truncated entries are misses, store failures never fail the build, and the hot/cold/skipped counters feed the phase log |
| `src/core/flags.cpp` | enum-indexed flag tables per compiler family (GCC/Clang/MSVC), triple/Config resolution, launcher detection, BMI-config flags |
| `src/core/ninja.cpp` | `NinjaWriter` + ninja escaping rules |
| `src/core/samu.cpp` | C shim over vendored samurai (`vendor/samurai`), in-process build + compdb; on a terminal, samurai's stdout is pumped through a pipe by a reader thread that renders the live in-flight block (`[started/total pct]` + the jobs currently compiling, FIFO-evicted as new ones start); compiler diagnostics clear the block and print verbatim, `TRUSS_PROGRESS=plain`/`-v`/`-n`/`--explain` keep the plain stream |
| `src/core/proc.cpp` | `posix_spawn` process runner (optional cwd via `addchdir_np`), output capture |
| `src/core/os.cpp` | `truss::os`: consteval host predicates, memoized fork-free `find_program` `$PATH` walk, lenient `env` parsing, machine specs, `temp_scope`, `find_truss_modules` auto-discovery |
| `src/core/pkg.cpp` | fetch (git/tarball + sha256), `~/.cache/truss` cache, hardlink materialization, `add_subdirectory`, conan/vcpkg fingerprint + configure-skip; on a terminal, fetch tools stream their progress bars (stderr inherited), off a terminal their diagnostics are captured |
| `src/core/system_lib.cpp` | `proj.system_lib()` probing (`pkg-config` + manual `.headers()`/`.libs()`/`.paths()`) with spec-keyed `Interface` targets and shell-token validation |
| `src/core/options.cpp` | typed build options (`Options`, `options.cache`, `-D` CLI overrides, `passthrough_options`, `proj.preset()`, `validate_options`) |
| `src/core/options_tui.cpp` | zero-dependency POSIX `termios` + ANSI TUI and decoupled `OptionsTuiState` state machine for `./truss configure` |
| `src/core/install.cpp` | Unix `./truss install` (`--prefix`, `DESTDIR`, `--strip`, atomic temp+chmod+rename, `install_manifest.txt`) and `./truss uninstall` |
| `src/core/target.cpp` | host triple detection, `Config` → `TargetTriple` resolution (`apply_config`), triple parsing, compiler-family/`c_driver` detection, launcher resolution |
| `src/core/build.cpp` | the emitter: rules, edges, unity batching, PCH edges, scan-deps wiring, `Project::build()` orchestration, `compile_commands` generation, `reload` task |
| `src/core/log.cpp` | colored stderr logging, the public `truss::log` channel |

## Invariants

- **No exceptions across the API**: everything internal returns
  `Result<T>` = `std::expected<T, Error>`; `std::filesystem` calls use the
  `error_code` overloads.
- **Path safety**: ninja paths escape `$`, `:` and spaces; every author-supplied
  path/name that reaches a build statement (target names, archives, custom-command
  outputs, the project/build dirs, `pch()`) goes through `path_is_safe`, which
  rejects shell metacharacters, quotes and control characters outright — those
  strings end up in commands samurai runs via `/bin/sh -c` and in `ar -M` MRI
  scripts. Package-spec values (conan/vcpkg) are whitelist-validated separately.
- **Determinism**: identical inputs produce a byte-identical `build.ninja`
  (sorted edges, memoized probes), so `write_file_if_changed` skips writes.
- **Command-line integrity**: every path that reaches a command line is either
  refused by `path_is_safe` (sources, targets, build dirs) or quoted through a
  shell escaper (`$`, backtick, `"`, `\` stay literal inside the quotes —
  include dirs and link paths may contain spaces but never expand anything).
  `-l`/`-framework` names are validated against a conservative charset instead
  of being quoted, since they are linker names rather than paths. `fetch()`
  values never reach a shell (argv only), must not start with `-`, and the
  git `ext::` transport (which executes commands) is refused outright.
- **Privilege boundary**: `reload` refuses to replace the executable when
  euid != uid (a setuid install must not let an unprivileged caller swap a
  root-owned binary); `write_file` publishes through an O_EXCL|O_NOFOLLOW
  sibling temp + rename, so a pre-placed symlink is never followed.
  `install()` publishes artifacts via a PID-unique temp sibling + `chmod` +
  `rename` (avoiding `ETXTBSY` on running executables and never writing through
  pre-existing symlinks), `install_headers()` skips symlinks inside header
  trees, and `system_lib()` rejects `pkg-config` tokens containing control
  characters or shell metacharacters.
- **Module wiring**: BMI edges are separate rules; consumers get
  `-fmodule-file=` flags from the scanner (Auto) or clang-scan-deps (Manual);
  unity never batches `.cppm` units.
- **Scan-cache invalidation**: the key covers what truss knows before the
  manifest exists (cwd, source path, resolved compile flags, compiler and
  scanner binary identity), and a hit also requires the source's (mtime, size)
  to be unchanged. Header *content* is deliberately not part of the key — the
  compiler's header deps do not exist yet when the graph is generated, and
  asking the compiler is the cost being avoided. Consequence: adding an
  `import` inside an included header does not invalidate that TU's entry until
  the TU itself changes; `./setup.sh clean` (or deleting `build/<mode>/scan-cache/`)
  forces a rescan. Adding an `import` to the TU itself — the normal way —
  changes its size and invalidates it.
- **Scan-skip gate**: a TU whose raw text contains neither `import` nor
  `module` is never handed to the compiler — import/module declarations
  cannot be macro-generated, so the scan's answer is provably empty. The
  check is textual and conservative (a keyword inside a comment or string
  literal still triggers a scan); it saves ~10 ms per plain TU on cold
  builds and is never wrong.
- **jthread pools**: locals captured by reference must be declared *before*
  the pool vector (destruction order would otherwise join too late).

## Vendored samurai

`vendor/samurai` is samurai (ninja-compatible backend) compiled into truss
as a static shim. Patches are minimal and listed in `vendor/samurai/TRUSS-VENDOR.txt`;
`AGENT.MD` records the verified behavioral quirks (e.g. clang-scan-deps
p1689 output shape on LLVM 23).
