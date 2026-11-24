/**
 * \file impl.cppm
 * \brief truss internal helpers partition (see the \ref grp_internals
 * "Internals" group and the Internals page for the architecture).
 *
 * Names here are NOT exported from the primary interface (src/truss.cppm
 * does not re-export this partition), so they are invisible to `import
 * truss;` consumers. Everything in truss::detail is internal: documented
 * for maintainers, not guaranteed stable across releases.
 */
module;
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module truss:impl;
import :api;

/// \defgroup grp_internals Internals
/// \{

namespace truss::detail {

/* --- logging: promoted to the public truss::log (see :api) --------------- */

/// Set the log threshold (0 debug, 1 info, 2 warn, 3 error-only). The CLI's
/// `-q` maps to 3; `TRUSS_LOG` (quiet/warn/info/debug) is applied once at
/// startup by log_init_from_env().
/// \ingroup grp_internals
void log_set_min_level(int level);
/// Apply the TRUSS_LOG environment variable (idempotent, first call wins).
/// \ingroup grp_internals
void log_init_from_env();

/* --- filesystem ---------------------------------------------------------- */

/// Read a whole file into a string (one allocation for seekable files).
/// \ingroup grp_internals
Result<std::string> read_file(const std::filesystem::path& p);
/// SHA-256 of an in-memory buffer, lowercase hex (the same digest fetch()
/// pinning uses, applied to a string).
/// \ingroup grp_internals
std::string sha256_bytes(std::string_view data);
/// Write a whole file, creating parent directories as needed.
/// \ingroup grp_internals
Result<void> write_file(const std::filesystem::path& p, std::string_view data);
/// Write only when content differs: keeps mtimes stable so generated build
/// inputs (unity TUs, the manifest) do not trigger spurious work.
/// \ingroup grp_internals
Result<void> write_file_if_changed(const std::filesystem::path& p, std::string_view data);
/// Prerequisite paths of a Make depfile (`-MMD -MF`): for every rule, the words
/// after its first `:`. `\\<newline>` continues a line, `\\ ` is a space inside a path.
/// \ingroup grp_internals
std::vector<std::string> parse_depfile(std::string_view text);
/// Create a directory and all parents.
/// \ingroup grp_internals
Result<void> make_dirs(const std::filesystem::path& p);
/// Split a PATH-style environment value on ':' — all entries, empty ones
/// included (POSIX gives them site-specific meaning: empty PATH entry = cwd).
/// \ingroup grp_internals
std::vector<std::string_view> split_env_list(std::string_view value);
/// Remove a file or directory tree (the `clean` task).
/// \ingroup grp_internals
Result<void> remove_tree(const std::filesystem::path& p);

/* --- lockfile (truss.lock): tiny key=value pin file ---------------------- */

/// Parse a truss.lock into name -> pin pairs (missing file = empty).
/// \ingroup grp_internals
std::map<std::string, std::string> read_lockfile(const std::filesystem::path& file);
/// Regenerate the whole truss.lock from the given entries.
/// \ingroup grp_internals
Result<void> write_lockfile(const std::filesystem::path& file,
                            const std::map<std::string, std::string>& entries);

/* Glob relative to cwd. `*` within one path component, `**` across
 * components, `?` one char, `{a,b}` alternation. Hidden entries are skipped.
 * Results are sorted and deduplicated. glob_multi brace-expands every
 * pattern, compiles one regex per alternative, then walks each distinct
 * magic-free literal prefix exactly once (sources_in adds six patterns that
 * share one walk). */

/// Glob with a single pattern (convenience wrapper over glob_multi).
/// \ingroup grp_internals
Result<std::vector<std::filesystem::path>> glob(std::string_view pattern);
/// Glob several patterns with one tree walk per distinct scan root.
/// \ingroup grp_internals
Result<std::vector<std::filesystem::path>> glob_multi(std::span<const std::string> patterns);
/// True when a pattern contains glob magic (`*`, `?` or `{a,b}`).
/// \ingroup grp_internals
bool has_glob_magic(std::string_view pattern);

/// path -> std::string with generic (forward-slash) separators.
/// \ingroup grp_internals
std::string path_str(const std::filesystem::path& p);

/* --- subprocess ---------------------------------------------------------- */

/// Result of a spawned process.
/// \ingroup grp_internals
struct ProcOutcome {
    int exit_code {0};   ///< waitpid status (128+signal on death by signal)
    std::string output;  ///< captured stdout (+stderr) when requested
};
/// Spawn argv via posix_spawnp (PATH search); optionally capture merged output.
/// The child runs in its own process group; timeout_seconds (0 = none, default
/// 600) SIGKILLs the whole group on expiry and returns a Command error.
/// \ingroup grp_internals
Result<ProcOutcome> run_process(std::span<const std::string> argv, bool capture,
                                int timeout_seconds = 600);
/// Spawn argv in cwd ("" = inherit).
/// \ingroup grp_internals
Result<ProcOutcome> run_process(std::span<const std::string> argv, bool capture,
                                const std::filesystem::path& cwd, int timeout_seconds = 600);
/// Spawn capturing stdout only; stderr passes through (machine-readable
/// tool output, e.g. `conan install --format=json`).
/// \ingroup grp_internals
Result<ProcOutcome> run_process_stdout(std::span<const std::string> argv, int timeout_seconds = 600);
/* program probing lives in the public truss::os (os::program_exists). */

/* --- C++ module scanning -------------------------------------------------- */

/// What the regex scan learned about one source file.
/// \ingroup grp_internals
struct ModuleUnitInfo {
    std::string path;             ///< source path as resolved by the globber
    std::string module_name;      ///< "" for plain TUs; "math" or "math:part"
    bool is_bmi_source {};        ///< true for .cppm interface/partition units
    std::vector<std::string> imports; ///< raw names: "math", ":part", "std"...
    std::vector<std::string> header_imports; ///< header-unit imports as written
                                             ///< (e.g. "<vector>"): truss does
                                             ///< not support them, and it must
                                             ///< say so instead of dropping them
    bool mentions_module_syntax {}; ///< the RAW text contains "import" or
                                    ///< "module" somewhere (comments and
                                    ///< literals included — deliberate: this
                                    ///< is a CHEAP pre-filter, and a false
                                    ///< positive only costs a compiler scan).
                                    ///< When false, no compiler scan can find
                                    ///< an import: the keywords cannot be
                                    ///< macro-generated.
};

/// Extract the module declaration + import list from a source file with the
/// hand-rolled line scan (comments/strings/literals blanked first; no
/// preprocessor — see ModulesMode::Manual for the authoritative alternative).
/// \ingroup grp_internals
Result<ModuleUnitInfo> scan_module_unit(const std::string& path);

/// Resolve a unit's imports against available BMIs (module name -> pcm path).
/// \return the `-fmodule-file=NAME=PATH` flags and the BMI files to order
///         before the edge (parallel arrays). Rules: a BMI source never
///         consumes its own BMI; an implementation unit implicitly imports
///         its primary; importing a primary pulls every partition BMI.
/// \ingroup grp_internals
Result<std::pair<std::string, std::vector<std::string>>>
resolve_module_imports(const ModuleUnitInfo& u, const std::map<std::string, std::string, std::less<>>& bmis,
                       const std::map<std::string, std::vector<std::string>, std::less<>>& module_imports);

/// Locate the `clang-scan-deps` binary matching `compiler` (checking sibling
/// directory and matching `-NN` version suffix before falling back to PATH).
/// \ingroup grp_internals
std::optional<std::filesystem::path> find_clang_scan_deps(std::string_view compiler);

/// Authoritative import closure for one TU via the compiler's p1689 output:
/// clang through clang-scan-deps, GCC through -fdeps-format=p1689r5 — one TU
/// per invocation, ~10 ms per TU.
/// \return (named-module requires, header-unit requires as written — the
///          latter are the caller's error to report; truss builds no
///          header-unit CMIs)
/// \ingroup grp_internals
Result<std::pair<std::vector<std::string>, std::vector<std::string>>>
scan_deps_requires(const std::string& compiler, std::string_view compile_flags,
                   const std::string& source, const std::filesystem::path& cwd,
                   const std::filesystem::path& scratch);

/* --- module-scan result cache -------------------------------------------- */

/// Counters for one `build()`: how many compiler scans were avoided.
/// \ingroup grp_internals
struct ScanCacheStats {
    std::size_t hot {};     ///< served from the cache (file unchanged)
    std::size_t cold {};    ///< actually scanned, then cached
    std::size_t skipped {}; ///< no module syntax in the raw text: nothing to scan
};

/// The counters accumulate across one process run; call this before a build.
/// \ingroup grp_internals
void scan_cache_reset_stats();
/// Snapshot of the counters.
/// \ingroup grp_internals
ScanCacheStats scan_cache_stats();
/// Record one skipped unit (no module syntax) in the counters.
/// \ingroup grp_internals
void scan_cache_note_skipped();

/// Cache key for one TU's scan result: everything the compiler scan's answer
/// depends on that truss knows up front — the working directory, the source
/// path, the compiler binary (path + size + mtime, so an upgrade invalidates
/// it), the exact compile-flag string, and the cache format version. Header
/// CONTENT is not part of the key (truss does not have the deps before the
/// manifest exists); see the Internals page for the documented consequence.
/// \ingroup grp_internals
std::string scan_cache_key(const std::string& cwd, const std::string& compiler,
                           std::string_view compile_flags, const std::string& source);

/// Look a cached scan result up. A hit requires the format version to match
/// and the source's (mtime, size) to be unchanged. Any I/O or parse problem is
/// a miss, never an error: the cache is an optimization, never a dependency.
/// \return the parsed (imports, header_imports), or nullopt on a miss
/// \ingroup grp_internals
Result<std::optional<std::pair<std::vector<std::string>, std::vector<std::string>>>>
scan_cache_lookup(const std::filesystem::path& cache_dir, std::string_view key,
                  const std::string& source);

/// Snapshot of a source file's (mtime, size) taken BEFORE scanning, so a
/// mid-scan edit never gets its new mtime stamped onto a stale scan result.
/// \ingroup grp_internals
struct ScanSourceStat {
    std::int64_t mtime {};
    std::uintmax_t size {};
    bool ok {};
};

/// Capture (mtime, size) for a source path before launching a compiler scan.
/// \ingroup grp_internals
ScanSourceStat scan_cache_stat(const std::string& source);

/// Store a successful scan result (atomic replace; parents created). Failures
/// are ignored by callers — a read-only build dir must not fail the build.
/// Pass `pre_stat` captured before scanning to avoid a mid-scan TOCTOU race.
/// \ingroup grp_internals
Result<void> scan_cache_store(const std::filesystem::path& cache_dir, std::string_view key,
                              const std::string& source,
                              std::span<const std::string> imports,
                              std::span<const std::string> header_imports,
                              std::optional<ScanSourceStat> pre_stat = std::nullopt);

/// Bound the cache directory: when it holds more than `max_entries` files, the
/// oldest (by mtime) are removed down to three quarters of that. Entries are
/// pure derived data, so dropping one only costs a rescan. Best effort.
/// \ingroup grp_internals
Result<void> scan_cache_gc(const std::filesystem::path& cache_dir,
                           std::size_t max_entries = 4096);

/// scan_module_unit() with a (mtime, size)-validated result cache: a no-op
/// invocation does not even read the sources (one stat per file). The cache is
/// keyed by source path only (the text scan does not depend on flags).
/// \ingroup grp_internals
Result<ModuleUnitInfo> scan_module_unit_cached(const std::filesystem::path& cache_dir,
                                               const std::string& source);

/* --- compiler flags ------------------------------------------------------ */

/// Compiled per-target compiler flag strings (space-joined).
/// \ingroup grp_internals
struct FlagSet {
    std::string compile;   ///< flags for C++ compile edges
    std::string compile_c; ///< flags for C compile edges (C standard + c_flags())
};

/// Link flags split by where they belong: driver-side flags go before the
/// driver's input list, linker-side flags go after `/link` on MSVC (for
/// GCC/Clang the distinction is cosmetic — the driver takes everything).
/// \ingroup grp_internals
struct LinkFlags {
    std::string driver; ///< flags placed by the compiler driver
    std::string linker; ///< flags passed through to the linker
};

/// DebugInfo::Default resolves to Full in debug mode, None otherwise.
/// \ingroup grp_internals
DebugInfo resolve_debug(BuildMode mode, DebugInfo debug);

/// The compile flags clang folds into a BMI's configuration check (hardening,
/// sanitizers, LTO): build BMIs that must match consumers (the std module)
/// with these same flags. GCC/Clang dialect; MSVC builds no std module.
/// \ingroup grp_internals
std::string bmi_config_flags(const TargetTriple& triple, Hardening hardening,
                             Sanitizer sanitizer, Lto lto);

/// Build the compile flag strings from target options via the enum-indexed
/// flag tables (GCC, Clang and MSVC dialects; unknown families are errors).
/// \ingroup grp_internals
Result<FlagSet> build_flags(const TargetTriple& triple, BuildMode mode,
                            CppStandard std, Warnings warnings, DebugInfo debug,
                            Optimization optimization, Sanitizer sanitizer, Lto lto,
                            Hardening hardening,
                            bool pic, std::span<const std::string> defines,
                            std::span<const std::string> includes,
                            std::string_view user_flags,
                            std::span<const std::string> system_includes = {},
                            CStandard c_std = CStandard::Default,
                            std::string_view user_c_flags = {},
                            bool symbols_hidden = false);
/// Build the link flag strings (sanitizers/LTO/hardening/stdlib/libs/frameworks).
/// `static_libs` are linked through `-Wl,-Bstatic ... -Wl,-Bdynamic` (before the
/// dynamic `libs`, so a static archive's own -l needs resolve to the right);
/// `static_stdlib` adds the static C++ runtime flags (validated: Config errors
/// for unsupported toolchain/stdlib combinations).
/// \ingroup grp_internals
Result<LinkFlags> link_flags(const TargetTriple& triple, BuildMode mode, DebugInfo debug,
                             Sanitizer sanitizer, Lto lto, Hardening hardening,
                             std::span<const std::string> link_paths,
                             std::span<const std::string> libs,
                             std::span<const std::string> frameworks,
                             std::span<const std::string> framework_paths,
                             std::string_view user_linker_flags,
                             std::span<const std::string> static_libs = {},
                             StaticStdlib static_stdlib = StaticStdlib::Default);

/// True for libraries of the C runtime family (c, m, pthread, dl, rt, ...) and
/// the C++ runtime: static_deps() keeps them dynamic (glibc does not support
/// mixing a static libc piecemeal; the C++ runtime belongs to static_stdlib()).
/// \ingroup grp_internals
bool is_runtime_lib(std::string_view lib);

/// True when the driver is zig (`zig c++`, `zig cc`, a path to zig).
/// \ingroup grp_internals
bool is_zig(const TargetTriple& t);

/// True for object formats with GNU-ld semantics (-Bstatic, -z relro, -soname,
/// $ORIGIN rpaths): Linux, FreeBSD, Android.
/// \ingroup grp_internals
bool is_elf(Os os);

/// The C driver matching a C++ driver: clang++ -> clang, g++ -> gcc,
/// x86_64-linux-musl-g++ -> x86_64-linux-musl-gcc, `zig c++` -> `zig cc`,
/// paths and version suffixes preserved (clang++-19 -> clang-19). MSVC
/// drivers compile both languages and are returned unchanged.
/// \ingroup grp_internals
std::string c_driver(std::string_view cxx);

/// Arch/OS/ABI parsed from a target triple ("aarch64-linux-musl",
/// "x86_64-unknown-linux-gnu", "x86_64-windows-gnu", "arm64-apple-macos").
/// \ingroup grp_internals
struct ParsedTarget {
    Arch arch {Arch::Unknown}; ///< from the first component
    Os os {Os::Unknown};       ///< first recognized OS component
    std::string abi;           ///< environment component ("gnu", "musl", ...; "" if none)
};
/// Parse a target triple; unknown components stay Unknown / empty.
/// \ingroup grp_internals
ParsedTarget parse_target(std::string_view triple);

/// "<arch>-<os>-<family>" build-dir component, e.g. "x64-linux-clang".
/// \ingroup grp_internals
std::string triple_dir(const TargetTriple& t);

/// Compiler family from a driver name/path basename ("x86_64-w64-mingw32-g++"
/// -> Gcc, "clang-cl" -> Msvc); Unknown when nothing matches.
/// \ingroup grp_internals
CompilerFamily family_from_compiler_name(std::string_view name);

/// Overlay a project Config onto a base triple (compiler family re-detected,
/// archiver follows unless set, linker/stdlib overrides), then validate
/// (g++ + libc++ and MSVC + stdlib() are Config errors).
/// \ingroup grp_internals
Result<TargetTriple> apply_config(const Config& conf, const TargetTriple& base);

/// Compiler-cache launcher resolution: explicit Config::launcher() wins
/// ("" opts out, missing program is a Config error); otherwise auto-detect
/// ccache then sccache. \return the launcher command, "" = none.
/// \ingroup grp_internals
Result<std::string> resolve_launcher(const Config& conf);

/// Materialize a cached tree with hardlinks (copy fallback across
/// filesystems); .git directories are skipped.
/// \ingroup grp_internals
Result<void> copy_tree(const std::filesystem::path& from, const std::filesystem::path& to);

/* --- ninja manifest emitter ----------------------------------------------
 * Append-only buffer producing the build.ninja text. Escaping: values double
 * `$`, paths additionally escape ':' and ' ' (ninja quoting rules). */

/// Streaming writer for the ninja manifest text.
/// \ingroup grp_internals
class NinjaWriter {
public:
    /// `# text` line.
    void comment(std::string_view text);
    /// `name = value` top-level variable.
    void var(std::string_view name, std::string_view value);
    /// `rule name` with body pairs (bodies carry $placeholders verbatim).
    void rule(std::string_view name, const std::vector<std::pair<std::string, std::string>>& body);
    /// One build statement.
    struct Edge {
        std::vector<std::string> outputs;       ///< explicit outputs
        std::vector<std::string> implicit_outs; ///< after `|` on the output side
        std::string rule;                       ///< rule name
        std::vector<std::string> inputs;        ///< explicit inputs
        std::vector<std::string> implicit_ins;  ///< after `|` on the input side
        std::vector<std::pair<std::string, std::string>> vars; ///< per-edge variables
    };
    /// Emit `build outs | implicit_outs: rule ins | implicit_ins` + vars.
    void build(const Edge& e);
    /// Phony target grouping inputs (the `all` target).
    void phony(std::string_view name, std::vector<std::string> ins);
    /// `default` statement (what plain `samu` builds).
    void default_targets(const std::vector<std::string>& names);
    /// The complete manifest text.
    const std::string& str() const { return buf_; }

private:
    std::string buf_;
};

/// Escape a manifest value (`$` -> `$$`).
/// \ingroup grp_internals
std::string esc_value(std::string_view v);
/// Escape a manifest path (`$` -> `$$`, `:` -> `$:`, ' ' -> `$ `).
/// \ingroup grp_internals
std::string esc_path(std::string_view p);

/* --- samurai bridge -------------------------------------------------------
 * Implemented in src/core/samu.cpp over the C shim (vendor/samurai). */

/// Run the vendored samurai build in-process over the given manifest.
/// \ingroup grp_internals
Result<void> samu_build(std::string_view manifest, int jobs, bool verbose,
                        bool dry_run, bool explain, std::span<const std::string> targets);
/// Generate a compile_commands.json from the manifest (compdb task).
/// \ingroup grp_internals
Result<void> samu_compdb(std::string_view manifest, std::string_view outpath);

} // namespace truss::detail

/// \}

