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
2. **Resolve** — glob sources, scan module units (regex or clang-scan-deps
   in `ModulesMode::Manual`), apply `Config` overlays to triples, resolve
   dependency graphs and link order.
3. **Emit** — `emit_manifest()` (anonymous namespace in `build.cpp`) writes
   the ninja graph: rules per language/BMI/PCH/link, unity TU generation,
   launcher (`ccache`) prefixing, per-edge variables. The manifest is written
   with `write_file_if_changed` so an unchanged graph never dirties samurai.
4. **Execute** — vendored samurai runs in-process (`samu_build`), then
   registered tasks (`test`, custom `task()`s) run.

## Subsystems

| Unit | Role |
|---|---|
| `src/core/fs.cpp` | glob engine (brace expansion -> one regex per alternative, literal-prefix walk, hidden entries skipped), file helpers, `walk`/`read_lines` coroutines (`truss::generator`; libc++ lacks `std::generator`), native FIPS-180-4 sha256, `find_library` system dirs, truss.lock parsing |
| `src/core/modules.cpp` | regex module scanner (`export module`/`import` line matching), BMI name resolution, p1689 `scan_deps_requires` fallback |
| `src/core/flags.cpp` | enum-indexed flag tables per compiler family (GCC/Clang/MSVC), triple/Config resolution, launcher detection |
| `src/core/ninja.cpp` | `NinjaWriter` + ninja escaping rules |
| `src/core/samu.cpp` | C shim over vendored samurai (`vendor/samurai`), in-process build + compdb |
| `src/core/proc.cpp` | `posix_spawn` process runner (optional cwd via `addchdir_np`), output capture |
| `src/core/os.cpp` | `truss::os`: consteval host predicates, memoized fork-free `find_program` `$PATH` walk, lenient `env` parsing, machine specs, `temp_scope`, `find_truss_modules` auto-discovery |
| `src/core/pkg.cpp` | fetch (git/tarball + sha256), `~/.cache/truss` cache, hardlink materialization, conan/vcpkg fingerprint + configure-skip |
| `src/core/target.cpp` | host triple detection, `Config` → `TargetTriple` resolution (`apply_config`), triple parsing, compiler-family/`c_driver` detection, launcher resolution |
| `src/core/build.cpp` | the emitter: rules, edges, unity batching, PCH edges, scan-deps wiring, `Project::build()` orchestration, `compile_commands` generation, `reload` task |
| `src/core/log.cpp` | colored stdout logging, the public `truss::log` channel |

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
- **Module wiring**: BMI edges are separate rules; consumers get
  `-fmodule-file=` flags from the scanner (Auto) or clang-scan-deps (Manual);
  unity never batches `.cppm` units.
- **jthread pools**: locals captured by reference must be declared *before*
  the pool vector (destruction order would otherwise join too late).

## Vendored samurai

`vendor/samurai` is samurai (ninja-compatible backend) compiled into truss
as a static shim. Patches are minimal and listed in `vendor/samurai/TRUSS-VENDOR.txt`;
`AGENT.MD` records the verified behavioral quirks (e.g. clang-scan-deps
p1689 output shape on LLVM 23).
