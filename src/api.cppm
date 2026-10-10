/**
 * \file api.cppm
 * \brief truss public types partition (`import truss;` is all consumers need).
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

/// Target operating system.
enum class Os {
    Unknown = 0, ///< not detected / not recognized
    Linux, Windows, MacOS, FreeBSD, Android, Emscripten, ///< supported platforms
};
/// Target instruction set.
enum class Arch {
    Unknown = 0, ///< not detected / not recognized
    X64, X86, Arm64, Arm32, Riscv64, Ppc64, S390x, Wasm32, ///< supported ISAs
};
/// Compiler driver dialect (flag syntax family).
enum class CompilerFamily {
    Unknown = 0,   ///< unrecognized driver (clear error at build time)
    Gcc, Clang,    ///< GNU and clang driver flag syntax
    Msvc,          ///< cl.exe / clang-cl syntax (validated with clang-cl)
    Tcc,           ///< detected but not supported by truss
};
/// Debug or release; `Default` option values resolve per mode.
enum class BuildMode { Debug, Release };

/// C++ standard (`Default` = C++23).
enum class CppStandard { Default = 0, Cpp11, Cpp14, Cpp17, Cpp20, Cpp23, Cpp26 };
/// Warning level (`Default` = -Wall -Wextra).
enum class Warnings {
    Default = 0, ///< -Wall -Wextra
    None,        ///< -w
    Minimal,     ///< -Wall
    All,         ///< -Wall -Wextra
    Pedantic,    ///< -Wall -Wextra -Wpedantic
};
/// Debug symbol level (`Default` resolves per mode).
enum class DebugInfo {
    Default = 0, ///< Full in debug, None in release
    None,        ///< -g0
    Minimal,     ///< -g1
    Medium,      ///< -g2
    Full,        ///< -g3 (MSVC: /Z7, embedded — no PDB races)
};
/// Optimization level.
enum class Optimization {
    Default = 0, ///< -O0 in debug, -O2 in release
    None,        ///< -O0
    O1, O2,      ///< -O1 / -O2
    Size,        ///< -Os
    O3,          ///< -O3 (MSVC: /O2)
};
/// Sanitizer set.
enum class Sanitizer {
    Default = 0, ///< off
    None,        ///< explicitly off
    Address,     ///< -fsanitize=address (+ -fno-omit-frame-pointer)
    Ub,          ///< -fsanitize=undefined
    All,         ///< address + undefined
};
/// Link-time optimization.
enum class Lto {
    Default = 0, ///< off
    None,        ///< explicitly off
    Full,        ///< -flto=full (MSVC: /GL + /LTCG)
    Thin,        ///< -flto=thin (no MSVC equivalent: maps to /GL)
};
/**
 * \brief Compiler/linker hardening tiers.
 * `Default` resolves to Light (truss's default hardening stack); None opts out.
 * See README "Hardening" for the per-tier flag tables.
 */
enum class Hardening {
    Default = 0, ///< resolves to Light
    None,        ///< no hardening flags
    Light,       ///< stack protector + hardened link (relro/full-bind/no-exec stack)
    Medium,      ///< Light + pattern-filled uninit locals, CFI, stack-clash (x86-64), libc++ hardened mode
    Max,         ///< Medium + libc++ debug mode + libstdc++ assertions
};

/**
 * \brief How .cppm sources in a user target get compiled and wired.
 * Auto (the default): BMI edges for every .cppm; consumers are wired from a
 * fast regex scan of each file's import declarations (heuristic: can miss
 * macro-generated imports or false-positive on `import` in comments).
 * Manual: same wiring but the import lists come from clang-scan-deps
 * (p1689) at configure time — authoritative, at ~10 ms per TU; clang only.
 */
enum class ModulesMode {
    Default = 0, ///< resolves to Auto
    Off,         ///< no BMI wiring (.cppm sources become a Config error)
    Auto,        ///< BMI edges + regex-scanned import wiring (heuristic, fast)
    Manual,      ///< import lists from clang-scan-deps (authoritative, ~10 ms/TU, clang only)
};

/// C++ standard library selection (emitted as -stdlib= on compile + link).
enum class StdLib {
    Default = 0, ///< compiler's default stdlib (no -stdlib= emitted)
    Libstdcxx,   ///< -stdlib=libstdc++ (clang only)
    Libcxx,      ///< -stdlib=libc++ (clang only; g++ + libc++ is a Config error)
};
/// Named toolchain bundles: expands to compiler + stdlib + archiver.
enum class Toolchain {
    Default = 0, ///< host defaults (no override)
    LLVM,        ///< clang++ + libc++ + llvm-ar
    GCC,         ///< g++ + libstdc++ + ar
};

/**
 * \brief fetch() sources. Exactly one pair must be set:
 *  - git + tag   : shallow clone pinned to a tag/branch/commit
 *  - url + sha256: tarball (.tar.gz/.tar.xz) verified against the hash
 */
struct FetchOptions {
    std::string git;    ///< repository URL
    std::string tag;    ///< tag / branch / commit to pin
    std::string url;    ///< tarball URL
    std::string sha256; ///< expected tarball hash
};

/// A fetched dependency materialized under `build/\<mode\>/packages/\<name\>`.
struct Package {
    std::string name;           ///< package name as requested
    std::filesystem::path dir;  ///< materialized directory (feed to add_subdirectory)
};

/// Artifact kind of a Target.
enum class TargetKind {
    Executable,     ///< linked binary under build/{mode}/bin/
    StaticLibrary,  ///< .a / .lib under build/{mode}/lib/
    SharedLibrary,  ///< .so / .dll under build/{mode}/lib/
    Interface,      ///< header-only: usage requirements only, no artifact
};

/**
 * \brief A range whose elements can name a string (vector<string>,
 * initializer_list of const char*, spans...). Deliberately excludes a bare
 * char literal/array so `include_dirs("include")` picks the single-string
 * overload instead.
 */
template <class R>
concept StringRange = std::ranges::range<R> &&
                      std::convertible_to<std::ranges::range_value_t<R>, std::string_view>;

/// Error carried by every fallible truss operation (no exceptions).
struct Error {
    /// Error category.
    enum class Kind {
        Io,        ///< filesystem / stream failure
        Config,    ///< bad build-script input (bad name, unknown target, ...)
        Command,   ///< a spawned process failed
        Fetch,     ///< dependency download/verification failed
        Toolchain, ///< required tool missing or unsupported
        Internal,  ///< truss invariant violated (please report)
    };
    Kind kind {Kind::Internal};                   ///< category
    std::string message;                          ///< human-readable, multi-line OK
    std::optional<std::filesystem::path> path {}; ///< offending file/dir when known
};

/// Result of a fallible operation: a value or an Error.
template <class T>
using Result = std::expected<T, Error>;

/// A compiler target: host by default, cross-compilable by override.
struct TargetTriple {
    Os os {Os::Unknown};                        ///< target OS
    Arch arch {Arch::Unknown};                  ///< target ISA
    CompilerFamily family {CompilerFamily::Unknown}; ///< flag dialect
    std::string compiler;                       ///< compiler driver, e.g. "clang++"
    std::string ar;                             ///< archiver, e.g. "llvm-ar"
    std::string linker;                         ///< -fuse-ld= override ("" = driver default)
    StdLib stdlib {StdLib::Default};            ///< stdlib selection (Default = compiler default)
};

/// Triple of the machine truss itself runs on (compiler from $CXX when set).
/// \return os/arch/family detected from predefined macros, plus default
///         driver and archiver (llvm-ar for clang when available).
TargetTriple host_triple();

/**
 * \brief Project-wide toolchain selection, applied with proj.configure(conf).
 *
 * compiler()/linker() take a program name or a path; the compiler family is
 * detected from the basename (clang++, g++, clang-cl, cl.exe, ...).
 * toolchain() expands to compiler + stdlib + archiver in one call; later
 * explicit calls override it (last call wins).
 */
class Config {
public:
    /// Compiler driver (name or path); family is re-detected from the basename.
    Config& compiler(std::string name_or_path) { compiler_ = std::move(name_or_path); return *this; }
    /// Link driver override, passed as -fuse-ld= (e.g. "mold", "lld-link").
    Config& linker(std::string name_or_path) { linker_ = std::move(name_or_path); return *this; }
    /// Archiver override (toolchain() normally picks it).
    Config& ar(std::string name_or_path) { ar_ = std::move(name_or_path); return *this; }
    /// C++ standard library: emitted as -stdlib= on compile + link.
    Config& stdlib(StdLib s) { stdlib_ = s; return *this; }

    /// Compiler cache launcher (e.g. "ccache", "sccache") prefixed to compile
    /// commands; "" disables it. Default: auto-detect ccache then sccache on PATH.
    /// GCC/Clang families only (MSVC compile rules never use a launcher);
    /// module BMI rules are never launched (ccache's module support is unreliable).
    Config& launcher(std::string name) { launcher_ = std::move(name); launcher_set_ = true; return *this; }

    /// Expand a named bundle: LLVM = clang++ + libc++ + llvm-ar, GCC = g++ + libstdc++ + ar.
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
    const std::string& launcher() const { return launcher_; }
    bool launcher_set() const { return launcher_set_; }
    StdLib stdlib() const { return stdlib_; }

private:
    std::string compiler_, linker_, ar_, launcher_;
    bool launcher_set_ {false};
    StdLib stdlib_ {StdLib::Default};
};

/**
 * \brief A build target: options holder built with method chaining.
 *
 * Every builder method returns Target& so calls chain; concrete kinds are
 * Executable, StaticLib, SharedLib and Interface.
 */
class Target {
public:
    Target(std::string name, TargetKind kind) : name_(std::move(name)), kind_(kind) {}
    virtual ~Target() = default;

    Target(const Target&) = delete;
    Target& operator=(const Target&) = delete;

    /// \brief Source glob patterns relative to the project root.
    /// `*` matches within one path component, `**` across components,
    /// `?` one char, `{a,b}` alternation. e.g. "src/**.cpp", "src/*.c".
    /// Overloads: single pattern, braced list, any range of strings.
    /// \param pattern glob, or literal path (which must exist).
    /// \return `*this` for chaining.
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
    /// Every .cpp/.cc/.c under `dir`, recursively.
    Target& sources_in(std::string_view dir);
    /// Exclude a previously matched file (exact resolved path).
    Target& remove_source(std::string_view path);

    /* --- usage requirements --- */
    /// Add an include directory (-I). Overloads: single, braced list, range.
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
    /// Add preprocessor defines (-D). Overloads: single, braced list, range.
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
    /// Add libraries to link (-l). Overloads: single, braced list, range.
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
    /// Add library search directories (-L). Overloads: single, braced list, range.
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
    /// Apple-only: link a framework. Overloads: single, braced list, range.
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
    /// Apple-only: framework search directories (-F). Overloads: single, braced list, range.
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
    /// Raw compiler flags, appended verbatim (repeat calls accumulate).
    Target& flags(std::string_view raw) {
        if (!flags_.empty() && !raw.empty())
            flags_ += ' ';
        flags_ += raw;
        return *this;
    }
    /// Raw linker flags, appended verbatim (repeat calls accumulate).
    Target& linker_flags(std::string_view raw) {
        if (!linker_flags_.empty() && !raw.empty())
            linker_flags_ += ' ';
        linker_flags_ += raw;
        return *this;
    }

    /* --- options --- */
    /// C++ standard (Default = C++23).
    Target& cpp_standard(CppStandard s) { std_ = s; return *this; }
    /// Warning level (Default = -Wall -Wextra).
    Target& warnings(Warnings w) { warnings_ = w; return *this; }
    /// Debug symbol level (Default resolves per mode).
    Target& debug_info(DebugInfo d) { debug_ = d; return *this; }
    /// Optimization level (Default resolves per mode).
    Target& optimization(Optimization o) { optimization_ = o; return *this; }
    /// Sanitizer set (Default = off).
    Target& sanitizer(Sanitizer s) { sanitizer_ = s; return *this; }
    /// Link-time optimization (Default = off).
    Target& lto(Lto l) { lto_ = l; return *this; }
    /// Hardening tier (Default = Light: truss's default hardening stack).
    Target& harden(Hardening h) { hardening_ = h; return *this; }
    /**
     * \brief Precompiled header for this target's C++ sources.
     * clang: `-include-pch`; GCC: `.gch` + `-I`/`-include`. Output lives
     * under build/{mode}/pch/{target}/ and is dependency-tracked. Not yet
     * supported on MSVC targets (clear error); module BMI and C rules never use it.
     * \param path the header, relative to the project root.
     */
    Target& pch(std::string_view path) { pch_ = std::string(path); return *this; }
    /**
     * \brief Unity build: concatenate this target's C/C++ sources into batch
     * TUs (16 per batch) under build/{mode}/unity/{target}/, compiled instead
     * of the originals. Module units (.cppm) are never batched. Faster clean
     * builds; coarser incremental rebuilds.
     * \param v true to enable (default when called with no argument).
     */
    Target& unity(bool v = true) { unity_ = v; return *this; }
    /// .cppm handling mode (Default = Auto).
    Target& modules_mode(ModulesMode m) { modules_ = m; return *this; }
    /// Position-independent code (on by default for libraries).
    Target& pic(bool v) { pic_ = v; return *this; }
    /**
     * \brief Full triple override for this target (cross-compiling); wins
     * over proj.configure() completely (it is not merged with the Config).
     */
    Target& cross_target(TargetTriple t) { triple_ = t; return *this; }
    /**
     * \brief Depend on another target: links its artifact, inherits its
     * usage requirements (include dirs always; defines/link flags from
     * Interface targets). Order is tracked in the ninja graph.
     */
    Target& depends_on(const Target& dep);
    /// Mark as a test executable (built and run by `./truss test`).
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
    const std::string& pch() const { return pch_; }
    bool unity() const { return unity_; }
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
    std::string pch_;
    bool unity_ {false};
    ModulesMode modules_ {};
    bool pic_ {false};
    bool test_ {false};
    std::optional<TargetTriple> triple_;
    std::vector<const Target*> deps_;
};

/// A linkable executable target.
class Executable : public Target {
public:
    explicit Executable(std::string name) : Target(std::move(name), TargetKind::Executable) {}
};
/// A static library target (.a / .lib).
class StaticLib : public Target {
public:
    explicit StaticLib(std::string name) : Target(std::move(name), TargetKind::StaticLibrary) {}
};
/// A shared library target (.so / .dll).
class SharedLib : public Target {
public:
    explicit SharedLib(std::string name) : Target(std::move(name), TargetKind::SharedLibrary) {}
};
/**
 * \brief Header-only target: no compiled artifact, only usage requirements
 * (include dirs, defines) propagated to targets that depends_on() it.
 */
class Interface : public Target {
public:
    explicit Interface(std::string name) : Target(std::move(name), TargetKind::Interface) {}
};

/**
 * \brief Non-owning handle to a target created by add_subdirectory()/conan()/vcpkg().
 * std::expected cannot hold T&, so this is the reference-shaped stand-in:
 * never null, dereferenceable, and converts implicitly to Target& —
 * `depends_on(*proj.conan({"zlib/1.3.1"}))` just works.
 */
class TargetRef {
public:
    explicit TargetRef(Target& t) : target_(&t) {}
    Target& operator*() const { return *target_; }
    Target* operator->() const { return target_; }
    operator Target&() const { return *target_; }

private:
    Target* target_;
};

/// Context handed to tasks and build hooks (the ./truss NAME dispatch).
/// Built-in tasks: test, run, clean, compdb.
struct TaskContext {
    std::string project;                      ///< project name
    BuildMode mode {BuildMode::Debug};        ///< debug or release
    std::filesystem::path build_dir;          ///< `\<root\>/build/\<mode\>` (or override)
    std::filesystem::path cwd;                ///< project root
    int jobs {0};                             ///< parallelism (-j, 0 = auto)
    bool verbose {false};                     ///< -v flag
};
/// Signature of tasks and build hooks.
using TaskFn = std::function<Result<void>(const TaskContext&)>;

/**
 * \brief The build script's entry point: owns targets, parses the CLI,
 * generates the ninja manifest, runs samurai, then the requested tasks.
 *
 * Typical shape: `truss::Project proj("Name", argc, argv); ... ; return proj.build() ? 0 : 1;`
 */
class Project {
public:
    /**
     * \brief Parse truss's CLI from argv (span form).
     * \param name project name (used in messages and generated vcpkg.json)
     * \param argv full process argv; truss parses `debug|release`, tasks,
     *             `-j`, `-v`, `-n`, `--explain`, `--target`, `--build-dir`
     *             (see README "Tasks & flags"); unknown options become a
     *             Config error reported by build().
     */
    Project(std::string name, std::span<char* const> argv);
    /**
     * \brief Parse truss's CLI from argc/argv (classic main() form).
     * \param name project name (used in messages and generated vcpkg.json)
     * \param argc argument count
     * \param argv argument array, passed straight to the span form.
     */
    Project(std::string name, int argc, char** argv);

    /**
     * \brief Create a new executable target owned by the project.
     * \param name bare target name (no path, no platform extension)
     * \return reference for builder chaining; the target lives as long as the Project.
     */
    Executable& add_executable(std::string name) { return add_target<Executable>(std::move(name)); }
    /// \copydoc add_executable
    StaticLib& add_static_library(std::string name) { return add_target<StaticLib>(std::move(name)); }
    /// \copydoc add_executable
    SharedLib& add_shared_library(std::string name) { return add_target<SharedLib>(std::move(name)); }
    /// \copydoc add_executable
    Interface& add_interface_library(std::string name) { return add_target<Interface>(std::move(name)); }
    /// Register a named task: `./truss <name>` builds first, then runs fn.
    void task(std::string name, TaskFn fn);

    /**
     * \brief Build hooks, run in registration order: pre_build fires once before
     * the first compile step, post_build once after the first successful compile
     * step (before test/run binaries execute). A failing hook aborts the
     * build. clean/compdb never fire hooks.
     */
    Project& pre_build(TaskFn fn) { pre_build_.push_back(std::move(fn)); return *this; }
    /// \copydoc pre_build
    Project& post_build(TaskFn fn) { post_build_.push_back(std::move(fn)); return *this; }

    /**
     * \brief Project-wide toolchain selection (compiler/linker/stdlib/archiver).
     * Per-target cross_target() still overrides it completely.
     */
    Project& configure(const Config& conf) { config_ = conf; return *this; }
    /// The active Config.
    const Config& config() const { return config_; }

    /**
     * \brief fetch(): git (shallow clone pinned to tag) or tarball
     * (sha256-verified), cached in ~/.cache/truss, materialized into
     * build/{mode}/packages/{name}, pinned in ./truss.lock. Call before build().
     * \param name package name (letters, digits, '-', '_', '.'; names the
     *             materialized directory and the truss.lock entry)
     * \param opts exactly one source: git+tag or url+sha256 (see FetchOptions)
     * \return the materialized Package, or a Config/Fetch/Io Error.
     *         Already-pinned + present packages return without touching the network.
     */
    Result<Package> fetch(std::string_view name, const FetchOptions& opts);

    /**
     * \brief fetch_all(): fetch several packages concurrently (the network-
     * bound work overlaps). Same pinning/caching as fetch(); results come
     * back in request order and the first error aborts the build script.
     * \param requests (name, options) pairs; duplicate names are a Config error
     * \return the materialized Packages in request order, or the first Error.
     */
    Result<std::vector<Package>> fetch_all(std::vector<std::pair<std::string, FetchOptions>> requests);

    /**
     * \brief add_subdirectory(): convention-based vendored project — a static
     * library named after the directory, sources from its src/ tree, includes
     * from its include/ dir when present.
     * \param dir directory (relative to the project root or absolute)
     * \return a handle to the created target for depends_on(), or a Config
     *         Error (missing directory, name clash with a non-subdirectory target).
     */
    Result<TargetRef> add_subdirectory(const std::filesystem::path& dir);
    /**
     * \brief add_subdirectory() for a fetched Package: uses the package's
     * materialized directory (build/{mode}/packages/{name}).
     * \param pkg a Package returned by fetch()/fetch_all().
     * \return a handle to the created target for depends_on(), or a Config
     *         Error (missing directory, name clash with a non-subdirectory target).
     */
    Result<TargetRef> add_subdirectory(const Package& pkg) { return add_subdirectory(pkg.dir); }

    /**
     * \brief conan 2: runs `conan install` on the conanfile (path to
     * conanfile.txt/.py or its directory), or generates one from versioned
     * refs — conan({"zlib/1.3.1", "fmt/11.0.2"}).
     * \param conanfile path to a conanfile or its directory ("." = project root)
     * \return one Interface target ("conan") carrying the whole graph's
     *         includes/libs/defines — depends_on() it; or a Toolchain/Fetch Error.
     *         Idempotent: a second call returns the same target. An unchanged
     *         conanfile fingerprint skips the tool invocation entirely.
     */
    Result<TargetRef> conan(std::string_view conanfile = ".");
    /**
     * \brief conan() with a generated conanfile: writes the versioned refs
     * into build/{mode}/packages/conan_generated/conanfile.txt and runs the
     * path overload on it.
     * \param refs conan references, each versioned ("fmt/11.0.2"); an
     *             unversioned ref is a Config error.
     * \return the "conan" Interface target, same as the path overload.
     */
    Result<TargetRef> conan(std::initializer_list<std::string_view> refs);

    /**
     * \brief vcpkg manifest mode: uses the project's vcpkg.json, or generates
     * one from `packages`. Installs under build/{mode}/vcpkg_installed and
     * returns an Interface target for depends_on().
     * \param packages vcpkg names (unversioned); empty = use the existing
     *                 vcpkg.json in the project root
     * \return the "vcpkg" Interface target, or a Toolchain/Config Error.
     *         Idempotent; an unchanged vcpkg.json skips the install.
     */
    Result<TargetRef> vcpkg(std::vector<std::string> packages);

    /// Generates the ninja manifest, runs samurai, then the requested tasks.
    /// \return success, or the first Error (also logged). Errors are logged.
    Result<void> build();

    const std::string& name() const { return name_; }
    BuildMode mode() const { return mode_; }
    const std::filesystem::path& cwd() const { return cwd_; }
    std::filesystem::path build_dir() const; /* build root + mode subdir */
    int jobs() const { return jobs_; }
    bool verbose() const { return verbose_; }
    const std::vector<std::string>& only_targets() const { return only_targets_; }

private:
    template <class T>
    T& add_target(std::string name) {
        auto t = std::make_unique<T>(std::move(name));
        auto& ref = *t;
        targets_.push_back(std::move(t));
        return ref;
    }

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
