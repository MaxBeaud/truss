// cross-host — example: a zig cross build (aarch64-linux-musl) with a host-side
// codegen tool pinned to the build machine via for_host().
//
// Compile (from the repo root):  ./setup.sh examples   (needs zig on PATH)
// Run (from examples/cross-host): ./truss run --target gen   (host tool)
//                                  ./truss test               (cross tests build, then skip)
import truss;

int main(int argc, char** argv) {
    truss::Project proj("cross-host", argc, argv);

    // The whole project crosses to aarch64-linux-musl through zig's bundled
    // sysroot: no cross toolchain to install.
    proj.configure(truss::Config().toolchain(truss::Toolchain::Zig).target("aarch64-linux-musl"));

    // Host-side codegen tool: for_host() pins it back onto the build machine
    // (host triple + the Config's compiler overlays), so it can run at build
    // time even while everything else crosses.
    auto& hostcore = proj.add_static_library("hostcore")
                         .include_dirs("tools/include")
                         .sources_in("tools/hostcore")
                         .for_host();
    auto& gen = proj.add_executable("gen")
                    .sources_in("tools/gen")
                    .include_dirs("tools/include")
                    .for_host()
                    .depends_on(hostcore); /* host-to-host edge: fine (deref-free) */

    // The cross app: built for aarch64-linux-musl, lands in
    // build/<mode>/bin/arm64-linux-musl-zig/.
    proj.add_executable("app")
        .sources_in("src/app");

    // A host test runs on the build machine under `./truss test` ...
    proj.add_executable("test_gen")
        .sources_in("tests")
        .include_dirs("tools/include")
        .for_host()
        .depends_on(hostcore)
        .is_test();

    // ... while a cross test is built for the target triple and then skipped
    // with a warning (a cross exe cannot run on the host — never silently
    // "passed").
    proj.add_executable("test_app")
        .sources_in("cross_tests")
        .is_test();

    auto result = proj.build();
    return result ? 0 : 1;
}
