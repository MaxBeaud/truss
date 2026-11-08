# cross-host — host tools inside a zig cross build

The whole project crosses to `aarch64-linux-musl` through zig (bundled sysroot,
no cross toolchain to install); the host-side codegen tool is pinned back to the
build machine with `for_host()`:

```cpp
proj.configure(truss::Config().toolchain(truss::Toolchain::Zig).target("aarch64-linux-musl"));

auto& gen = proj.add_executable("gen").sources_in("tools/gen").for_host();
proj.add_executable("app").sources_in("src/app");   // -> bin/arm64-linux-musl-zig/app
```

- `for_host()` resolves the target to the host triple while keeping the Config's
  compiler/stdlib/ar overlays, so the tool builds with the same toolchain but
  runs on the build machine.
- **Mixing host and cross deps is a Config error**: a `depends_on()` edge must
  stay on one side of the host/target boundary (different ABI and C library).
  `gen.for_host().depends_on(cross_lib)` — or a cross app depending on a
  `for_host()` library — fails at build time with:

  ```text
  a dependency must be built for the same machine as its consumer
  (different ABI and C library — the link would be broken anyway).
  Mark host-side tools with .for_host(), or drop the edge.
  ```

  Host-to-host edges (`gen.for_host().depends_on(hostcore)`) are fine.
- **`./truss test` builds but skips cross tests with a warning**: a
  cross-compiled test exe cannot run on the host, so it is built and then
  skipped — never silently counted as passed:

  ```text
  test '.../test_app' skipped (cross target 'aarch64-linux-musl')
  1 cross-compiled test(s) skipped (no runner configured)
  ```

Run it (needs `zig` on PATH):

```sh
./truss run --target gen   # build + run the host tool on the build machine
./truss test               # host test runs; cross test builds, then skips (warning)
```
