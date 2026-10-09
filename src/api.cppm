/* truss public types partition.
 *
 * Re-exported by the primary interface (src/truss.cppm), so consumers only
 * ever write `import truss;`. Kept as a partition because module units of
 * `truss` cannot import the primary interface of their own module.
 */
module;
#include <concepts>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module truss:api;

export namespace truss {

inline constexpr std::string_view kVersion = "0.1.0";

enum class Os { Unknown = 0, Linux, Windows, MacOS, FreeBSD, Android, Emscripten };
enum class Arch { Unknown = 0, X64, X86, Arm64, Arm32, Riscv64, Ppc64, S390x, Wasm32 };
enum class CompilerFamily { Unknown = 0, Gcc, Clang, Msvc, Tcc };
enum class BuildMode { Debug, Release };

enum class CppStandard { Default = 0, Cpp11, Cpp14, Cpp17, Cpp20, Cpp23, Cpp26 };
enum class Warnings { Default = 0, None, Minimal, All, Pedantic };
enum class DebugInfo { Default = 0, None, Minimal, Medium, Full };
enum class Optimization { Default = 0, None, O1, O2, Size, O3 };
enum class Sanitizer { Default = 0, None, Address, Ub, All };
enum class Lto { Default = 0, None, Full, Thin };
/* Compiler/linker hardening tiers. Default resolves to Light (truss's default
 * hardening stack); None opts out explicitly. */
enum class Hardening { Default = 0, None, Light, Medium, Max };

/* How .cppm sources in a user target get compiled and wired.
 * Auto (the default): conservative v0.1 mode — every .cppm becomes a BMI edge and
 * every other unit in the target (and its deps) imports all of them.
 * Manual: precise per-file BMI wiring — not implemented yet. */
enum class ModulesMode { Default = 0, Off, Auto, Manual };

/* C++ standard library selection (emitted as -stdlib= on compile + link). */
enum class StdLib { Default = 0, Libstdcxx, Libcxx };
/* Named toolchain bundles: expands to compiler + stdlib + archiver. */
enum class Toolchain { Default = 0, LLVM, GCC };

/* fetch() sources. Exactly one pair must be set:
 *  - git + tag  : shallow clone pinned to a tag/branch/commit
 *  - url + sha256: tarball (.tar.gz/.tar.xz) verified against the hash */
struct FetchOptions {
    std::string git;
    std::string tag;
    std::string url;
    std::string sha256;
};

/* A fetched dependency materialized under build/<mode>/packages/<name>. */
struct Package {
    std::string name;
    std::filesystem::path dir;
};

enum class TargetKind { Executable, StaticLibrary, SharedLibrary, Interface };

/* A range whose elements can name a string (vector<string>, initializer_list
 * of const char*, spans...). Deliberately excludes a bare char literal/array
 * so `include_dirs("include")` picks the single-string overload instead. */
template <class R>
concept StringRange = std::ranges::range<R> &&
                      std::convertible_to<std::ranges::range_value_t<R>, std::string_view>;

struct Error {
    enum class Kind { Io, Config, Command, Fetch, Toolchain, Internal };
    Kind kind {Kind::Internal};
    std::string message;
    std::optional<std::filesystem::path> path {};
};

template <class T>
using Result = std::expected<T, Error>;

/* A compiler target: host by default, cross-compilable by override. */
struct TargetTriple {
    Os os {Os::Unknown};
    Arch arch {Arch::Unknown};
    CompilerFamily family {CompilerFamily::Unknown};
    std::string compiler; /* compiler driver, e.g. "clang++" */
    std::string ar;       /* archiver, e.g. "llvm-ar" */
    std::string linker;   /* link driver override ("" = compiler default), passed via -fuse-ld= */
    StdLib stdlib {StdLib::Default}; /* ""-equivalent: compiler's default stdlib */
};

/* Triple of the machine truss itself runs on. */
TargetTriple host_triple();

/* ---------------------------------------------------------------------------
 * Config: project-wide toolchain selection, applied with proj.configure(conf).
 * compiler()/linker() take a program name or a path; the compiler family is
 * detected from the basename (clang++, g++, clang-cl, cl.exe, ...).
 * toolchain() expands to compiler + stdlib + archiver in one call; later
 * explicit calls override it (last call wins).
 * ------------------------------------------------------------------------- */
class Config {
public:
    Config& compiler(std::string name_or_path) { compiler_ = std::move(name_or_path); return *this; }
    Config& linker(std::string name_or_path) { linker_ = std::move(name_or_path); return *this; }
    Config& ar(std::string name_or_path) { ar_ = std::move(name_or_path); return *this; }
    Config& stdlib(StdLib s) { stdlib_ = s; return *this; }

    Config& toolchain(Toolchain t) {
        switch (t) {
        case Toolchain::LLVM:
            compiler_ = "clang++"; ar_ = "llvm-ar"; stdlib_ = StdLib::Libcxx; break;
        case Toolchain::GCC:
            compiler_ = "g++"; ar_ = "ar"; stdlib_ = StdLib::Libstdcxx; break;
        case Toolchain::Default: break;
        }
        return *this;
    }

    const std::string& compiler() const { return compiler_; }
    const std::string& linker() const { return linker_; }
    const std::string& ar() const { return ar_; }
    StdLib stdlib() const { return stdlib_; }

private:
    std::string compiler_, linker_, ar_;
    StdLib stdlib_ {StdLib::Default};
};

/* ---------------------------------------------------------------------------
 * Targets: options holders built with method chaining or pipe composition.
 * ------------------------------------------------------------------------- */
class Target {
public:
    Target(std::string name, TargetKind kind) : name_(std::move(name)), kind_(kind) {}
    virtual ~Target() = default;

    Target(const Target&) = delete;
    Target& operator=(const Target&) = delete;

    // --- sources: glob patterns relative to the project root.
    // `*` matches within one path component, `**` across components,
    // `?` one char, `{a,b}` alternation. e.g. "src/**.cpp", "src/*.c".
    Target& sources(std::string_view pattern) {
        source_patterns_.emplace_back(pattern);
        return *this;
    }
    Target& sources(std::initializer_list<std::string_view> patterns) {
        for (const auto& p : patterns)
            source_patterns_.emplace_back(p);
        return *this;
    }
    template <StringRange R>
    Target& sources(R&& patterns) {
        for (const auto& p : patterns)
            source_patterns_.emplace_back(std::string_view(p));
        return *this;
    }
    /* every .cpp/.cc/.c under `dir`, recursively. */
    Target& sources_in(std::string_view dir);
    /* exclude a previously matched file (exact resolved path). */
    Target& remove_source(std::string_view path);

    /* --- usage requirements --- */
    Target& include_dirs(std::string_view dir) {
        include_dirs_.emplace_back(dir);
        return *this;
    }
    Target& include_dirs(std::initializer_list<std::string_view> dirs) {
        for (const auto& d : dirs)
            include_dirs_.emplace_back(d);
        return *this;
    }
    template <StringRange R>
    Target& include_dirs(R&& dirs) {
        for (const auto& d : dirs)
            include_dirs_.emplace_back(std::string_view(d));
        return *this;
    }
    Target& defines(std::string_view def) {
        defines_.emplace_back(def);
        return *this;
    }
    Target& defines(std::initializer_list<std::string_view> defs) {
        for (const auto& d : defs)
            defines_.emplace_back(d);
        return *this;
    }
    template <StringRange R>
    Target& defines(R&& defs) {
        for (const auto& d : defs)
            defines_.emplace_back(std::string_view(d));
        return *this;
    }
    Target& link_libs(std::string_view lib) {
        link_libs_.emplace_back(lib);
        return *this;
    }
    Target& link_libs(std::initializer_list<std::string_view> libs) {
        for (const auto& l : libs)
            link_libs_.emplace_back(l);
        return *this;
    }
    template <StringRange R>
    Target& link_libs(R&& libs) {
        for (const auto& l : libs)
            link_libs_.emplace_back(std::string_view(l));
        return *this;
    }
    Target& link_paths(std::string_view path) {
        link_paths_.emplace_back(path);
        return *this;
    }
    Target& link_paths(std::initializer_list<std::string_view> paths) {
        for (const auto& p : paths)
            link_paths_.emplace_back(p);
        return *this;
    }
    template <StringRange R>
    Target& link_paths(R&& paths) {
        for (const auto& p : paths)
            link_paths_.emplace_back(std::string_view(p));
        return *this;
    }
    Target& link_frameworks(std::string_view fw) {
        frameworks_.emplace_back(fw);
        return *this;
    }
    Target& link_frameworks(std::initializer_list<std::string_view> fw) {
        for (const auto& f : fw)
            frameworks_.emplace_back(f);
        return *this;
    }
    template <StringRange R>
    Target& link_frameworks(R&& fw) {
        for (const auto& f : fw)
            frameworks_.emplace_back(std::string_view(f));
        return *this;
    }
    Target& framework_paths(std::string_view path) {
        framework_paths_.emplace_back(path);
        return *this;
    }
    Target& framework_paths(std::initializer_list<std::string_view> paths) {
        for (const auto& p : paths)
            framework_paths_.emplace_back(p);
        return *this;
    }
    template <StringRange R>
    Target& framework_paths(R&& paths) {
        for (const auto& p : paths)
            framework_paths_.emplace_back(std::string_view(p));
        return *this;
    }

    /* raw escape hatches (verbatim, compiler-specific) */
    Target& flags(std::string_view raw) { flags_ = std::string(raw); return *this; }
    Target& linker_flags(std::string_view raw) { linker_flags_ = std::string(raw); return *this; }

    /* --- options --- */
    Target& cpp_standard(CppStandard s) { std_ = s; return *this; }
    Target& warnings(Warnings w) { warnings_ = w; return *this; }
    Target& debug_info(DebugInfo d) { debug_ = d; return *this; }
    Target& optimization(Optimization o) { optimization_ = o; return *this; }
    Target& sanitizer(Sanitizer s) { sanitizer_ = s; return *this; }
    Target& lto(Lto l) { lto_ = l; return *this; }
    Target& harden(Hardening h) { hardening_ = h; return *this; }
    Target& modules_mode(ModulesMode m) { modules_ = m; return *this; }
    Target& pic(bool v) { pic_ = v; return *this; }
    Target& cross_target(TargetTriple t) { triple_ = t; return *this; }
    Target& depends_on(const Target& dep);
    Target& is_test(bool v = true) { test_ = v; return *this; }

    /* --- read access (used by the build engine) --- */
    const std::string& name() const { return name_; }
    TargetKind kind() const { return kind_; }
    const std::vector<std::string>& source_patterns() const { return source_patterns_; }
    const std::vector<std::string>& removed_sources() const { return removed_sources_; }
    const std::vector<std::string>& include_dirs() const { return include_dirs_; }
    const std::vector<std::string>& defines() const { return defines_; }
    const std::vector<std::string>& link_libs() const { return link_libs_; }
    const std::vector<std::string>& link_paths() const { return link_paths_; }
    const std::vector<std::string>& frameworks() const { return frameworks_; }
    const std::vector<std::string>& framework_paths() const { return framework_paths_; }
    const std::string& flags() const { return flags_; }
    const std::string& linker_flags() const { return linker_flags_; }
    CppStandard cpp_standard() const { return std_; }
    Warnings warnings() const { return warnings_; }
    DebugInfo debug_info() const { return debug_; }
    Optimization optimization() const { return optimization_; }
    Sanitizer sanitizer() const { return sanitizer_; }
    Lto lto() const { return lto_; }
    Hardening hardening() const { return hardening_; }
    ModulesMode modules_mode() const { return modules_; }
    bool pic() const { return pic_; }
    const std::optional<TargetTriple>& triple_override() const { return triple_; }
    const std::vector<const Target*>& deps() const { return deps_; }
    bool is_test_target() const { return test_; }

private:
    std::string name_;
    TargetKind kind_;
    std::vector<std::string> source_patterns_;
    std::vector<std::string> removed_sources_;
    std::vector<std::string> include_dirs_, defines_, link_libs_, link_paths_;
    std::vector<std::string> frameworks_, framework_paths_;
    std::string flags_, linker_flags_;
    CppStandard std_ {};
    Warnings warnings_ {};
    DebugInfo debug_ {};
    Optimization optimization_ {};
    Sanitizer sanitizer_ {};
    Lto lto_ {};
    Hardening hardening_ {};
    ModulesMode modules_ {};
    bool pic_ {false};
    bool test_ {false};
    std::optional<TargetTriple> triple_;
    std::vector<const Target*> deps_;
};

class Executable : public Target {
public:
    explicit Executable(std::string name) : Target(std::move(name), TargetKind::Executable) {}
};
class StaticLib : public Target {
public:
    explicit StaticLib(std::string name) : Target(std::move(name), TargetKind::StaticLibrary) {}
};
class SharedLib : public Target {
public:
    explicit SharedLib(std::string name) : Target(std::move(name), TargetKind::SharedLibrary) {}
};
/* Header-only: no compiled artifact, only usage requirements (include dirs,
 * defines) propagated to targets that depends_on() it. */
class Interface : public Target {
public:
    explicit Interface(std::string name) : Target(std::move(name), TargetKind::Interface) {}
};

/* Non-owning handle to a target created by add_subdirectory()/conan()/vcpkg().
 * std::expected cannot hold T&, so this is the reference-shaped stand-in:
 * never null, dereferenceable, and converts implicitly to Target& —
 * `depends_on(*proj.conan({"zlib/1.3.1"}))` just works. */
class TargetRef {
public:
    explicit TargetRef(Target& t) : target_(&t) {}
    Target& operator*() const { return *target_; }
    Target* operator->() const { return target_; }
    operator Target&() const { return *target_; }

private:
    Target* target_;
};

/* ---------------------------------------------------------------------------
 * Tasks: `./truss <name>` dispatch. Built-ins: test, run, clean, compdb.
 * ------------------------------------------------------------------------- */
struct TaskContext {
    std::string project;
    BuildMode mode {BuildMode::Debug};
    std::filesystem::path build_dir; /* <root>/build/<mode> (or override) */
    std::filesystem::path cwd;
    int jobs {0};
    bool verbose {false};
};
using TaskFn = std::function<Result<void>(const TaskContext&)>;

/* ---------------------------------------------------------------------------
 * Project: owns targets, parses the CLI, generates + runs the build.
 * ------------------------------------------------------------------------- */
class Project {
public:
    Project(std::string name, std::span<char* const> argv);
    Project(std::string name, int argc, char** argv);

    Executable& add_executable(std::string name);
    StaticLib& add_static_library(std::string name);
    SharedLib& add_shared_library(std::string name);
    Interface& add_interface_library(std::string name);
    void task(std::string name, TaskFn fn);

    /* Build hooks, run in registration order: pre_build fires once before the
     * first compile step, post_build once after the first successful compile
     * step (before test/run binaries execute). A failing hook aborts the
     * build. clean/compdb never fire hooks. */
    Project& pre_build(TaskFn fn) { pre_build_.push_back(std::move(fn)); return *this; }
    Project& post_build(TaskFn fn) { post_build_.push_back(std::move(fn)); return *this; }

    /* Project-wide toolchain selection (compiler/linker/stdlib/archiver).
     * Per-target cross_target() still overrides it completely. */
    Project& configure(const Config& conf) { config_ = conf; return *this; }
    const Config& config() const { return config_; }

    /* --- packages (phase 5) ------------------------------------------------
     * fetch(): git (shallow clone pinned to tag) or tarball (sha256-verified),
     * cached in ~/.cache/truss, materialized into build/{mode}/packages/{name},
     * pinned in ./truss.lock. Call before build(). */
    Result<Package> fetch(std::string_view name, const FetchOptions& opts);

    /* add_subdirectory(): convention-based vendored project — a static library
     * named after the directory, sources from its src/ tree, includes from its
     * include/ dir when present. Returns the target for depends_on(). */
    Result<TargetRef> add_subdirectory(const std::filesystem::path& dir);
    Result<TargetRef> add_subdirectory(const Package& pkg) { return add_subdirectory(pkg.dir); }

    /* conan 2: runs `conan install` on the conanfile (path to conanfile.txt/.py
     * or its directory), or generates one from versioned refs —
     * conan({"zlib/1.3.1", "fmt/11.0.2"}). Returns an Interface target carrying
     * the whole graph's includes/libs/defines — depends_on() it. */
    Result<TargetRef> conan(std::string_view conanfile = ".");
    Result<TargetRef> conan(std::initializer_list<std::string_view> refs);

    /* vcpkg manifest mode: uses the project's vcpkg.json, or generates one from
     * `packages`. Installs under build/{mode}/vcpkg_installed and returns an
     * Interface target for depends_on(). */
    Result<TargetRef> vcpkg(std::vector<std::string> packages);

    /* Generates the ninja manifest, runs samurai, then the requested tasks.
     * Errors are logged and returned. */
    Result<void> build();

    const std::string& name() const { return name_; }
    BuildMode mode() const { return mode_; }
    const std::filesystem::path& cwd() const { return cwd_; }
    std::filesystem::path build_dir() const; /* build root + mode subdir */
    int jobs() const { return jobs_; }
    bool verbose() const { return verbose_; }
    const std::vector<std::string>& only_targets() const { return only_targets_; }

private:
    std::string name_;
    std::filesystem::path cwd_;
    std::vector<std::unique_ptr<Target>> targets_;
    std::vector<std::pair<std::string, TaskFn>> user_tasks_;
    std::vector<TaskFn> pre_build_, post_build_;
    Config config_;

    /* parsed CLI state */
    BuildMode mode_ {BuildMode::Debug};
    int jobs_ {0};
    bool verbose_ {false};
    bool dry_run_ {false};
    bool explain_ {false};
    std::vector<std::string> requested_tasks_;
    std::vector<std::string> only_targets_;
    std::optional<std::filesystem::path> build_root_;
    std::optional<Error> cli_error_;
    std::vector<std::string> subdir_target_names_; /* created by add_subdirectory */
};

} // namespace truss
