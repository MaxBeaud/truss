// CLI argument parsing, build-script self-rebuild, and compile_commands.json
// export for Project.
module;
#include <atomic>
#include <charconv>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <unistd.h>

module truss;
import :api;
import :impl;

namespace truss {
namespace {

std::string_view cli_mode_dir_name(BuildMode m) {
    return m == BuildMode::Release ? "release" : "debug";
}

/* Path of the running executable: /proc/self/exe, else argv[0] when it is a path. */
std::filesystem::path self_exe(std::span<char* const> argv) {
    std::error_code ec;
    if (auto p = std::filesystem::canonical("/proc/self/exe", ec); !ec)
        return p;
    if (!argv.empty() && argv[0] != nullptr && std::string_view(argv[0]).find('/') != std::string_view::npos)
        if (auto p = std::filesystem::canonical(argv[0], ec); !ec)
            return p;
    return {};
}

/* nob-style self-rebuild: when the build script (truss.cpp next to the
 * executable) or a header/BMI it depends on (truss.d) is newer than the
 * executable, a stale build graph would be generated — run the project's
 * rebuild driver and re-exec with the same argv. The driver is either a
 * `bootstrap.sh` next to the binary (the init template) or, in a truss
 * checkout, `setup.sh build` (bootstrap.sh was folded into setup.sh). With
 * neither, report it (via build()) instead of building the stale graph.
 * TRUSS_NO_REBUILD=1 disables the check; TRUSS_REBUILT guards the re-exec loop. */
std::optional<Error> refresh_build_script(std::span<char* const> argv, bool dry_run) {
    if (const char* off = std::getenv("TRUSS_NO_REBUILD"); off != nullptr && *off != '\0' && *off != '0')
        return std::nullopt;
    const auto exe = self_exe(argv);
    if (exe.empty())
        return std::nullopt;
    const auto dir = exe.parent_path();
    const auto script = dir / "truss.cpp";
    std::error_code ec;
    const auto script_time = std::filesystem::last_write_time(script, ec);
    if (ec)
        return std::nullopt; /* not a build-script executable (tests, installed CLI) */
    const auto exe_time = std::filesystem::last_write_time(exe, ec);
    if (ec)
        return std::nullopt;

    /* build/truss.d (written by bootstrap.sh / `setup.sh build` via -MMD;
     * falls back to legacy root truss.d) lists the headers truss.cpp includes
     * plus the truss BMIs/libtruss.a it was built against; without it, only
     * truss.cpp itself is compared. */
    std::filesystem::path changed;
    if (script_time > exe_time)
        changed = script;
    if (changed.empty()) {
        auto deps = detail::read_file(dir / "build" / "truss.d");
        if (!deps)
            deps = detail::read_file(dir / "truss.d");
        if (deps) {
            for (const auto& dep : detail::parse_depfile(*deps)) {
                std::error_code dep_ec;
                const auto dep_time = std::filesystem::last_write_time(dir / dep, dep_ec);
                if (!dep_ec && dep_time > exe_time) {
                    changed = dir / dep;
                    break;
                }
            }
        }
    }
    if (changed.empty())
        return std::nullopt;

    if (dry_run) {
        /* -n changes nothing: report instead of rebuilding */
        log::warn(changed.filename().string() + " is newer than " + exe.filename().string()
                  + "; a dry run does not rebuild it (run ./truss once, or ./setup.sh build)");
        return std::nullopt;
    }
    if (std::getenv("TRUSS_REBUILT") != nullptr) {
        log::warn(changed.filename().string() + " still looks newer than " + exe.filename().string()
                  + " after a rebuild (clock skew?); continuing");
        return std::nullopt;
    }
    /* rebuild driver, in preference order: a project's bootstrap.sh, then the
     * truss checkout's setup.sh (whose `build` verb is the old bootstrap) */
    const auto bootstrap = dir / "bootstrap.sh";
    const auto setup = dir / "setup.sh";
    std::vector<std::string> cmd;
    std::error_code driver_ec;
    if (std::filesystem::is_regular_file(bootstrap, driver_ec))
        cmd = {"sh", bootstrap.string()};
    else if (std::filesystem::is_regular_file(setup, driver_ec))
        /* setup.sh uses bash arrays, mapfile, pipefail, and [[ ]] */
        cmd = {"bash", setup.string(), "build"};
    if (cmd.empty())
        return Error {
            Error::Kind::Config,
            "build script is stale: " + changed.filename().string() + " is newer than "
                + exe.filename().string()
                + "\nrebuild it first (`./setup.sh build` in a truss checkout, or a bootstrap.sh"
                  " next to the binary), or set TRUSS_NO_REBUILD=1 to use it anyway",
            changed,
        };

    log::info(changed.filename().string() + " changed: rebuilding " + exe.filename().string()
              + " (" + std::filesystem::path(cmd[1]).filename().string()
              + (cmd.size() > 2 ? " " + cmd[2] : "") + ")");
    auto outcome = detail::run_process(cmd, false, dir);
    if (!outcome)
        return outcome.error();
    if (outcome->exit_code != 0)
        return Error {Error::Kind::Command,
                      "rebuilding the build script failed (" + std::filesystem::path(cmd[1]).filename().string()
                          + " exited with " + std::to_string(outcome->exit_code) + ")"};

    /* execv needs a NULL-terminated array; argv spans need not be */
    std::vector<char*> args(argv.begin(), argv.end());
    args.push_back(nullptr);
    ::setenv("TRUSS_REBUILT", "1", 1);
    ::execv(exe.c_str(), args.data());
    return Error {Error::Kind::Io, "re-exec of the rebuilt build script failed: " + std::string(std::strerror(errno))};
}

constexpr std::string_view kHelp =
    "usage: truss [debug|release] [task...] [options] [-- program-args...]\n"
    "\n"
    "modes (default: debug):\n"
    "  debug            -O0 -g, assertions on; outputs in build/debug/\n"
    "  release          optimized, NDEBUG; outputs in build/release/\n"
    "\n"
    "tasks (default: build everything):\n"
    "  test             build and run every test executable\n"
    "  run              build and run an executable (pick one with --target)\n"
    "  clean            remove the mode's build directory\n"
    "  options          list project build options and their current values\n"
    "  configure        interactively edit options (--reset to clear cache)\n"
    "  presets          list registered build presets\n"
    "  install          install targets marked install() to --prefix (default ~/.local)\n"
    "  uninstall        remove files recorded in build/<mode>/install_manifest.txt\n"
    "  compdb           write compile_commands.json\n"
    "  reload           rebuild the truss binary (truss checkout only)\n"
    "  task NAME        build, then run a task registered with proj.task(...)\n"
    "  task list        list the registered tasks with their descriptions\n"
    "\n"
    "options:\n"
    "  -j, --jobs N     parallel jobs (default: CPU count)\n"
    "  -q, --quiet      errors only\n"
    "  -v, --verbose    print full command lines\n"
    "  -n, --dry-run    show what would run without running it\n"
    "      --explain    explain why each step is rebuilt\n"
    "      --target NAME  build (or run) only NAME; repeatable. With subgraphs\n"
    "                      (proj.targets()): SUBGRAPH:NAME picks one subgraph, a\n"
    "                      bare name applies to every subgraph that has it\n"
    "      --for NAME      with subgraphs: restrict build/test/run to one subgraph\n"
    "      --tag TAG...    with test: include tests matching TAG or exclude with !TAG\n"
    "      --build-dir PATH  build root (default: build)\n"
    "  -D KEY[=VAL]     set a build option (persisted in build/<mode>/options.cache)\n"
    "      --preset NAME  apply a named preset registered with proj.preset(...)\n"
    "      --prefix PATH  with install: installation prefix (default: ~/.local)\n"
    "      --strip        with install: strip installed binaries and shared libs\n"
    "      --reset      with configure: clear saved options back to defaults\n"
    "  -V, --version    print the truss version\n"
    "  -h, --help       show this help\n"
    "  --               pass the remaining arguments to the program started by run\n"
    "\n"
    "examples:\n"
    "  truss release run -- --port 8080\n"
    "  truss test -j 8\n"
    "  truss run --target server\n";

} // namespace

Project::Project(std::string name, std::span<char* const> argv) : name_(std::move(name)) {
    detail::log_init_from_env();

    std::error_code ec;
    cwd_ = std::filesystem::current_path(ec);
    if (ec) {
        cwd_ = ".";
        cli_error_ = Error {Error::Kind::Io, "failed to determine current working directory"};
    }

    for (size_t i = 1; i < argv.size(); ++i) {
        const std::string_view a = argv[i];
        if (a == "debug") mode_ = BuildMode::Debug;
        else if (a == "release") mode_ = BuildMode::Release;
        else if (a == "-v" || a == "--verbose") verbose_ = true;
        else if (a == "-q" || a == "--quiet") detail::log_set_min_level(3);
        else if (a == "-n" || a == "--dry-run") dry_run_ = true;
        else if (a == "--explain") explain_ = true;
        else if (a == "-j" || a == "--jobs") {
            if (++i >= argv.size()) { cli_error_ = Error {Error::Kind::Config, "-j requires a number"}; continue; }
            const std::string_view n = argv[i];
            const auto [end, ec_j] = std::from_chars(n.data(), n.data() + n.size(), jobs_);
            if (ec_j != std::errc {} || end != n.data() + n.size() || jobs_ <= 0)
                cli_error_ = Error {Error::Kind::Config, "-j requires a positive number"};
        }
        else if (a == "--target") {
            if (++i >= argv.size()) { cli_error_ = Error {Error::Kind::Config, "--target requires a name"}; continue; }
            only_targets_.emplace_back(argv[i]);
        }
        else if (a == "--for") {
            /* subgraph selector for tasks (test/run); validated in build() */
            if (++i >= argv.size()) { cli_error_ = Error {Error::Kind::Config, "--for requires a subgraph name"}; continue; }
            for_selector_ = argv[i];
        }
        else if (a == "--build-dir") {
            if (++i >= argv.size()) { cli_error_ = Error {Error::Kind::Config, "--build-dir requires a path"}; continue; }
            build_root_ = argv[i];
        }
        else if (a == "--prefix") {
            if (++i >= argv.size()) { cli_error_ = Error {Error::Kind::Config, "--prefix requires a path"}; continue; }
            install_prefix_ = argv[i];
        }
        else if (a == "--strip") {
            install_strip_ = true;
        }
        else if (a == "--reset") {
            reset_options_ = true;
        }
        else if (a == "--preset") {
            if (++i >= argv.size()) { cli_error_ = Error {Error::Kind::Config, "--preset requires a name"}; continue; }
            selected_preset_ = argv[i];
        }
        else if (a == "--tag") {
            auto is_tag_stop = [](std::string_view tok) {
                return tok.starts_with('-')
                    || tok == "debug" || tok == "release"
                    || tok == "build" || tok == "test" || tok == "run" || tok == "clean"
                    || tok == "options" || tok == "configure" || tok == "presets"
                    || tok == "compdb" || tok == "reload" || tok == "install"
                    || tok == "uninstall" || tok == "task" || tok == "help";
            };
            std::size_t added = 0;
            while (i + 1 < argv.size() && !is_tag_stop(argv[i + 1])) {
                const std::string_view tok = argv[++i];
                if (tok == "!") {
                    cli_error_ = Error {Error::Kind::Config, "--tag '!': tag name after '!' cannot be empty"};
                } else {
                    test_tags_.emplace_back(tok);
                }
                ++added;
            }
            if (added == 0)
                cli_error_ = Error {Error::Kind::Config, "--tag requires at least one tag"};
        }
        else if (a == "-D" || a.starts_with("-D")) {
            std::string_view spec = a == "-D" ? std::string_view {} : a.substr(2);
            if (a == "-D") {
                if (++i >= argv.size()) { cli_error_ = Error {Error::Kind::Config, "-D requires KEY=VAL or KEY"}; continue; }
                spec = argv[i];
            }
            if (spec.empty()) {
                cli_error_ = Error {Error::Kind::Config, "-D requires KEY=VAL or KEY"};
                continue;
            }
            const std::size_t eq = spec.find('=');
            if (eq == std::string_view::npos) {
                cli_options_.push_back({std::string(spec), "true", true});
            } else if (eq == 0) {
                cli_error_ = Error {Error::Kind::Config, "-D requires a non-empty option name"};
            } else {
                cli_options_.push_back({std::string(spec.substr(0, eq)), std::string(spec.substr(eq + 1)), false});
            }
        }
        else if (a == "--") {
            /* everything after `--` goes to the program started by `run` */
            for (++i; i < argv.size(); ++i)
                run_args_.emplace_back(argv[i]);
        }
        else if (a == "-h" || a == "--help" || a == "help") {
            std::fputs(kHelp.data(), stdout);
            std::exit(0);
        }
        else if (a == "--version" || a == "-V") {
            std::printf("truss %.*s\n", static_cast<int>(kVersion.size()), kVersion.data());
            std::exit(0);
        }
        else if (a.starts_with('-')) {
            cli_error_ = Error {
                Error::Kind::Config,
                "unknown option '" + std::string(a) + "' (see truss --help)",
            };
        }
        else if (a == "task") {
            /* `task NAME` runs a user task; `task` / `task list` lists them */
            if (i + 1 >= argv.size() || std::string_view(argv[i + 1]) == "list" || std::string_view(argv[i + 1]).starts_with('-')) {
                list_tasks_ = true;
                if (i + 1 < argv.size() && std::string_view(argv[i + 1]) == "list")
                    ++i;
            } else {
                requested_tasks_.push_back("task:" + std::string(argv[++i]));
            }
        }
        else requested_tasks_.emplace_back(a);
    }

    /* self-rebuild last: --help/--version exit above (they need no graph, and
     * must not pay a bootstrap), an invalid option is reported as typed, and
     * -n stays side-effect-free. */
    if (!cli_error_)
        if (auto stale = refresh_build_script(argv, dry_run_))
            cli_error_ = std::move(*stale);
}

const std::vector<std::filesystem::path>& Project::truss_module_paths() const {
    if (!truss_module_paths_loaded_) {
        truss_module_paths_ = os::find_truss_modules();
        truss_module_paths_loaded_ = true;
    }
    return truss_module_paths_;
}

Project::Project(std::string name, int argc, char** argv)
    : Project(std::move(name), std::span<char* const>(argv, static_cast<std::size_t>(argc))) {}

void Project::task(std::string name, std::string description, TaskFn fn) {
    user_tasks_.push_back({std::move(name), std::move(description), std::move(fn)});
}

std::filesystem::path Project::build_dir() const {
    const std::filesystem::path root = build_root_.has_value() ? *build_root_ : cwd_ / "build";
    return root / cli_mode_dir_name(mode_);
}

Result<void> Project::compile_commands(const std::filesystem::path& path) {
    /* -n promises no side effects: a build script that exports the database
     * after build() (truss.cpp does) must not write it in a dry run either */
    if (dry_run_)
        return {};
    auto output_path = path;
    if (std::error_code dir_ec; std::filesystem::is_directory(path, dir_ec))
        output_path = path / "compile_commands.json";

    const std::filesystem::path bld = build_dir();
    const std::filesystem::path manifest = bld / "ninja" / "build.ninja";

    std::error_code ec;
    if (!std::filesystem::exists(manifest, ec)) {
        return std::unexpected(Error {
            Error::Kind::Config,
            "compile_commands requires a built manifest (run build() first)",
            manifest,
        });
    }

    /* Generate into a private temp directory so samurai's fopen() never writes
     * to a predictable sibling name, then publish atomically via
     * write_file_if_changed (O_EXCL|O_NOFOLLOW + rename, mtime-stable). */
    const os::temp_scope scratch;
    if (!scratch.valid())
        return std::unexpected(Error {
            Error::Kind::Io, "failed to create scratch directory for compile_commands.json", output_path});
    const std::filesystem::path tmp = scratch.path() / "compile_commands.json";
    if (auto r = detail::samu_compdb(manifest.generic_string(), tmp.generic_string()); !r)
        return r;
    auto fresh = detail::read_file(tmp);
    if (!fresh)
        return std::unexpected(fresh.error());
    auto existing = detail::read_file(output_path);
    if (existing && *existing == *fresh)
        return {};
    if (auto r = detail::write_file_if_changed(output_path, *fresh); !r)
        return r;

    log::success("wrote " + output_path.generic_string());
    return {};
}

} // namespace truss
