/**
 * \file api.cppm
 * \brief truss public types partition (`import truss;` is all consumers need).
 *
 * Re-exported by the primary interface (src/truss.cppm), so consumers only
 * ever write `import truss;`. Kept as a partition because module units of
 * `truss` cannot import the primary interface of their own module.
 */
module;
#include <charconv>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <expected>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <iterator>
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
    Zig,         ///< `zig c++` + zig's bundled libc++ + `zig ar` (clang flag dialect; cross-compiles via Config::target)
};

/**
 * \brief C++ runtime linked statically into an executable / shared library
 * (Target::static_stdlib). The C library stays dynamic except with Musl.
 */
enum class StaticStdlib {
    Default = 0, ///< no static runtime (driver default: everything dynamic)
    Gcc,         ///< libstdc++ + libgcc: -static-libstdc++ -static-libgcc (g++, or clang with libstdc++)
    Llvm,        ///< libc++ + libc++abi archives (clang with stdlib(Libcxx); always the case with zig)
    Musl,        ///< fully static binary (-static) against musl: zig + a *-musl target, or a musl-targeting driver
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

/**
 * \brief Synchronous coroutine generator (range-for compatible).
 *
 * Shape adapted from cppcoro::generator (MIT, Lewis Baker). truss uses this
 * instead of std::generator because libc++ still lacks the `<generator>`
 * header (P2502R2, llvm/llvm-project#105226) while truss's own stdlib is
 * libc++; when libc++ ships it this becomes an alias for std::generator and
 * call sites are unchanged. Move-only; the coroutine produces values lazily
 * on iteration.
 */
template <class T>
class generator {
public:
    struct promise_type {
        std::optional<T> value;
        std::exception_ptr exception;
        generator get_return_object() { return generator {handle_t::from_promise(*this)}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(T v) { value.emplace(std::move(v)); return {}; }
        void return_void() {}
        void unhandled_exception() { exception = std::current_exception(); }
    };
    using handle_t = std::coroutine_handle<promise_type>;

    /// Input iterator over the lazily produced values.
    class iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = const T&;
        using pointer = const T*;

        iterator() = default;
        explicit iterator(handle_t h) : handle_(h) {}
        const T& operator*() const { return *handle_.promise().value; }
        const T* operator->() const { return &*handle_.promise().value; }
        iterator& operator++() {
            handle_.resume();
            if (handle_.done()) {
                if (handle_.promise().exception)
                    std::rethrow_exception(handle_.promise().exception);
                handle_ = {};
            }
            return *this;
        }
        iterator operator++(int) { const iterator tmp = *this; ++*this; return tmp; }
        friend bool operator==(const iterator& it, std::default_sentinel_t) {
            return !it.handle_ || it.handle_.done();
        }
        friend bool operator==(std::default_sentinel_t s, const iterator& it) { return it == s; }
        friend bool operator!=(const iterator& it, std::default_sentinel_t s) { return !(it == s); }
        friend bool operator!=(std::default_sentinel_t s, const iterator& it) { return !(it == s); }

    private:
        handle_t handle_ {};
    };

    explicit generator(handle_t h) : handle_(h) {}
    generator(const generator&) = delete;
    generator& operator=(const generator&) = delete;
    generator(generator&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    generator& operator=(generator&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    ~generator() {
        if (handle_) handle_.destroy();
    }

    iterator begin() {
        handle_.resume();
        if (handle_.done()) {
            if (handle_.promise().exception)
                std::rethrow_exception(handle_.promise().exception);
            return iterator {};
        }
        return iterator {handle_};
    }
    std::default_sentinel_t end() { return {}; }

private:
    handle_t handle_ {};
};

/// A compiler target: host by default, cross-compilable by override.
struct TargetTriple {
    Os os {Os::Unknown};                        ///< target OS
    Arch arch {Arch::Unknown};                  ///< target ISA
    CompilerFamily family {CompilerFamily::Unknown}; ///< flag dialect
    std::string compiler;                       ///< compiler driver, e.g. "clang++"
    std::string ar;                             ///< archiver, e.g. "llvm-ar"
    std::string linker;                         ///< -fuse-ld= override ("" = driver default)
    StdLib stdlib {StdLib::Default};            ///< stdlib selection (Default = compiler default)
    /// Cross-compilation target triple, e.g. "aarch64-linux-musl" ("" = native).
    /// Emitted as `--target=` (clang/clang-cl) or `-target` (zig); GCC
    /// cross-compiles through a target-prefixed driver instead (Config error).
    std::string target;
};

/// Triple of the machine truss itself runs on (compiler from $CXX when set).
/// \\return os/arch/family detected from predefined macros, plus default
///         driver and archiver (llvm-ar for clang when available).
TargetTriple host_triple();

/**
 * \brief Host introspection + program lookup for build scripts.
 *
 * The platform predicates are `consteval` (predefined macros): they describe
 * the machine the build script is compiled on — truss's distribution model
 * guarantees the same compiler builds the BMIs and the script — and work in
 * `if constexpr`. The rest are runtime facts (env, PATH, syscalls).
 */
namespace os {

/// OS of the host (compile-time, from predefined macros).
consteval Os host_os() {
#if defined(__EMSCRIPTEN__)
    return Os::Emscripten;
#elif defined(__ANDROID__)
    return Os::Android;
#elif defined(__linux__) || defined(__gnu_linux__)
    return Os::Linux;
#elif defined(_WIN32) || defined(__WIN32__)
    return Os::Windows;
#elif defined(__APPLE__) || defined(__MACH__)
    return Os::MacOS;
#elif defined(__FreeBSD__)
    return Os::FreeBSD;
#else
    return Os::Unknown;
#endif
}
/// ISA of the host (compile-time, from predefined macros).
consteval Arch host_arch() {
#if defined(__x86_64__) || defined(_M_X64)
    return Arch::X64;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return Arch::Arm64;
#elif defined(__i386__) || defined(_M_IX86)
    return Arch::X86;
#elif defined(__arm__) || defined(_M_ARM)
    return Arch::Arm32;
#elif defined(__riscv) && (__riscv_xlen == 64)
    return Arch::Riscv64;
#elif defined(__powerpc64__) || defined(__ppc64__)
    return Arch::Ppc64;
#elif defined(__s390x__)
    return Arch::S390x;
#elif defined(__wasm32__)
    return Arch::Wasm32;
#else
    return Arch::Unknown;
#endif
}
/// Compiler family of the compiler compiling the script (compile-time).
consteval CompilerFamily host_family() {
#if defined(__clang__)
    return CompilerFamily::Clang;
#elif defined(__GNUC__)
    return CompilerFamily::Gcc;
#elif defined(_MSC_VER)
    return CompilerFamily::Msvc;
#else
    return CompilerFamily::Unknown;
#endif
}

/// Host predicates (compile-time; usable in `if constexpr`).
consteval bool is_linux()   { return host_os() == Os::Linux; }
/// \\copydoc is_linux
consteval bool is_windows() { return host_os() == Os::Windows; }
/// \\copydoc is_linux
consteval bool is_macos()   { return host_os() == Os::MacOS; }

/**
 * \brief Locate a program: fork-free $PATH walk + access(X_OK) (a name
 * containing '/' is checked directly). Results (including misses) are
 * memoized for the process lifetime.
 * \param name program name or path.
 * \return absolute path, or nullopt when not found / not executable.
 */
std::optional<std::filesystem::path> find_program(std::string_view name);
/// \brief find_program() for const char* (sugar overload for C-string literals).
inline std::optional<std::filesystem::path> find_program(const char* name) {
    return find_program(std::string_view(name));
}
/**
 * \brief find_program() for several candidates; first hit wins
 * (e.g. find_program({"clang++-19", "clang++"})).
 * \param names candidate names, tried in order.
 * \return the first program found, or nullopt if none is.
 */
std::optional<std::filesystem::path> find_program(std::initializer_list<std::string_view> names);
/// True when find_program(name) succeeds; memoized.
bool program_exists(std::string_view name);
/**
 * \brief Multi-program precondition checks over find_program (memoized).
 * \param names program names to check
 * \return all_programs: every name is installed; any_program: at least one is.
 */
bool all_programs(std::initializer_list<std::string_view> names);
/// \\copydoc all_programs
bool any_program(std::initializer_list<std::string_view> names);

/// Environment variable value; nullopt when unset or empty.
std::optional<std::string> env(std::string_view name);
/// \brief env() for const char* (sugar overload for C-string literals).
inline std::optional<std::string> env(const char* name) { return env(std::string_view(name)); }
/// Types env(name, def) can parse into.
template <class T>
concept EnvParsable = std::same_as<T, bool> || std::integral<T> || std::floating_point<T> ||
                      std::same_as<T, std::string>;

/**
 * \brief Environment variable parsed into T (type deduced from `def`).
 * Lenient by design: unset, empty, or unparseable values yield `def` — a
 * garbage JOBS=banana must not kill a build script. bool accepts
 * 1/true/on/yes/y and 0/false/off/no/n (case-insensitive); numbers use
 * std::from_chars and must consume the whole value.
 * \param name variable name
 * \param def default value; also selects the parsed type
 */
template <EnvParsable T>
T env(std::string_view name, T def) {
    const auto value = env(name);
    if (!value || value->empty())
        return def;
    if constexpr (std::same_as<T, bool>) {
        std::string lower;
        lower.reserve(value->size());
        for (const char c : *value)
            lower.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
        if (lower == "1" || lower == "true" || lower == "on" || lower == "yes" || lower == "y")
            return true;
        if (lower == "0" || lower == "false" || lower == "off" || lower == "no" || lower == "n")
            return false;
        return def;
    } else if constexpr (std::same_as<T, std::string>) {
        return *value;
    } else {
        T parsed {};
        const auto [end, ec] = std::from_chars(value->data(), value->data() + value->size(), parsed);
        if (ec == std::errc {} && end == value->data() + value->size())
            return parsed;
        return def;
    }
}
/// Sugar so `env("CXX", "g++")` deduces std::string instead of char[N].
inline std::string env(std::string_view name, const char* def) { return env<std::string>(name, std::string(def)); }

// --- machine specs (runtime facts; unsupported probes -> nullopt / 0) ---
/// Logical CPU count (std::thread::hardware_concurrency; 0 if unknown).
unsigned cpu_count();
/// Physical RAM in bytes (0 when the probe is unsupported).
std::uint64_t memory_bytes();
/**
 * \brief Human-readable OS version: Linux PRETTY_NAME from /etc/os-release,
 * else "<sysname> <release>" from uname.
 */
std::optional<std::string> os_version();
/// Hostname (gethostname); nullopt on failure.
std::optional<std::string> hostname();
/// System temp directory ($TMPDIR else /tmp); query only, creates nothing.
std::filesystem::path temp_dir();
/**
 * \brief Search for truss module BMI files in standard installation paths.
 * Searches in order: TRUSS_INSTALL_PREFIX (if set), same directory as the truss
 * executable, .local/lib/truss (relative to cwd), ~/.local/lib/truss,
 * /usr/local/lib/truss, /usr/lib/truss.
 * \return vector of paths where truss BMIs were found, or empty if not found.
 */
std::vector<std::filesystem::path> find_truss_modules();
// --- process (for task() bodies) ---
/// Outcome of a spawned process.
struct ProcessResult {
    int exit_code {0};   ///< waitpid status (128+signal on death by signal)
    std::string output;  ///< captured stdout+stderr (capture() only)
};
/// Where run()/capture() execute; empty cwd = inherit.
struct RunOptions {
    std::filesystem::path cwd {};
};
/**
 * \brief Spawn argv (argv[0] searched on PATH); stdio passes through.
 * Spawn failure is an Error; a non-zero exit_code is a normal result.
 */
Result<ProcessResult> run(std::span<const std::string> argv, RunOptions = {});
/// \\copydoc run, with stdout+stderr captured into ProcessResult::output.
Result<ProcessResult> capture(std::span<const std::string> argv, RunOptions = {});
/// run() with a brace-enclosed argument list.
Result<ProcessResult> run(std::initializer_list<std::string> argv, RunOptions opts = {});
/// capture() with a brace-enclosed argument list.
Result<ProcessResult> capture(std::initializer_list<std::string> argv, RunOptions opts = {});
/**
 * \brief RAII unique temp directory (mkdtemp "truss-XXXXXX" under
 * temp_dir()): removed on destruction, move-only.
 * truss uses it internally for scratch work (scan-deps, fetch staging).
 */
class temp_scope {
public:
    temp_scope();  ///< dir creation failure leaves path() empty; check valid().
    temp_scope(const temp_scope&) = delete;
    temp_scope& operator=(const temp_scope&) = delete;
    temp_scope(temp_scope&&) noexcept;
    temp_scope& operator=(temp_scope&&) noexcept;
    ~temp_scope();  ///< removes the tree; never throws
    const std::filesystem::path& path() const;
    bool valid() const { return !dir_.empty(); }
    /// Keep the contents (skip cleanup) — e.g. to hand results to the caller.
    void release() { dir_.clear(); }
private:
    std::filesystem::path dir_;
};

} // namespace os

/**
 * \brief Filesystem helpers for build scripts: globbing, traversal, text I/O.
 *
 * Non-throwing throughout (error_code overloads internally); fallible
 * operations return Result, "not found" queries return nullopt.
 */
namespace fs {

/// glob(): single pattern. `*` within one path component, `**` across
/// components, `?` one char, `{a,b}` alternation. Hidden entries are skipped;
/// results are sorted, deduplicated, relative to the current directory.
Result<std::vector<std::filesystem::path>> glob(std::string_view pattern);
/// glob(): several patterns, one tree walk per distinct scan root.
Result<std::vector<std::filesystem::path>> glob(std::initializer_list<std::string_view> patterns);

/// walk() depth value for "no limit".
inline constexpr int walk_unlimited = -1;

/// Options for walk().
struct WalkOptions {
    int depth {walk_unlimited}; ///< 0 = dir's direct entries, 1 = one level down, ...
    bool skip_hidden {true};    ///< skip dot entries and never descend into hidden dirs
};

/**
 * \brief Lazy recursive traversal (C++26 generator; composes with std::ranges).
 * Forgiving like glob(): a missing root yields nothing, unreadable
 * directories are skipped.
 * \param dir directory to walk; entries are yielded as dir/entry paths
 * \param opts depth cap and hidden-entry policy
 */
generator<std::filesystem::path> walk(const std::filesystem::path& dir, WalkOptions opts = {});

/**
 * \brief Search for a file in from_dir and every parent (workspace-root /
 * .git-style discovery).
 * \param filename file to look for (e.g. ".git", "vcpkg.json")
 * \param from_dir starting directory
 * \return the first hit, or nullopt up to the filesystem root.
 */
std::optional<std::filesystem::path> find_upward(std::string_view filename, const std::filesystem::path& from_dir);
/// find_upward() starting at the current directory.
std::optional<std::filesystem::path> find_upward(std::string_view filename);

/// The first candidate that exists, in argument order; nullopt when none does.
std::optional<std::filesystem::path> first_existing(std::initializer_list<std::filesystem::path> candidates);

/**
 * \brief Locate a library file: lib{name}.a / .so / .dylib / {name}.lib,
 * searched directory-first (caller's dir priority wins). For "is the system
 * copy installed" gating only — real dependencies go through
 * fetch()/conan()/vcpkg(), and pkg-config via os::capture covers the
 * name≠file case (e.g. boost).
 * \param name library base name without lib prefix or extension
 * \param search_dirs directories to scan (e.g. -L dirs, build/{mode}/lib/)
 * \return the first match, or nullopt.
 */
std::optional<std::filesystem::path> find_library(std::string_view name,
                                                  std::span<const std::filesystem::path> search_dirs);
/**
 * \brief find_library() over the default system library paths:
 * $LD_LIBRARY_PATH / $DYLD_LIBRARY_PATH, /usr/local/lib, /usr/lib, the
 * host-arch multiarch dir (GNU/Linux), /lib, /opt/homebrew/lib (macOS).
 * Best-effort convenience — explicit dirs stay the reliable route.
 */
std::optional<std::filesystem::path> find_library(std::string_view name);

/// Read a whole file into a string.
Result<std::string> read_text(const std::filesystem::path&);
/// Write a whole file, creating parent directories as needed.
Result<void> write_text(const std::filesystem::path&, std::string_view);
/// Write only when the content differs (keeps mtimes stable for build inputs).
Result<void> write_text_if_changed(const std::filesystem::path&, std::string_view);
/**
 * \brief Iterate a file line by line (newline terminators stripped).
 * \return a generator over the lines, or an Error if the file cannot be
 *         opened; read errors mid-file simply end the iteration.
 */
Result<generator<std::string>> read_lines(const std::filesystem::path&);

/// Lowercase hex sha256 of a file (the same digest fetch() pinning uses).
Result<std::string> sha256_file(const std::filesystem::path&);
/// Current working directory (non-throwing pwd).
Result<std::filesystem::path> current_dir();

/// Materialize a directory tree with hardlinks (copy fallback across
/// filesystems); .git directories are skipped.
Result<void> copy_tree(const std::filesystem::path& from, const std::filesystem::path& to);

/// path -> std::string with generic (forward-slash) separators.
std::string generic_str(const std::filesystem::path&);

} // namespace fs

/**
 * \brief Colored stderr-free logging on truss's own channel.
 * Use from task() bodies and build-script helpers for output consistent
 * with truss's own messages.
 */
namespace log {
/// Informational line (cyan).
void info(std::string_view msg);
/// Warning line (orange).
void warn(std::string_view msg);
/// Success line (green).
void success(std::string_view msg);
/// Error line (red).
void error(std::string_view msg);
/// Error line including err.path when set.
void error(const Error& err);
} // namespace log

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
    /**
     * \brief Cross-compile every target for this triple ("" = native), e.g.
     * "aarch64-linux-gnu", "x86_64-windows-gnu", "x86_64-linux-musl".
     * os()/arch() of the resolved triple are derived from it, so outputs land in
     * their own build/{mode}/bin/\<arch\>-\<os\>-... directory. Best paired with
     * toolchain(Toolchain::Zig) (sysroots bundled); plain clang needs a sysroot
     * passed through flags("--sysroot=...").
     */
    Config& target(std::string triple) { target_ = std::move(triple); return *this; }

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
        case Toolchain::Zig: /* zig always links its own libc++: no -stdlib= */
            compiler_ = "zig c++"; ar_ = "zig ar"; stdlib_ = StdLib::Default; break;
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
    const std::string& target() const { return target_; }

private:
    std::string compiler_, linker_, ar_, launcher_, target_;
    bool launcher_set_ {false};
    StdLib stdlib_ {StdLib::Default};
};

// Forward declaration for TargetRef (used in Target::depends_on below)
class TargetRef;

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
        append_strings(source_patterns_, patterns);
        return *this;
    }
    template <StringRange R>
    Target& sources(R&& patterns) {
        append_strings(source_patterns_, std::forward<R>(patterns));
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
        append_strings(include_dirs_, dirs);
        return *this;
    }
    template <StringRange R>
    Target& include_dirs(R&& dirs) {
        append_strings(include_dirs_, std::forward<R>(dirs));
        return *this;
    }
    /**
     * \brief Add a system include directory (`-isystem`, MSVC `/external:I`):
     * warnings inside its headers are silenced. For third-party headers.
     * Propagates to every dependent like include_dirs(). Overloads: single,
     * braced list, range.
     */
    Target& system_include_dirs(std::string_view dir) {
        system_include_dirs_.emplace_back(dir);
        return *this;
    }
    Target& system_include_dirs(std::initializer_list<std::string_view> dirs) {
        append_strings(system_include_dirs_, dirs);
        return *this;
    }
    template <StringRange R>
    Target& system_include_dirs(R&& dirs) {
        append_strings(system_include_dirs_, std::forward<R>(dirs));
        return *this;
    }
    /// Add preprocessor defines (-D). Overloads: single, braced list, range.
    /// Private to this target (Interface targets: passed to dependents); see public_defines().
    Target& defines(std::string_view def) {
        defines_.emplace_back(def);
        return *this;
    }
    Target& defines(std::initializer_list<std::string_view> defs) {
        append_strings(defines_, defs);
        return *this;
    }
    template <StringRange R>
    Target& defines(R&& defs) {
        append_strings(defines_, std::forward<R>(defs));
        return *this;
    }
    /**
     * \brief Add defines used by this target *and* every dependent, transitively
     * (CMake's PUBLIC): for macros the library's headers need to agree on.
     * Overloads: single, braced list, range.
     */
    Target& public_defines(std::string_view def) {
        public_defines_.emplace_back(def);
        return *this;
    }
    Target& public_defines(std::initializer_list<std::string_view> defs) {
        append_strings(public_defines_, defs);
        return *this;
    }
    template <StringRange R>
    Target& public_defines(R&& defs) {
        append_strings(public_defines_, std::forward<R>(defs));
        return *this;
    }
    /// Add libraries to link (-l). Overloads: single, braced list, range.
    Target& link_libs(std::string_view lib) {
        link_libs_.emplace_back(lib);
        return *this;
    }
    Target& link_libs(std::initializer_list<std::string_view> libs) {
        append_strings(link_libs_, libs);
        return *this;
    }
    template <StringRange R>
    Target& link_libs(R&& libs) {
        append_strings(link_libs_, std::forward<R>(libs));
        return *this;
    }
    /// Add library search directories (-L). Overloads: single, braced list, range.
    Target& link_paths(std::string_view path) {
        link_paths_.emplace_back(path);
        return *this;
    }
    Target& link_paths(std::initializer_list<std::string_view> paths) {
        append_strings(link_paths_, paths);
        return *this;
    }
    template <StringRange R>
    Target& link_paths(R&& paths) {
        append_strings(link_paths_, std::forward<R>(paths));
        return *this;
    }
    /// Apple-only: link a framework. Overloads: single, braced list, range.
    Target& link_frameworks(std::string_view fw) {
        frameworks_.emplace_back(fw);
        return *this;
    }
    Target& link_frameworks(std::initializer_list<std::string_view> fw) {
        append_strings(frameworks_, fw);
        return *this;
    }
    template <StringRange R>
    Target& link_frameworks(R&& fw) {
        append_strings(frameworks_, std::forward<R>(fw));
        return *this;
    }
    /// Apple-only: framework search directories (-F). Overloads: single, braced list, range.
    Target& framework_paths(std::string_view path) {
        framework_paths_.emplace_back(path);
        return *this;
    }
    Target& framework_paths(std::initializer_list<std::string_view> paths) {
        append_strings(framework_paths_, paths);
        return *this;
    }
    template <StringRange R>
    Target& framework_paths(R&& paths) {
        append_strings(framework_paths_, std::forward<R>(paths));
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
    /**
     * \brief Raw compile flags for this target *and* every dependent,
     * transitively (CMake's PUBLIC), e.g. `-pthread`, `-fno-rtti` for a library
     * whose ABI needs it. Appended verbatim; repeat calls accumulate.
     */
    Target& public_flags(std::string_view raw) {
        if (!raw.empty())
            public_flags_.emplace_back(raw);
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
     * \brief Link this dependency statically into whatever consumes it.
     * (`static` is a C++ keyword, hence the name.)
     * - Interface targets (conan()/vcpkg()/header+prebuilt packages): their
     *   link_libs are wrapped in `-Wl,-Bstatic ... -Wl,-Bdynamic` (the .a must exist).
     * - SharedLib targets: built as a static archive instead of a .so.
     * - Static libraries / executables: no effect (already static / not a dependency).
     * ELF targets only (Linux, FreeBSD, Android, MinGW); a Config error on macOS/MSVC.
     * \param v true to enable (default when called with no argument).
     */
    Target& static_link(bool v = true) {
        if (kind_ == TargetKind::SharedLibrary || demoted_shared_) {
            kind_ = v ? TargetKind::StaticLibrary : TargetKind::SharedLibrary;
            demoted_shared_ = v;
        }
        static_link_ = v;
        return *this;
    }
    /**
     * \brief Link every dependency of this executable/shared library statically
     * (transitively): Interface link_libs and this target's own link_libs go
     * through `-Bstatic`, SharedLib deps are linked from a static twin archive
     * built from the same (PIC) objects. The C library family (c, m, pthread,
     * dl, rt, ...) and the C++ runtime stay dynamic — use static_stdlib() for the latter.
     * \param v true to enable (default when called with no argument).
     */
    Target& static_deps(bool v = true) { static_deps_ = v; return *this; }
    /**
     * \brief Link the C++ runtime statically (see StaticStdlib). Validated at
     * build() against the project toolchain: Gcc needs libstdc++, Llvm needs
     * clang + stdlib(Libcxx) (or zig), Musl needs zig + a *-musl target or a
     * musl-targeting driver. Typical portable Linux binary:
     * `.static_deps().static_stdlib(truss::StaticStdlib::Gcc)` -> depends on glibc only.
     */
    Target& static_stdlib(StaticStdlib s) { static_stdlib_ = s; return *this; }
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
    /// \brief Depend on a target handle (`depends_on(*proj.add_subdirectory(...))`).
    /// Exact-match overload: without it, a TargetRef converts equally well to
    /// `const Target&` and to `Result<TargetRef>` (ambiguous call).
    Target& depends_on(TargetRef dep);
    /// \brief Depend on a target from a Result (e.g., from conan()/vcpkg()).
    /// An error Result is kept and reported by Project::build() (never dropped).
    Target& depends_on(Result<TargetRef> dep_result);
    /// Mark as a test executable (built and run by `./truss test`).
    Target& is_test(bool v = true) { test_ = v; return *this; }
    /**
     * \brief Combine another static library's archive into this one.
     *
     * This target and every merged library must be static libraries; the
     * result is itself a static library (consumable via depends_on). Members
     * are merged exactly as listed — their own transitive dependencies are not
     * pulled in, so depends_on() those separately if a consumer needs them.
     * Emitted via an `ar -M` MRI script (GNU ar / llvm-ar / zig ar); MSVC is a
     * Config error. A merged library needs no sources of its own.
     */
    Target& merge(const Target& lib) { merge_.push_back(&lib); return *this; }

    /* --- read access (used by the build engine) --- */
    const std::string& name() const { return name_; }
    TargetKind kind() const { return kind_; }
    const std::vector<std::string>& source_patterns() const { return source_patterns_; }
    const std::vector<std::string>& removed_sources() const { return removed_sources_; }
    const std::vector<std::string>& include_dirs() const { return include_dirs_; }
    const std::vector<std::string>& system_include_dirs() const { return system_include_dirs_; }
    const std::vector<std::string>& public_defines() const { return public_defines_; }
    const std::vector<std::string>& public_flags() const { return public_flags_; }
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
    /// static_link() / static_deps() state (named apart: the setters default their bool).
    bool static_link_enabled() const { return static_link_; }
    bool static_deps_enabled() const { return static_deps_; }
    StaticStdlib static_stdlib() const { return static_stdlib_; }
    /// First error handed to depends_on(Result) (reported by build()).
    const std::optional<Error>& deferred_error() const { return deferred_error_; }
    const std::optional<TargetTriple>& triple_override() const { return triple_; }
    const std::vector<const Target*>& deps() const { return deps_; }
    const std::vector<const Target*>& merged_libraries() const { return merge_; }
    bool is_test_target() const { return test_; }

private:
    /// Shared append loop behind the braced-list/range builder overloads.
    template <class R>
    static void append_strings(std::vector<std::string>& v, R&& range) {
        for (const auto& s : range)
            v.emplace_back(std::string_view(s));
    }

    std::string name_;
    TargetKind kind_;
    std::vector<std::string> source_patterns_;
    std::vector<std::string> removed_sources_;
    std::vector<std::string> include_dirs_, defines_, link_libs_, link_paths_;
    std::vector<std::string> system_include_dirs_, public_defines_, public_flags_;
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
    bool static_link_ {false};
    bool demoted_shared_ {false}; /* SharedLib turned into a static archive by static_link() */
    bool static_deps_ {false};
    StaticStdlib static_stdlib_ {};
    std::optional<Error> deferred_error_;
    std::optional<TargetTriple> triple_;
    std::vector<const Target*> deps_;
    std::vector<const Target*> merge_; /* static libraries combined into this archive */
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
    explicit TargetRef(Target& t) : ref_(t) {}
    Target& get() const { return ref_; }
    Target& operator*() const { return ref_; }
    Target* operator->() const { return &ref_.get(); }
    operator Target&() const { return ref_; }

private:
    std::reference_wrapper<Target> ref_;
};

// Inline implementation of depends_on(Result<TargetRef>) - TargetRef must be complete
inline Target& Target::depends_on(TargetRef dep) {
    return depends_on(static_cast<const Target&>(dep.get()));
}

inline Target& Target::depends_on(Result<TargetRef> dep_result) {
    if (dep_result)
        return depends_on(static_cast<const Target&>(dep_result->get()));
    /* keep the first failure: build() reports it instead of linking without the dep */
    if (!deferred_error_)
        deferred_error_ = std::move(dep_result.error());
    return *this;
}

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
     * \param argv full process argv; truss parses `debug|release`, built-in
     *             tasks, `task NAME` / `task list`,
     *             `-j`, `-v`, `-n`, `--explain`, `--target`, `--build-dir`,
     *             `-h`/`--help`, and `--` (rest forwarded to `run`)
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
    /**
     * \brief Register a named task: `./truss task <name>` builds first, then
     * runs fn. `./truss task list` prints every task with its description.
     * Task names live apart from the built-ins (`task test` is fine); `list`
     * is reserved and duplicates are a Config error reported by build().
     * \param name task name, as typed after `./truss task`
     * \param description one line shown by `./truss task list`
     * \param fn the task body
     */
    void task(std::string name, std::string description, TaskFn fn);
    /// \copydoc task(std::string, std::string, TaskFn)
    void task(std::string name, TaskFn fn) { task(std::move(name), {}, std::move(fn)); }

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
    /**
     * \brief Generate compile_commands.json for the project.
     * This invokes samurai's compdb tool to generate a compilation database.
     * \param path Optional output path (defaults to project root's compile_commands.json).
     */
    Result<void> compile_commands(const std::filesystem::path& path = ".");

    const std::string& name() const { return name_; }
    BuildMode mode() const { return mode_; }
    const std::filesystem::path& cwd() const { return cwd_; }
    std::filesystem::path build_dir() const; /* build root + mode subdir */
    int jobs() const { return jobs_; }
    bool verbose() const { return verbose_; }
    const std::vector<std::string>& only_targets() const { return only_targets_; }
    /// Get the auto-discovered truss module paths.
    const std::vector<std::filesystem::path>& truss_module_paths() const { return truss_module_paths_; }

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
    struct UserTask {
        std::string name, description;
        TaskFn fn;
    };
    std::vector<UserTask> user_tasks_;
    std::vector<TaskFn> pre_build_, post_build_;
    Config config_;

    /* parsed CLI state */
    BuildMode mode_ {BuildMode::Debug};
    int jobs_ {0};
    bool verbose_ {false};
    bool dry_run_ {false};
    bool explain_ {false};
    bool list_tasks_ {false};                   /* `task list` / bare `task` */
    std::vector<std::string> requested_tasks_;  /* user tasks as "task:<name>" */
    std::vector<std::string> only_targets_;
    std::vector<std::string> run_args_; /* after `--`, forwarded by run */
    std::optional<std::filesystem::path> build_root_;
    std::optional<Error> cli_error_;
    std::vector<std::string> subdir_target_names_; /* created by add_subdirectory */
    std::vector<std::filesystem::path> truss_module_paths_; /* auto-discovered truss module paths */
};

} // namespace truss
