# Cross-compilation strategy

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
| **zig** (`Toolchain::Zig` + `target()`) | bundled for every target (glibc versions, musl, MinGW, macOS libSystem stubs) | zero setup; glibc version pinning (`x86_64-linux-gnu.2.17`); static libc++ by default | no ASan/UBSan runtimes; no clang-scan-deps (`ModulesMode::Manual`); macOS frameworks not bundled |
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

## Phase 3 — CLI selection and target matrices

- `./truss release --cross aarch64-linux-musl` overrides `Config::target` without
  editing the script (outputs already land in their own `triple_dir`).
- `proj.targets({"x86_64-linux-gnu.2.17", "aarch64-linux-musl", "x86_64-windows-gnu"})`
  emits one subgraph per triple into the same manifest, so samurai builds the
  whole matrix in parallel with a single invocation (`all` = every triple).
- Cross tests: `Config::runner("qemu-aarch64 -L <sysroot>")` / `"wine"`
  prefixes `test`/`run` execution; without a runner, cross test executables are
  built but skipped with a warning (never silently "passed").

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
proj.configure(Config().toolchain(Toolchain::Zig).target("x86_64-linux-gnu.2.17"));
dist.static_deps().static_stdlib(StaticStdlib::Llvm);   // runs on any glibc >= 2.17
// or fully static, no glibc at all:
proj.configure(Config().toolchain(Toolchain::Zig).target("x86_64-linux-musl"));
dist.static_deps().static_stdlib(StaticStdlib::Musl);
```

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

- `ModulesMode::Manual` uses `clang-scan-deps`, which cannot run a `zig c++`
  command line: use `Auto` with zig.
- Sanitizers on cross targets depend on the sysroot shipping the runtimes
  (zig ships none).
- Windows *hosts* (truss itself running on Windows) are a separate roadmap item:
  `proc.cpp` is POSIX-only.
