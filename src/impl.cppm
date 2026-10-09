/* truss internal helpers partition. Names here are NOT exported from the
 * primary interface (src/truss.cppm does not re-export this partition), so
 * they are invisible to `import truss;` consumers. */
module;
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module truss:impl;
import :api;

namespace truss::detail {

/* --- logging ------------------------------------------------------------- */
void log_info(std::string_view msg);
void log_warn(std::string_view msg);
void log_success(std::string_view msg);
void log_error(std::string_view msg);
void log_error(const Error& err);
void log_set_verbose(bool v);
bool log_is_verbose();

/* --- filesystem ---------------------------------------------------------- */
Result<std::string> read_file(const std::filesystem::path& p);
Result<void> write_file(const std::filesystem::path& p, std::string_view data);
Result<void> make_dirs(const std::filesystem::path& p);
Result<void> remove_tree(const std::filesystem::path& p);

/* Glob relative to cwd. `*` within a component, `**` across components,
 * `?` one char, `{a,b}` alternation. Hidden entries are skipped. Sorted. */
Result<std::vector<std::filesystem::path>> glob(std::string_view pattern);
bool has_glob_magic(std::string_view pattern);

/* path -> std::string with native separators (forward slashes on unix). */
std::string path_str(const std::filesystem::path& p);

/* --- subprocess ---------------------------------------------------------- */
struct ProcOutcome {
    int exit_code {0};
    std::string output; /* captured stdout+stderr when requested */
};
Result<ProcOutcome> run_process(std::span<const std::string> argv, bool capture);
bool program_exists(std::string_view name);

/* --- compiler flags ------------------------------------------------------ */
struct FlagSet {
    std::string compile; /* flags for compile edges */
    std::string link;    /* flags for link edges */
};

/* Returns std/warnings/debug/optimization/sanitizer/lto/pic/defines flags for
 * GCC and Clang. MSVC returns a Toolchain error (not implemented yet). */
Result<FlagSet> build_flags(const TargetTriple& triple, BuildMode mode,
                            CppStandard std, Warnings warnings, DebugInfo debug,
                            Optimization optimization, Sanitizer sanitizer, Lto lto,
                            bool pic, std::span<const std::string> defines,
                            std::span<const std::string> includes,
                            std::string_view user_flags);
Result<std::string> link_flags(const TargetTriple& triple, Sanitizer sanitizer,
                               Lto lto, std::span<const std::string> link_paths,
                               std::span<const std::string> libs,
                               std::span<const std::string> frameworks,
                               std::span<const std::string> framework_paths,
                               std::string_view user_linker_flags);

/* "<arch>-<os>-<family>" e.g. "x64-linux-clang" */
std::string triple_dir(const TargetTriple& t);

/* --- ninja manifest emitter ---------------------------------------------- */
class NinjaWriter {
public:
    void comment(std::string_view text);
    void var(std::string_view name, std::string_view value);
    void rule(std::string_view name, std::vector<std::pair<std::string, std::string>> body);
    struct Edge {
        std::vector<std::string> outputs;
        std::string rule;
        std::vector<std::string> inputs;       /* explicit */
        std::vector<std::string> implicit_ins; /* after | */
        std::vector<std::pair<std::string, std::string>> vars;
    };
    void build(Edge e);
    void phony(std::string_view name, std::vector<std::string> ins);
    void default_targets(std::vector<std::string> names);
    std::string str() const;

private:
    std::string buf_;
};

/* escape helpers for manifest emission */
std::string esc_value(std::string_view v);   /* $ -> $$ */
std::string esc_path(std::string_view p);    /* $ -> $$, : -> $:, ' ' -> $ */

/* --- samurai bridge -------------------------------------------------------
 * Implemented in src/core/samu.cpp over the C shim (src/samu/shim.c). */
Result<void> samu_build(std::string_view manifest, int jobs, bool verbose,
                        bool dry_run, std::span<const std::string> targets);
Result<void> samu_compdb(std::string_view manifest, std::string_view outpath);

} // namespace truss::detail
