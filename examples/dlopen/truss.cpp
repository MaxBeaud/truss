// dlopen — a shared library built as a .so but deliberately NOT linked into
// the executable: the host opens it at runtime with dlopen(3).
//
// Run (from examples/dlopen): ./truss run
import truss;

int main(int argc, char** argv) {
    truss::Project proj("plugin", argc, argv);
    proj.configure(truss::Config{}.toolchain(truss::Toolchain::LLVM));

    // The host: needs libdl and its own ABI header; `run` starts this one even
    // though the project has more than one artifact.
    proj.add_executable("host")
        .sources("src/main.cpp")
        .include_dirs("include")
        .link_libs("dl")
        .default_target();

    // The runtime-loaded library: same ABI header, only explicitly exported
    // symbols visible, and never a link dependency (not_linkable() rejects
    // depends_on()).
    proj.add_shared_library("plugin_math")
        .sources("plugins/math/plugin.cpp")
        .include_dirs("include")
        .symbols_hidden()
        .not_linkable();

    return proj.build() ? 0 : 1;
}
