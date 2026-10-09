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

/* How .cppm sources in a user target get compiled and wired.
 * Auto (the default): conservative v0.1 mode — every .cppm becomes a BMI edge and
 * every other unit in the target (and its deps) imports all of them.
 * Manual: precise per-file BMI wiring — not implemented yet. */
enum class ModulesMode { Default = 0, Off, Auto, Manual };

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
};

/* Triple of the machine truss itself runs on. */
TargetTriple host_triple();

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

/* ---------------------------------------------------------------------------
 * Pipe composition: `target | with_sources({...}) | with_depends_on(core)`.
 * Adapters are callables; operator| applies them and keeps the concrete type.
 * ------------------------------------------------------------------------- */
inline auto with_sources(std::vector<std::string> patterns) {
    return [p = std::move(patterns)](Target& t) { t.sources(p); };
}
inline auto with_sources_in(std::string dir) {
    return [d = std::move(dir)](Target& t) { t.sources_in(d); };
}
inline auto with_include_dirs(std::vector<std::string> dirs) {
    return [d = std::move(dirs)](Target& t) { t.include_dirs(d); };
}
inline auto with_defines(std::vector<std::string> defs) {
    return [d = std::move(defs)](Target& t) { t.defines(d); };
}
inline auto with_link_libs(std::vector<std::string> libs) {
    return [l = std::move(libs)](Target& t) { t.link_libs(l); };
}
inline auto with_linker_flags(std::string f) {
    return [f = std::move(f)](Target& t) { t.linker_flags(f); };
}
inline auto with_depends_on(const Target& dep) {
    return [&dep](Target& t) { t.depends_on(dep); };
}
inline auto with_cpp_standard(CppStandard s) {
    return [s](Target& t) { t.cpp_standard(s); };
}
inline auto with_warnings(Warnings w) {
    return [w](Target& t) { t.warnings(w); };
}
inline auto with_optimization(Optimization o) {
    return [o](Target& t) { t.optimization(o); };
}
inline auto with_debug_info(DebugInfo d) {
    return [d](Target& t) { t.debug_info(d); };
}
inline auto with_sanitizer(Sanitizer s) {
    return [s](Target& t) { t.sanitizer(s); };
}
inline auto with_pic(bool v) {
    return [v](Target& t) { t.pic(v); };
}
inline auto with_is_test(bool v = true) {
    return [v](Target& t) { t.is_test(v); };
}

template <class T, class F>
    requires std::derived_from<T, Target> && std::invocable<F&, T&>
T& operator|(T& target, F&& fn) {
    std::forward<F>(fn)(target);
    return target;
}

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

    /* --- packages (phase 5) ------------------------------------------------
     * fetch(): git (shallow clone pinned to tag) or tarball (sha256-verified),
     * cached in ~/.cache/truss, materialized into build/{mode}/packages/{name},
     * pinned in ./truss.lock. Call before build(). */
    Result<Package> fetch(std::string_view name, const FetchOptions& opts);

    /* add_subdirectory(): convention-based vendored project — a static library
     * named after the directory, sources from its src/ tree, includes from its
     * include/ dir when present. Returns the target for depends_on(). */
    Result<Target*> add_subdirectory(const std::filesystem::path& dir);
    Result<Target*> add_subdirectory(const Package& pkg) { return add_subdirectory(pkg.dir); }

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

    /* parsed CLI state */
    BuildMode mode_ {BuildMode::Debug};
    int jobs_ {0};
    bool verbose_ {false};
    bool dry_run_ {false};
    std::vector<std::string> requested_tasks_;
    std::vector<std::string> only_targets_;
    std::optional<std::filesystem::path> build_root_;
    std::optional<Error> cli_error_;
    std::vector<std::string> subdir_target_names_; /* created by add_subdirectory */
};

} // namespace truss
