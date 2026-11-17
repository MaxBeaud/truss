# Cross-compilation strategy {#cross_compilation_page}

Status: **phase 1 landed** (project-wide target triple, zig toolchain, target-aware
naming/link conventions). Phases 2–5 below are the proposed roadmap.

## Goals

1. One build script builds for any target the toolchain can reach, without editing it:
   `./truss release` for the host, the same script with a different target for
   `aarch64-linux-musl`, `x86_64-windows-gnu`, ...
2. Outputs for several targets coexist in one build tree (no clean between targets).
3. A shipped binary runs on as many machines as possible (old glibc, no C++ runtime
   installed) — truss's own `truss-dist` is the first customer.
4. Errors over guesses: an impossible combination (GCC + `--target`, `Gcc` static
   runtime on a libc++ toolchain, `-Bstatic` on macOS) is a Config error with a
   correct/incorrect example, never a silent wrong flag.

## Toolchain ranking

| Route | Sysroot | Strengths | Limits |
|---|---|---|---|
| **zig** (`Toolchain::Zig` + `target()`) | bundled for every target (glibc versions, musl, MinGW, macOS libSystem stubs) | zero setup; glibc version pinning (`x86_64-linux-gnu.2.17`); static libc++ by default | no ASan/UBSan runtimes; cannot build C++20 modules or PCH at all (use clang); macOS frameworks not bundled |
| **clang** (`target()` + `--sysroot`) | user-provided (Debian multiarch, `xwin` for MSVC, Android NDK, Apple SDK) | full clang feature set, sanitizers where the sysroot has them | sysroot management is on the user |
| **GCC** (prefixed driver) | comes with the cross package (`aarch64-linux-gnu-g++`) | distro-packaged, reliable | one driver per target; `target()` refused |
| **clang-cl** (MSVC ABI) | Windows SDK + MSVC CRT (`xwin`) | real MSVC ABI from Linux | no modules/static options yet |

Recommendation: **zig is the default cross route**, clang + sysroot the escape hatch,
GCC prefixed drivers supported as-is.

## Phase 1 — done

- `TargetTriple::target` + `Config::target(triple)`: emitted as `--target=` (clang,
  clang-cl) or `-target` (zig) on compile **and** link lines; GCC refuses it.
- `detail::parse_target` derives `arch`/`os` (and the ABI) from the triple, so:
  - `triple_dir` separates builds: `arm64-linux-musl-zig`, `x64-windows-zig`,
    `x64-linux-gnu.2.17-zig` (the default `gnu` ABI is omitted);
  - naming follows the target OS: `.exe`, `lib<name>.dll`, `.dylib`;
  - link conventions follow the object format: `-z relro`/`--as-needed`,
    `-soname`, `$ORIGIN` rpaths only on ELF; `-install_name` + `@loader_path`
    on macOS; no rpath on Windows.
- `Toolchain::Zig`: `zig c++` / `zig cc` / `zig ar`, clang flag dialect, no
  `stdlib()`, no compiler-cache launcher.
- Static options (`static_link`, `static_deps`, `static_stdlib`) are validated
  per target triple.

## Phase 2 — host vs target targets

A cross build still needs **host** tools: code generators, embedders, protoc
plugins run *during* the build. Proposal:

```cpp
auto& gen = proj.add_executable("gen").sources("tools/gen.cpp").for_host();
proj.add_custom_command(...).uses(gen);   // runs on the build machine
```

- `for_host()` pins the target to `host_triple()` (ignores `Config::target`).
- Validation: a target-triple artifact may not `depends_on()` a host library and
  vice versa (different ABI) — Config error.
- `test`/`run` keep working for host targets regardless of the project target.

**Phase 2 tail shipped — `add_custom_command(...).uses(host_tool)`:** a custom
command always *executes* on the build host (that is the point of a host tool),
even in a cross project:

```cpp
auto& gen = proj.add_executable("gen").sources("tools/gen.cpp").for_host();
auto& out = proj.add_custom_command("generated")
                .command({"$tool", "-o", "$out", "input.txt"}) // $tool/$out/$in expand at emit time
                .uses(gen)                                     // explicit edge input: built first, re-runs on tool change
                .outputs({"build/gen/out.h"});                 // default: build/<mode>/gen/<name>/output
proj.add_executable("app").sources("app/main.cpp").depends_on(out);
// app's compile edges get out.h as an implicit input; its dir propagates as -I
```

Cross hygiene: the command itself follows the project triple unless it is
`.for_host()` itself, and the usual host/target `depends_on()` mismatch check
applies to it (a target-triple consumer may not depend on a for_host custom
command). The `uses()` tool is exempt from that check (it is executed, not
linked) but must be runnable on the host — in a cross project it has to be
`.for_host()` or the build fails with a Config error.

## Phase 3 — CLI selection and target matrices

**Status: shipped** (per-triple subgraphs: `proj.targets({{"name", Config{}}, ...})`
resolves and emits one subgraph per triple into the same manifest, with
per-subgraph runners and `--for` / compound `--target` selection).

- `proj.targets({{"gnu217", Config{}.toolchain(Toolchain::Zig).target("x86_64-linux-gnu.2.17")},
                 {"musl",  Config{}.toolchain(Toolchain::Zig).target("aarch64-linux-musl")},
                 {"win",   Config{}.toolchain(Toolchain::Zig).target("x86_64-w64-mingw32")}})`
  emits one subgraph per triple into the same manifest, so samurai builds the
  whole matrix in parallel with a single invocation (`all` = every triple).
- Cross tests: `Config::runner("qemu-aarch64 -L <sysroot>")` / `"wine"`
  prefixes `test`/`run` execution (split on spaces); without a runner, cross
  test executables are built but skipped with a warning (never silently
  "passed"), and `run` on a cross exe is a Config error.

## Phase 4 — packages follow the target

- `fetch` / `add_subdirectory`: source packages, already compiled per triple.
- `vcpkg`: today it maps `host_triple()` to a triplet — must use the **resolved
  project triple** (`arm64-linux`, `x64-mingw-static`, `arm64-osx`, ...) and
  `VCPKG_DEFAULT_HOST_TRIPLET` for host tools.
- `conan`: generate a host profile (`-pr:h`) from the triple (os, arch,
  compiler, libcxx) and keep the build profile (`-pr:b`) native.
- `Config::sysroot(path)`: emitted as `--sysroot=` on compile + link for clang,
  and used by `fs::find_library` instead of host paths.

## Phase 5 — portable releases (truss itself)

`static_stdlib(Gcc)` removes the C++ runtime dependency, but the binary still
requires the build machine's glibc version (symbol versions such as
`GLIBC_2.38`). The portable recipe for `truss-dist`:

```cpp
// static-link truss's OWN dependencies, keep libc++ dynamic (fully static not required):
proj.configure(Config().toolchain(Toolchain::Zig).target("x86_64-linux-musl"));
dist.static_deps();
// glibc-portable alternative (runs on any glibc >= 2.17):
proj.configure(Config().toolchain(Toolchain::Zig).target("x86_64-linux-gnu.2.17"));
dist.static_deps().static_stdlib(StaticStdlib::Llvm);
```

Musl/zig/cross-compilation support in the tool is done and verified; shipping truss
itself as a portable binary (this section) is a secondary follow-up.

**Status: BLOCKED via modules; the amalgam path is closer but has two concrete
blockers (verified 2026-11-02 with zig 0.17.0).** truss's own sources are `.cppm`
modules (`import truss;`), and zig cannot build C++20 modules (see Known gaps), so
the direct build is out. The single-header amalgamation (`examples/amalgam`, non-module)
sidesteps modules and gets much further:

- First catch RESOLVED: truss's C++ *does* compile against zig's bundled libc++ 22.
  `zig c++ -std=c++26 -target x86_64-linux-musl -Ibuild/dist -Ivendor
  examples/amalgam/truss.cpp` compiles and links a fully-static binary cleanly.
- Blocker A (runtime, zig-musl only): `truss-samu: reallocarray: No such file or
  directory` is raised only when RUNNING a truss built with ZIG for a musl target.
  It is a quirk of zig's bundled musl, not musl in general. Measured: zig-musl
  `realloc(NULL,0)` and `realloc(p,0)` both return NULL; real musl
  (`x86_64-linux-musl-gcc -static`) both non-null; glibc `realloc(NULL,0)` non-null,
  `realloc(p,0)` NULL. gdb backtrace: samurai's `depsinit` (deps.c) calls
  `xreallocarray(NULL, 0, 32)` when `.ninja_deps` is empty (fresh build dir);
  zig-musl returns NULL and `xreallocarray` treats any NULL as `fatal()`. The
  "No such file or directory" is a stale errno, not from realloc. Does not
  reproduce on a real musl system (Alpine / musl-gcc).
  Toolchain reality on this box: `musl-clang` and `x86_64-linux-musl-gcc` are
  C-only (no C++ headers) and there is no musl `g++`; `zig c++ -target
  x86_64-linux-musl` is the only working musl C++ toolchain.
  PENDING (not yet applied): a one-line sanctioned truss patch to
  `vendor/samurai/util.c` `xreallocarray`: only `fatal` when a non-zero-size
  allocation fails (`if (!p && n && m) fatal(...)`), accepting NULL for a
  zero-size request. Valid C and C++, inert on glibc/real musl; follows the
  truss-patch precedent in `vendor/samurai/TRUSS-VENDOR.txt`.
- Blocker B (gnu.2.17, link): truss calls `posix_spawn_file_actions_addchdir_np`
  (`run_impl`, proc.cpp), added in glibc 2.29. This only affects a glibc-floor
  artifact below 2.29 (undefined symbol at link) and is irrelevant to a musl
  build. For the glibc-portable path, raise the floor (e.g. `gnu.2.29`) or add a
  `chdir`-in-child fallback.

Two catches to resolve first:

- truss's own sources must build with zig's libc++ (the bootstrap already uses
  libc++, so this is expected to work) and samurai with zig's libc.
- `truss-dist` and the installed BMIs are independent: `truss init` only writes
  files, the BMIs/`libtruss.a` a user compiles against are still built on their
  machine (BMIs are not portable across compiler builds).

Release matrix suggestion: `x86_64-linux-musl`, `aarch64-linux-musl` (static,
any distro), `x86_64-windows-gnu`, `aarch64-macos` (built on a macOS runner —
needs the SDK for frameworks).

## Known gaps

- zig cannot build C++20 named modules at all (zig 0.17.0). `zig c++` does not
  treat module-interface extensions (.cppm/.ixx/...) as C++, so the compile gets
  the host's system headers and clang's default triple instead of zig's libc++.
  Forcing `-x c++` restores headers but clang then emits no BMI, and forcing the
  BMI out fails because zig's build cache discards it. truss raises a Config
  error for `Toolchain::Zig` + `.cppm` sources, pointing to `Toolchain::LLVM`.
- `ModulesMode::Manual` with zig is an upfront Config error: `clang-scan-deps`
  cannot run the two-token `zig c++` command line. `Auto` does not work either.
- zig cannot build precompiled headers: `zig c++ -x c++-header` sends the output
  to the linker ("unknown file type"). `pch()` with `Toolchain::Zig` is a Config
  error.
- Sanitizers on cross targets depend on the sysroot shipping the runtimes
  (zig ships none).
- Windows *hosts* (truss itself running on Windows) are a separate roadmap item:
  `proc.cpp` is POSIX-only.
