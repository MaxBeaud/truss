module;
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module truss;
import :api;
import :impl;

namespace truss {
namespace {

using detail::NinjaWriter;

constexpr std::array<std::string_view, 7> kPlatformExts {
    ".exe", ".a", ".lib", ".so", ".dll", ".dylib", ".obj",
};

bool ends_with_any(std::string_view s, const auto& exts) {
    return std::ranges::any_of(exts, [&](std::string_view e) { return s.ends_with(e); });
}

std::string_view mode_dir_name(BuildMode m) {
    return m == BuildMode::Release ? "release" : "debug";
}

bool path_is_safe(const std::filesystem::path& p) {
    const std::string s = p.generic_string();
    return s.find_first_of(" \t:$") == std::string::npos;
}

/* A module unit (.cppm) compiled to a BMI + object. */
struct BmiUnit {
    std::string source;       /* relative path */
    std::string module_name;  /* e.g. "math" or "math:part" */
    std::string bmi_path;     /* objdir/bmi/<name>.pcm (':' -> '_') */
    std::string object;       /* objdir/<source>.o */
};

/* A resolved target: everything the emitter needs. */
struct ResolvedTarget {
    Target* target {};
    TargetTriple triple;
    std::vector<std::string> cxx_sources, c_sources; /* relative to cwd */
    std::vector<BmiUnit> bmis;
    std::vector<detail::ModuleUnitInfo> units;       /* all sources, scanned */
    std::string objdir, outdir, output;              /* relative to cwd */
};

/* BMIs visible to a target: its own plus those of depends_on (transitively). */
void collect_bmis(const ResolvedTarget& r, const std::vector<ResolvedTarget>& all,
                  std::map<std::string, std::string>& out, std::set<const Target*>& seen) {
    if (!seen.insert(r.target).second)
        return;
    for (const auto& b : r.bmis)
        out.emplace(b.module_name, b.bmi_path);
    for (const Target* dep : r.target->deps()) {
        const auto it = std::ranges::find(all, dep->name(),
                                          [](const ResolvedTarget& x) -> const std::string& { return x.target->name(); });
        if (it != all.end())
            collect_bmis(*it, all, out, seen);
    }
}

/* Usage requirements a target sees through depends_on (transitively):
 * include dirs from every dependency; defines/link flags only from Interface
 * (header-only / prebuilt package) targets, mirroring CMake INTERFACE semantics
 * so user static libs never leak private defines into consumers. */
struct Usage {
    std::vector<std::string> includes, defines, link_paths, link_libs;
};

void collect_usage(const ResolvedTarget& r, const std::vector<ResolvedTarget>& all,
                   Usage& out, std::set<const Target*>& seen) {
    if (!seen.insert(r.target).second)
        return;
    const bool interface = r.target->kind() == TargetKind::Interface;
    for (const auto& d : r.target->include_dirs())
        if (std::ranges::find(out.includes, d) == out.includes.end())
            out.includes.push_back(d);
    if (interface) {
        for (const auto& d : r.target->defines())
            if (std::ranges::find(out.defines, d) == out.defines.end())
                out.defines.push_back(d);
        for (const auto& p : r.target->link_paths())
            if (std::ranges::find(out.link_paths, p) == out.link_paths.end())
                out.link_paths.push_back(p);
        for (const auto& l : r.target->link_libs())
            if (std::ranges::find(out.link_libs, l) == out.link_libs.end())
                out.link_libs.push_back(l);
    }
    for (const Target* dep : r.target->deps()) {
        const auto it = std::ranges::find(all, dep->name(),
                                          [](const ResolvedTarget& x) -> const std::string& { return x.target->name(); });
        if (it != all.end())
            collect_usage(*it, all, out, seen);
    }
}

Result<std::vector<std::string>> resolve_sources(const Target& t) {
    std::vector<std::string> files;
    for (const auto& pattern : t.source_patterns()) {
        if (!detail::has_glob_magic(pattern)) {
            std::filesystem::path p(pattern);
            std::error_code ec;
            if (!std::filesystem::is_regular_file(p, ec))
                return std::unexpected(Error {
                    Error::Kind::Config,
                    "source file '" + pattern + "' not found for target '" + t.name() + "'",
                    p,
                });
            files.emplace_back(p.generic_string());
            continue;
        }
        auto globbed = detail::glob(pattern);
        if (!globbed)
            return std::unexpected(globbed.error());
        for (const auto& f : *globbed)
            files.push_back(f.generic_string());
    }

    for (const auto& removed : t.removed_sources())
        std::erase(files, removed);
    std::erase_if(files, [](const std::string& f) { return f.empty(); });
    std::ranges::sort(files);
    files.erase(std::ranges::unique(files).begin(), files.end());
    return files;
}

Result<void> validate(const std::vector<std::unique_ptr<Target>>& targets) {
    for (const auto& t : targets) {
        const std::string& name = t->name();
        if (name.empty())
            return std::unexpected(Error {Error::Kind::Config, "target with an empty name"});
        if (name.find_first_of("/\\") != std::string::npos)
            return std::unexpected(Error {
                Error::Kind::Config,
                "target name '" + name + "' must be a bare name, not a path, e.g:\n"
                "  correct:   proj.add_executable(\"app\")\n"
                "  incorrect: proj.add_executable(\"./bin/app\")",
            });
        if (ends_with_any(name, kPlatformExts))
            return std::unexpected(Error {
                Error::Kind::Config,
                "target name '" + name + "' must not carry a platform extension; truss adds it automatically",
            });
        if (!path_is_safe(std::filesystem::path(name)))
            return std::unexpected(Error {Error::Kind::Config, "target name '" + name + "' contains unsupported characters (spaces, ':', '$')"});
    }
    for (size_t i = 0; i < targets.size(); ++i)
        for (size_t j = i + 1; j < targets.size(); ++j)
            if (targets[i]->name() == targets[j]->name())
                return std::unexpected(Error {Error::Kind::Config, "duplicate target name '" + targets[i]->name() + "'"});
    return {};
}

std::string output_path_for(const Target& t, const std::filesystem::path& build_dir, const std::string& triple_dir) {
    const std::string name = t.name();
    switch (t.kind()) {
    case TargetKind::Executable:
        return (build_dir / "bin" / triple_dir / name).generic_string();
    case TargetKind::StaticLibrary:
        return (build_dir / "lib" / triple_dir / ("lib" + name + ".a")).generic_string();
    case TargetKind::SharedLibrary:
        return (build_dir / "lib" / triple_dir / ("lib" + name + ".so")).generic_string();
    case TargetKind::Interface:
        return {}; /* header-only: no artifact */
    }
    return name;
}

Result<void> emit_manifest(const Project& proj, const std::vector<ResolvedTarget>& resolved,
                          const std::filesystem::path& build_dir, NinjaWriter& w) {
    const std::filesystem::path ninja_dir = build_dir / "ninja";

    w.comment("generated by truss " + std::string(kVersion) + " — do not edit");
    w.var("ninja_required_version", "1.3");
    w.var("builddir", ninja_dir.generic_string());

    w.rule("compile_cxx", {
        {"command", "$cxx $flags -MMD -MF $depfile -c $in -o $out"},
        {"deps", "gcc"},
        {"description", "CXX $out"},
    });
    w.rule("compile_c", {
        {"command", "$cc $flags -MMD -MF $depfile -c $in -o $out"},
        {"deps", "gcc"},
        {"description", "CC $out"},
    });
    w.rule("compile_bmi", {
        {"command", "$cxx $flags -fmodule-output=$bmi -MMD -MF $depfile -c $in -o $out"},
        {"deps", "gcc"},
        {"description", "BMI $out"},
    });
    w.rule("archive", {
        {"command", "$ar rcs $out $in"},
        {"description", "AR $out"},
    });
    w.rule("link_exe", {
        {"command", "$cxx -o $out $in $flags $link"},
        {"description", "LINK $out"},
    });
    w.rule("link_shared", {
        {"command", "$cxx -shared -Wl,-soname,$soname -o $out $in $flags $link"},
        {"description", "LINK $out"},
    });

    std::vector<std::string> all_outputs;
    for (const auto& r : resolved) {
        const Target& t = *r.target;
        /* header-only targets contribute usage requirements (via collect_include_dirs)
         * but produce no artifact and no edges */
        if (t.kind() == TargetKind::Interface)
            continue;
        const bool pic = t.pic() || t.kind() != TargetKind::Executable;

        Usage usage;
        std::set<const Target*> usage_seen;
        collect_usage(r, resolved, usage, usage_seen);

        std::vector<std::string> defines = t.defines();
        defines.insert(defines.end(), usage.defines.begin(), usage.defines.end());

        auto cflags = detail::build_flags(r.triple, proj.mode(), t.cpp_standard(), t.warnings(),
                                          t.debug_info(), t.optimization(), t.sanitizer(), t.lto(),
                                          pic, defines, usage.includes, t.flags());
        if (!cflags)
            return std::unexpected(cflags.error());

        /* BMIs visible to this target: own + transitive depends_on */
        std::map<std::string, std::string> bmis;
        std::set<const Target*> seen;
        collect_bmis(r, resolved, bmis, seen);

        const auto unit_for = [&r](const std::string& src) -> const detail::ModuleUnitInfo* {
            for (const auto& u : r.units)
                if (u.path == src)
                    return &u;
            return nullptr;
        };

        /* compile edges */
        std::vector<std::string> objects;
        for (const auto& b : r.bmis) {
            auto imp = detail::resolve_module_imports(*unit_for(b.source), bmis);
            if (!imp)
                return std::unexpected(imp.error());
            NinjaWriter::Edge e;
            e.outputs = {b.object};
            /* the .pcm is produced by -fmodule-output: declare it as an implicit
             * output so samu can order consumers against it */
            e.implicit_outs = {b.bmi_path};
            e.rule = "compile_bmi";
            e.inputs = {b.source};
            e.implicit_ins = imp->second;
            std::string flags = cflags->compile;
            if (!imp->first.empty())
                flags += " " + imp->first;
            e.vars = {
                {"cxx", r.triple.compiler},
                {"flags", flags},
                {"bmi", b.bmi_path},
                {"depfile", b.object + ".d"},
            };
            w.build(std::move(e));
            objects.push_back(b.object);
        }
        for (const auto& src : r.cxx_sources) {
            auto imp = detail::resolve_module_imports(*unit_for(src), bmis);
            if (!imp)
                return std::unexpected(imp.error());
            const std::string obj = r.objdir + "/" + src + ".o";
            NinjaWriter::Edge e;
            e.outputs = {obj};
            e.rule = "compile_cxx";
            e.inputs = {src};
            e.implicit_ins = imp->second;
            std::string flags = cflags->compile;
            if (!imp->first.empty())
                flags += " " + imp->first;
            e.vars = {
                {"cxx", r.triple.compiler},
                {"flags", flags},
                {"depfile", obj + ".d"},
            };
            w.build(std::move(e));
            objects.push_back(obj);
        }
        for (const auto& src : r.c_sources) {
            const std::string obj = r.objdir + "/" + src + ".o";
            NinjaWriter::Edge e;
            e.outputs = {obj};
            e.rule = "compile_c";
            e.inputs = {src};
            e.vars = {
                {"cc", r.triple.compiler == "clang++" ? "clang" : r.triple.compiler == "g++" ? "gcc" : r.triple.compiler},
                {"flags", cflags->compile_c},
                {"depfile", obj + ".d"},
            };
            w.build(std::move(e));
            objects.push_back(obj);
        }

        /* dependency outputs (static archives / shared libs) */
        std::vector<std::string> dep_outputs;
        bool has_shared_dep = false;
        for (const Target* dep : t.deps()) {
            const auto it = std::ranges::find(resolved, dep->name(), [](const ResolvedTarget& x) -> const std::string& { return x.target->name(); });
            if (it == resolved.end())
                return std::unexpected(Error {
                    Error::Kind::Config,
                    "target '" + t.name() + "' depends on unknown target '" + dep->name() + "'",
                });
            if (dep->kind() == TargetKind::Interface)
                continue; /* header-only: no artifact to link */
            dep_outputs.push_back(it->output);
            if (dep->kind() == TargetKind::SharedLibrary)
                has_shared_dep = true;
        }

        if (t.kind() == TargetKind::StaticLibrary) {
            NinjaWriter::Edge e;
            e.outputs = {r.output};
            e.rule = "archive";
            e.inputs = std::move(objects);
            e.vars = {{"ar", r.triple.ar}};
            w.build(std::move(e));
        } else {
            std::vector<std::string> extra_link;
            if (has_shared_dep) {
                /* exe lives in bin/<tdir>, shared libs in lib/<tdir>;
                 * single-quote $ORIGIN so the shell passes it through to ld. */
                extra_link.push_back("-Wl,-rpath,'$ORIGIN'/../../lib/" + detail::triple_dir(r.triple));
            }
            std::vector<std::string> link_paths = t.link_paths();
            link_paths.insert(link_paths.end(), usage.link_paths.begin(), usage.link_paths.end());
            std::vector<std::string> link_libs = t.link_libs();
            link_libs.insert(link_libs.end(), usage.link_libs.begin(), usage.link_libs.end());
            auto link_res = detail::link_flags(r.triple, t.sanitizer(), t.lto(), link_paths,
                                               link_libs, t.frameworks(), t.framework_paths(),
                                               t.linker_flags());
            if (!link_res)
                return std::unexpected(link_res.error());
            std::string link = *link_res;
            for (const auto& x : extra_link) link += " " + x;

            NinjaWriter::Edge e;
            e.outputs = {r.output};
            e.rule = t.kind() == TargetKind::SharedLibrary ? "link_shared" : "link_exe";
            e.inputs = std::move(objects);
            /* samu's $in only carries explicit inputs, so dependency artifacts
             * (archives / shared libs) must be explicit, after the objects. */
            e.inputs.insert(e.inputs.end(), dep_outputs.begin(), dep_outputs.end());
            e.vars = {
                {"cxx", r.triple.compiler},
                {"flags", t.sanitizer() != Sanitizer::Default || t.lto() != Lto::Default ? link : std::string()},
                {"link", link},
            };
            if (t.kind() == TargetKind::SharedLibrary) {
                const std::string soname = std::filesystem::path(r.output).filename().generic_string();
                e.vars.emplace_back("soname", soname);
            }
            w.build(std::move(e));
        }
        all_outputs.push_back(r.output);
    }

    w.phony("all", std::move(all_outputs));

    if (proj.only_targets().empty()) {
        w.default_targets({"all"});
    } else {
        std::vector<std::string> selected;
        for (const auto& name : proj.only_targets()) {
            const auto it = std::ranges::find(resolved, name, [](const ResolvedTarget& x) -> const std::string& { return x.target->name(); });
            if (it == resolved.end())
                return std::unexpected(Error {Error::Kind::Config, "unknown --target '" + name + "'"});
            selected.push_back(it->output);
        }
        w.default_targets(std::move(selected));
    }
    return {};
}

} // namespace

/* --- Target ------------------------------------------------------------- */

Target& Target::sources_in(std::string_view dir) {
    std::string d(dir);
    while (!d.empty() && (d.back() == '/' || d.back() == '\\'))
        d.pop_back();
    source_patterns_.push_back(d + "/**.cpp");
    source_patterns_.push_back(d + "/**.cc");
    source_patterns_.push_back(d + "/**.cxx");
    source_patterns_.push_back(d + "/**.c");
    source_patterns_.push_back(d + "/**.cppm");
    return *this;
}

Target& Target::remove_source(std::string_view path) {
    removed_sources_.emplace_back(path);
    return *this;
}

Target& Target::depends_on(const Target& dep) {
    deps_.push_back(&dep);
    return *this;
}

/* --- Project ------------------------------------------------------------ */

Project::Project(std::string name, std::span<char* const> argv) : name_(std::move(name)) {
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
        else if (a == "-n" || a == "--dry-run") dry_run_ = true;
        else if (a == "-j" || a == "--jobs") {
            if (++i >= argv.size()) { cli_error_ = Error {Error::Kind::Config, "-j requires a number"}; continue; }
            jobs_ = std::atoi(argv[i]);
        }
        else if (a == "--target") {
            if (++i >= argv.size()) { cli_error_ = Error {Error::Kind::Config, "--target requires a name"}; continue; }
            only_targets_.emplace_back(argv[i]);
        }
        else if (a == "--build-dir") {
            if (++i >= argv.size()) { cli_error_ = Error {Error::Kind::Config, "--build-dir requires a path"}; continue; }
            build_root_ = argv[i];
        }
        else if (a.starts_with('-')) {
            cli_error_ = Error {
                Error::Kind::Config,
                "unknown option '" + std::string(a) + "'\n"
                "usage: truss [debug|release] [task...] [-j N] [-v] [-n] [--target NAME] [--build-dir PATH]\n"
                "tasks: test, run, clean, compdb, or any task defined with proj.task(...)",
            };
        }
        else requested_tasks_.emplace_back(a);
    }

    detail::log_set_verbose(verbose_);
}

Project::Project(std::string name, int argc, char** argv)
    : Project(std::move(name), std::span<char* const>(argv, static_cast<std::size_t>(argc))) {}

Executable& Project::add_executable(std::string name) {
    auto t = std::make_unique<Executable>(std::move(name));
    auto& ref = *t;
    targets_.push_back(std::move(t));
    return ref;
}

StaticLib& Project::add_static_library(std::string name) {
    auto t = std::make_unique<StaticLib>(std::move(name));
    auto& ref = *t;
    targets_.push_back(std::move(t));
    return ref;
}

SharedLib& Project::add_shared_library(std::string name) {
    auto t = std::make_unique<SharedLib>(std::move(name));
    auto& ref = *t;
    targets_.push_back(std::move(t));
    return ref;
}

Interface& Project::add_interface_library(std::string name) {
    auto t = std::make_unique<Interface>(std::move(name));
    auto& ref = *t;
    targets_.push_back(std::move(t));
    return ref;
}

void Project::task(std::string name, TaskFn fn) {
    user_tasks_.emplace_back(std::move(name), std::move(fn));
}

std::filesystem::path Project::build_dir() const {
    const std::filesystem::path root = build_root_.has_value() ? *build_root_ : cwd_ / "build";
    return root / mode_dir_name(mode_);
}

Result<void> Project::build() {
    const auto start = std::chrono::steady_clock::now();
    if (cli_error_) {
        detail::log_error(*cli_error_);
        return std::unexpected(*cli_error_);
    }

    const std::filesystem::path bld = build_dir();
    if (!path_is_safe(cwd_) || !path_is_safe(bld))
        return std::unexpected(Error {
            Error::Kind::Config,
            "paths containing spaces or ':' are not supported yet (project root or build dir)",
            cwd_,
        });

    if (auto r = validate(targets_); !r) {
        detail::log_error(r.error());
        return std::unexpected(r.error());
    }

    /* user tasks must not shadow built-ins */
    for (const auto& [name, fn] : user_tasks_)
        if (name == "test" || name == "run" || name == "clean" || name == "compdb")
            return std::unexpected(Error {Error::Kind::Config, "user task '" + name + "' shadows a built-in task"});

    /* resolve + validate sources */
    std::vector<ResolvedTarget> resolved;
    resolved.reserve(targets_.size());
    for (const auto& t : targets_) {
        ResolvedTarget r;
        r.target = t.get();
        r.triple = t->triple_override().value_or(host_triple());
        if (r.triple.family == CompilerFamily::Msvc)
            return std::unexpected(Error {Error::Kind::Toolchain, "MSVC targets are not supported yet"});

        auto sources = resolve_sources(*t);
        if (!sources) {
            detail::log_error(sources.error());
            return std::unexpected(sources.error());
        }
        if (sources->empty() && t->kind() != TargetKind::Interface)
            return std::unexpected(Error {
                Error::Kind::Config,
                "target '" + t->name() + "' has no sources; add some with .sources(...) or .sources_in(...)",
            });

        const ModulesMode mm =
            t->modules_mode() == ModulesMode::Default ? ModulesMode::Auto : t->modules_mode();
        if (mm == ModulesMode::Manual)
            return std::unexpected(Error {
                Error::Kind::Config,
                "modules_mode(Manual) is not implemented yet — use Auto (conservative BMI wiring)",
            });

        const std::string tdir = detail::triple_dir(r.triple);
        r.objdir = (bld / "obj" / tdir / t->name()).generic_string();

        for (const auto& f : *sources) {
            const bool is_bmi = f.ends_with(".cppm") || f.ends_with(".ixx");
            if (is_bmi && mm == ModulesMode::Off)
                return std::unexpected(Error {
                    Error::Kind::Config,
                    "'" + f + "' is a module interface unit, but target '" + t->name() +
                        "' has modules_mode(Off)",
                    std::filesystem::path(f),
                });
            if (is_bmi) {
                auto info = detail::scan_module_unit(f);
                if (!info)
                    return std::unexpected(info.error());
                if (info->module_name.empty())
                    return std::unexpected(Error {
                        Error::Kind::Config,
                        "no 'export module <name>;' declaration found in '" + f + "'",
                        std::filesystem::path(f),
                    });
                info->is_bmi_source = true;
                std::string flat = info->module_name;
                std::replace(flat.begin(), flat.end(), ':', '_');
                BmiUnit b;
                b.source = f;
                b.module_name = info->module_name;
                b.bmi_path = r.objdir + "/bmi/" + flat + ".pcm";
                b.object = r.objdir + "/" + f + ".o";
                r.units.push_back(std::move(*info));
                r.bmis.push_back(std::move(b));
            } else if (f.ends_with(".cpp") || f.ends_with(".cc") || f.ends_with(".cxx")) {
                auto info = detail::scan_module_unit(f);
                if (!info)
                    return std::unexpected(info.error());
                r.cxx_sources.push_back(f);
                r.units.push_back(std::move(*info));
            } else if (f.ends_with(".c")) {
                r.c_sources.push_back(f);
            } else {
                return std::unexpected(Error {Error::Kind::Config, "unsupported source file '" + f + "'", std::filesystem::path(f)});
            }
        }

        r.output = output_path_for(*t, bld, tdir);
        r.outdir = std::filesystem::path(r.output).parent_path().generic_string();
        resolved.push_back(std::move(r));
    }

    /* generate the manifest */
    NinjaWriter w;
    if (auto r = emit_manifest(*this, resolved, bld, w); !r) {
        detail::log_error(r.error());
        return std::unexpected(r.error());
    }
    const std::filesystem::path manifest = bld / "ninja" / "build.ninja";
    if (auto r = detail::write_file(manifest, w.str()); !r) {
        detail::log_error(r.error());
        return std::unexpected(r.error());
    }

    /* dispatch tasks */
    const TaskContext ctx {name_, mode_, bld, cwd_, jobs_, verbose_};
    std::vector<std::string> tasks = requested_tasks_;
    if (tasks.empty())
        tasks.push_back("build");

    for (const auto& task : tasks) {
        Result<void> step = [&]() -> Result<void> {
            if (task == "clean")
                return detail::remove_tree(bld);

            if (task == "build")
                return detail::samu_build(manifest.generic_string(), jobs_, verbose_, dry_run_, {});

            if (task == "compdb") {
                auto r = detail::samu_compdb(manifest.generic_string(), (bld / "compile_commands.json").generic_string());
                if (r) detail::log_success("wrote " + (bld / "compile_commands.json").generic_string());
                return r;
            }

            if (task == "test") {
                std::vector<std::string> test_outputs;
                for (const auto& r : resolved)
                    if (r.target->is_test_target())
                        test_outputs.push_back(r.output);
                if (test_outputs.empty()) {
                    detail::log_warn("no targets marked is_test(); nothing to run");
                    return {};
                }
                if (auto r = detail::samu_build(manifest.generic_string(), jobs_, verbose_, dry_run_, test_outputs); !r)
                    return r;
                int failures = 0;
                for (const auto& exe : test_outputs) {
                    detail::log_info("running " + exe);
                    const std::array argv {exe};
                    auto outcome = detail::run_process(argv, false);
                    if (!outcome)
                        return std::unexpected(outcome.error());
                    if (outcome->exit_code != 0) {
                        detail::log_error("test '" + exe + "' failed with exit code " + std::to_string(outcome->exit_code));
                        ++failures;
                    }
                }
                if (failures)
                    return std::unexpected(Error {Error::Kind::Command, std::to_string(failures) + " test(s) failed"});
                detail::log_success("all tests passed");
                return {};
            }

            if (task == "run") {
                std::vector<std::string> exe_outputs;
                for (const auto& r : resolved)
                    if (r.target->kind() == TargetKind::Executable && !r.target->is_test_target())
                        exe_outputs.push_back(r.output);
                if (exe_outputs.empty())
                    return std::unexpected(Error {Error::Kind::Config, "no executable to run (test executables are excluded)"});
                if (auto r = detail::samu_build(manifest.generic_string(), jobs_, verbose_, dry_run_, exe_outputs); !r)
                    return r;
                const std::array argv {exe_outputs.front()};
                auto outcome = detail::run_process(argv, false);
                if (!outcome)
                    return std::unexpected(outcome.error());
                if (outcome->exit_code != 0)
                    return std::unexpected(Error {Error::Kind::Command, "'" + exe_outputs.front() + "' exited with " + std::to_string(outcome->exit_code)});
                return {};
            }

            const auto it = std::ranges::find(user_tasks_, task, [](const auto& p) -> const std::string& { return p.first; });
            if (it != user_tasks_.end())
                return it->second(ctx);

            return std::unexpected(Error {
                Error::Kind::Config,
                "unknown task '" + task + "' (built-ins: test, run, clean, compdb)",
            });
        }();

        if (!step) {
            detail::log_error(step.error());
            return std::unexpected(step.error());
        }
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    detail::log_info("truss: build took " + std::to_string(elapsed) + "ms");
    return {};
}

} // namespace truss
