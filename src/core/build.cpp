module;
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <unistd.h>

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
    std::string objdir, output;                      /* relative to cwd */
    std::string implib;                              /* Windows import lib for shared libs */
    std::string static_twin;                         /* shared lib archived for static_deps() consumers */
    bool needs_std {};                               /* some unit does `import std;` */
    bool needs_std_compat {};                        /* some unit does `import std.compat;` */
};

/* name -> resolved target, built once per build (avoids repeated linear scans) */
using TargetIndex = std::map<std::string_view, const ResolvedTarget*, std::less<>>;

/* BMIs visible to a target: its own plus those of depends_on (transitively). */
void collect_bmis(const ResolvedTarget& r, const TargetIndex& idx,
                  std::map<std::string, std::string, std::less<>>& out,
                  std::map<std::string, std::vector<std::string>, std::less<>>& module_imports,
                  std::set<const Target*>& seen) {
    if (!seen.insert(r.target).second)
        return;
    for (const auto& b : r.bmis)
        out.emplace(b.module_name, b.bmi_path);
    /* per-module import lists drive the transitive BMI closure */
    for (const auto& u : r.units)
        if (u.is_bmi_source)
            module_imports.emplace(u.module_name, u.imports);
    for (const Target* dep : r.target->deps()) {
        const auto it = idx.find(dep->name());
        if (it != idx.end())
            collect_bmis(*it->second, idx, out, module_imports, seen);
    }
}

/* `import std;` / `import std.compat;`: libc++ ships the module sources, and
 * one clang call builds both the BMI and its object. Built once per
 * (toolchain, -std) and shared by every target that imports it. */
struct StdModule {
    std::string pcm, obj, compat_pcm, compat_obj;
};

struct StdManifest {
    std::string std_source, compat_source;
    std::vector<std::string> system_dirs;
};

/* -std flag for the std module; the guard has already ensured >= C++23 */
std::string_view std_module_flag(CppStandard s) {
    return s == CppStandard::Cpp26 ? "-std=c++26" : "-std=c++23";
}

/* Ask clang for libc++'s module manifest (empty for g++, zig, libstdc++).
 * Paths in it are relative to the manifest's directory. Cached per query. */
Result<StdManifest> discover_std_manifest(const std::string& compiler, std::string_view std_flag) {
    static std::mutex mutex;
    static std::map<std::string, StdManifest, std::less<>> cache;
    const std::string key = compiler + ' ' + std::string(std_flag);
    const std::scoped_lock lock(mutex);
    if (const auto it = cache.find(key); it != cache.end())
        return it->second;

    const auto fail = [&](std::string why) {
        return std::unexpected(Error {
            Error::Kind::Toolchain,
            "`import std;` needs libc++'s module manifest, but " + why + " (compiler: '" + compiler +
                "'); install libc++ with its module sources (e.g. libc++-dev / libc++ 18+)",
        });
    };
    const std::vector<std::string> argv {compiler, std::string(std_flag), "-stdlib=libc++",
                                         "-print-library-module-manifest-path"};
    auto out = detail::run_process_stdout(argv);
    if (!out || out->exit_code != 0)
        return fail("`" + compiler + " -print-library-module-manifest-path` failed");
    std::string path = out->output;
    while (!path.empty() && std::isspace(static_cast<unsigned char>(path.back())))
        path.pop_back();
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(path, ec))
        return fail("the compiler reports no manifest ('" + path + "')");

    const std::filesystem::path manifest(path);
    const std::filesystem::path base = manifest.parent_path();
    const auto text = detail::read_file(manifest);
    if (!text)
        return std::unexpected(text.error());
    const auto j = nlohmann::json::parse(*text, nullptr, false);
    if (j.is_discarded() || !j.contains("modules") || !j["modules"].is_array())
        return fail("'" + path + "' is not a module manifest");

    StdManifest m;
    for (const auto& mod : j["modules"]) {
        const std::string name = mod.value("logical-name", "");
        if (name != "std" && name != "std.compat")
            continue;
        const std::string src =
            std::filesystem::weakly_canonical(base / mod.value("source-path", "")).generic_string();
        (name == "std" ? m.std_source : m.compat_source) = src;
        if (name == "std" && mod.contains("local-arguments"))
            for (const auto& d : mod["local-arguments"].value("system-include-directories",
                                                              nlohmann::json::array()))
                m.system_dirs.push_back(
                    std::filesystem::weakly_canonical(base / d.get<std::string>()).generic_string());
    }
    if (m.std_source.empty())
        return fail("'" + path + "' has no `std` module");
    return cache.emplace(key, std::move(m)).first->second;
}

/* Usage requirements a target sees through depends_on (transitively):
 * include dirs from every dependency; defines/link flags only from Interface
 * (header-only / prebuilt package) targets, mirroring CMake INTERFACE semantics
 * so user static libs never leak private defines into consumers. */
struct Usage {
    std::vector<std::string> includes, defines, link_paths, link_libs;
    std::vector<std::string> system_includes, flags; /* flags: public_flags() of self + deps */
    std::vector<std::string> static_link_libs; /* from Interface deps marked static_link() */
};

/* One walk per consumer; the dedupe sets live for the whole walk (rebuilding
 * them from `out` at every recursion level made deep graphs quadratic). */
class UsageWalk {
public:
    explicit UsageWalk(const TargetIndex& idx) : idx_(idx) {}

    Usage run(const ResolvedTarget& r) {
        visit(r);
        return std::move(out_);
    }

private:
    static void add(std::vector<std::string>& v, std::unordered_set<std::string>& seen, const std::string& x) {
        if (seen.insert(x).second)
            v.push_back(x);
    }

    void visit(const ResolvedTarget& r) {
        if (!visited_.insert(r.target).second)
            return;
        for (const auto& d : r.target->include_dirs())
            add(out_.includes, includes_, d);
        for (const auto& d : r.target->system_include_dirs())
            add(out_.system_includes, system_includes_, d);
        for (const auto& d : r.target->public_defines())
            add(out_.defines, defines_, d);
        for (const auto& f : r.target->public_flags())
            add(out_.flags, flags_, f);
        if (r.target->kind() == TargetKind::Interface) {
            for (const auto& d : r.target->defines())
                add(out_.defines, defines_, d);
            for (const auto& p : r.target->link_paths())
                add(out_.link_paths, link_paths_, p);
            auto& libs = r.target->static_link_enabled() ? out_.static_link_libs : out_.link_libs;
            for (const auto& l : r.target->link_libs())
                add(libs, link_libs_, l);
        }
        for (const Target* dep : r.target->deps())
            if (const auto it = idx_.find(dep->name()); it != idx_.end())
                visit(*it->second);
    }

    const TargetIndex& idx_;
    Usage out_;
    std::set<const Target*> visited_;
    std::unordered_set<std::string> includes_, system_includes_, defines_, flags_, link_paths_, link_libs_;
};

Result<std::vector<std::string>> resolve_sources(const Target& t) {
    std::vector<std::string> files;
    std::vector<std::string> magic;
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
        } else {
            magic.push_back(pattern);
        }
    }
    /* one tree walk per scan root for all glob patterns (sources_in adds six) */
    if (!magic.empty()) {
        auto globbed = detail::glob_multi(magic);
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
        if (const auto& err = t->deferred_error(); err) {
            Error e = *err;
            e.message = "target '" + name + "': depends_on() was given a failed result:\n" + e.message;
            return std::unexpected(std::move(e));
        }
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

constexpr std::string_view obj_ext(CompilerFamily family) {
    return family == CompilerFamily::Msvc ? ".obj" : ".o";
}

/* Artifact naming follows the target OS (a zig/clang cross build to
 * windows-gnu still wants app.exe), the lib prefix/.lib suffix the family. */
std::string output_path_for(const Target& t, const std::filesystem::path& build_dir,
                            const std::string& triple_dir, const TargetTriple& triple) {
    const std::string& name = t.name();
    const bool msvc = triple.family == CompilerFamily::Msvc;
    const bool windows = msvc || triple.os == Os::Windows;
    switch (t.kind()) {
    case TargetKind::Executable:
        return (build_dir / "bin" / triple_dir / (name + (windows ? ".exe" : ""))).generic_string();
    case TargetKind::StaticLibrary:
        return (build_dir / "lib" / triple_dir / (msvc ? name + ".lib" : "lib" + name + ".a")).generic_string();
    case TargetKind::SharedLibrary: {
        /* Windows has no rpath: a dll must sit next to the executables that
         * load it (bin/), only its import lib goes to lib/ (import_lib_path) */
        const std::string_view ext = windows ? ".dll" : triple.os == Os::MacOS ? ".dylib" : ".so";
        return (build_dir / (windows ? "bin" : "lib") / triple_dir /
                ((msvc ? name : "lib" + name) + std::string(ext))).generic_string();
    }
    case TargetKind::Interface:
        return {}; /* header-only: no artifact */
    }
    return name;
}

/* Windows shared libraries come with an import lib in lib/ (the dll itself
 * lives in bin/); consumers link against the import lib, never the dll.
 * MSVC: <name>.lib, MinGW (zig/clang *-windows-gnu): lib<name>.dll.a. */
std::string import_lib_path(const Target& t, const std::filesystem::path& build_dir,
                            const std::string& triple_dir, const TargetTriple& triple) {
    const bool msvc = triple.family == CompilerFamily::Msvc;
    return (build_dir / "lib" / triple_dir /
            (msvc ? t.name() + ".lib" : "lib" + t.name() + ".dll.a")).generic_string();
}

Result<void> emit_manifest(const Project& proj, std::vector<ResolvedTarget>& resolved,
                          const std::filesystem::path& build_dir, NinjaWriter& w) {
    const std::filesystem::path ninja_dir = build_dir / "ninja";

    TargetIndex by_name;
    for (const auto& r : resolved)
        by_name[std::string_view(r.target->name())] = &r;

    /* std / std.compat edges: one pair per (triple dir, -std), emitted on first use */
    struct StdEntry {
        StdModule paths;
        StdManifest manifest;
        std::string flags;
        bool compat_emitted {};
    };
    std::map<std::string, StdEntry, std::less<>> std_modules;
    const auto std_for = [&](const ResolvedTarget& r, bool compat) -> Result<StdModule> {
        const std::string_view flag = std_module_flag(r.target->cpp_standard());
        const std::string key = detail::triple_dir(r.triple) + "-" + std::string(flag.substr(5));
        const auto emit = [&](const StdEntry& en, const std::string& src, const std::string& obj,
                              const std::string& pcm, std::string_view extra, std::vector<std::string> ins) {
            NinjaWriter::Edge e;
            e.outputs = {obj};
            e.implicit_outs = {pcm};
            e.rule = "compile_bmi";
            e.inputs = {src};
            e.implicit_ins = std::move(ins);
            e.vars = {
                {"cxx", r.triple.compiler},
                {"flags", en.flags + std::string(extra)},
                {"bmi", pcm},
                {"depfile", obj + ".d"},
            };
            w.build(std::move(e));
        };
        auto it = std_modules.find(key);
        if (it == std_modules.end()) {
            auto man = discover_std_manifest(r.triple.compiler, flag);
            if (!man)
                return std::unexpected(man.error());
            const std::string dir = (build_dir / "std" / key).generic_string();
            if (auto mk = detail::make_dirs(dir); !mk)
                return std::unexpected(mk.error());
            StdEntry en;
            en.paths = {dir + "/std.pcm", dir + "/std.o", dir + "/std.compat.pcm", dir + "/std.compat.o"};
            en.flags = std::string(flag) + " -stdlib=libc++";
            if (!r.triple.target.empty())
                en.flags += " --target=" + r.triple.target;
            en.flags += " -fPIC -Wno-reserved-module-identifier";
            for (const auto& d : man->system_dirs)
                en.flags += " -isystem \"" + d + "\"";
            en.manifest = std::move(*man);
            emit(en, en.manifest.std_source, en.paths.obj, en.paths.pcm, "", {});
            it = std_modules.emplace(key, std::move(en)).first;
        }
        StdEntry& en = it->second;
        if (compat && !en.compat_emitted) {
            if (en.manifest.compat_source.empty())
                return std::unexpected(Error {
                    Error::Kind::Toolchain, "libc++'s module manifest has no `std.compat` module",
                });
            emit(en, en.manifest.compat_source, en.paths.compat_obj, en.paths.compat_pcm,
                 " -fmodule-file=std=" + en.paths.pcm, {en.paths.pcm});
            en.compat_emitted = true;
        }
        return en.paths;
    };

    w.comment("generated by truss " + std::string(kVersion) + " — do not edit");
    w.var("ninja_required_version", "1.3");
    w.var("builddir", ninja_dir.generic_string());

    /* compiler cache (ccache/sccache): prefixed into compile commands rather
     * than ninja's RULE_LAUNCH_COMPILE special var — samurai support for it
     * is unverified, the command string always works. MSVC rules stay raw. */
    auto launcher = detail::resolve_launcher(proj.config());
    if (!launcher)
        return std::unexpected(launcher.error());
    /* ccache/sccache do not recognize `zig c++` as a compiler: no launcher
     * when any target compiles with zig (rules are shared by all targets) */
    if (std::ranges::any_of(resolved, [](const ResolvedTarget& r) { return detail::is_zig(r.triple); }))
        launcher->clear();
    const std::string launch = launcher->empty()
        ? std::string {}
        : (launcher->find(' ') == std::string::npos ? *launcher : "'" + *launcher + "'") + " ";

    w.rule("compile_cxx", {
        {"command", launch + "$cxx $flags -MMD -MF $depfile -c $in -o $out"},
        {"deps", "gcc"},
        {"description", "CXX $out"},
    });
    w.rule("compile_c", {
        {"command", launch + "$cc $flags -MMD -MF $depfile -c $in -o $out"},
        {"deps", "gcc"},
        {"description", "CC $out"},
    });
    w.rule("compile_bmi", {
        /* no launcher: ccache's C++20 module (BMI) support is unreliable */
        {"command", "$cxx $flags -fmodule-output=$bmi -MMD -MF $depfile -c $in -o $out"},
        {"deps", "gcc"},
        {"description", "BMI $out"},
    });
    w.rule("archive", {
        {"command", "$ar rcs $out $in"},
        {"description", "AR $out"},
    });
    if (std::ranges::any_of(resolved, [](const ResolvedTarget& r) { return !r.target->merged_libraries().empty(); }))
        w.rule("merge_archive", {
            /* combine several .a into one via an MRI script ($mri) on stdin;
             * GNU ar / llvm-ar / zig ar all accept -M */
            {"command", "$ar -M < $mri"},
            {"description", "AR(merge) $out"},
        });
    w.rule("link_exe", {
        {"command", "$cxx -o $out $in $flags $link"},
        {"description", "LINK $out"},
    });
    w.rule("link_shared", {
        /* $soname_flag: -Wl,-soname (ELF), -Wl,-install_name (macOS), -Wl,--out-implib (MinGW), "" (MSVC) */
        {"command", "$cxx -shared $soname_flag -o $out $in $flags $link"},
        {"description", "LINK $out"},
    });

    const bool any_msvc = std::ranges::any_of(resolved, [](const ResolvedTarget& r) {
        return r.triple.family == CompilerFamily::Msvc;
    });
    if (std::ranges::any_of(resolved, [](const ResolvedTarget& r) { return !r.target->pch().empty(); }))
        w.rule("compile_pch", {
            /* no launcher: ccache's PCH support is unreliable */
            {"command", "$cxx $flags -x c++-header -MMD -MF $depfile -c $in -o $out"},
            {"deps", "gcc"},
            {"description", "PCH $out"},
        });
    if (any_msvc) {
        /* header deps via /showIncludes: the vendored samurai parses these
         * lines (deps = msvc, see vendor/samurai TRUSS-VENDOR.txt) */
        w.var("msvc_deps_prefix", "Note: including file:");
        w.rule("compile_cxx_msvc", {
            {"command", "$cxx $flags /showIncludes /c /Fo$out $in"},
            {"deps", "msvc"},
            {"description", "CXX $out"},
        });
        w.rule("compile_c_msvc", {
            {"command", "$cc $flags /showIncludes /c /Fo$out $in"},
            {"deps", "msvc"},
            {"description", "CC $out"},
        });
        /* object lists go through response files: Windows command lines are short */
        w.rule("archive_msvc", {
            {"command", "$ar /NOLOGO /OUT:$out @$out.rsp"},
            {"rspfile", "$out.rsp"},
            {"rspfile_content", "$in"},
            {"description", "AR $out"},
        });
        w.rule("link_exe_msvc", {
            {"command", "$cxx $flags @$out.rsp /link /OUT:$out $link"},
            {"rspfile", "$out.rsp"},
            {"rspfile_content", "$in"},
            {"description", "LINK $out"},
        });
        w.rule("link_shared_msvc", {
            {"command", "$cxx /LD $flags @$out.rsp /link /OUT:$out $link"},
            {"rspfile", "$out.rsp"},
            {"rspfile_content", "$in"},
            {"description", "LINK $out"},
        });
    }

    /* static twins: a SharedLib reached (transitively) by a static_deps()
     * consumer also gets an archive over the same PIC objects — linked
     * statically there, no recompilation */
    {
        std::set<const Target*> twins, walked;
        const auto mark = [&](this const auto& self, const Target& t) -> void {
            for (const Target* dep : t.deps()) {
                if (!walked.insert(dep).second)
                    continue;
                if (dep->kind() == TargetKind::SharedLibrary)
                    twins.insert(dep);
                self(*dep);
            }
        };
        for (const auto& r : resolved)
            if (r.target->static_deps_enabled())
                mark(*r.target);
        for (auto& r : resolved)
            if (twins.contains(r.target))
                r.static_twin = (build_dir / "lib" / detail::triple_dir(r.triple) /
                                 ("lib" + r.target->name() + ".a")).generic_string();
    }

    std::vector<std::string> all_outputs;
    for (auto& r : resolved) {
        const Target& t = *r.target;
        /* header-only targets contribute usage requirements (via collect_usage)
         * but produce no artifact and no edges */
        if (t.kind() == TargetKind::Interface)
            continue;
        const bool msvc = r.triple.family == CompilerFamily::Msvc;
        const bool pic = t.pic() || t.kind() != TargetKind::Executable;
        if (msvc && t.static_deps_enabled())
            return std::unexpected(Error {
                Error::Kind::Config, "target '" + t.name() + "': static_deps() is not supported on MSVC targets yet",
            });

        const Usage usage = UsageWalk(by_name).run(r);

        std::vector<std::string> defines = t.defines();
        defines.insert(defines.end(), usage.defines.begin(), usage.defines.end());

        /* public flags (self + deps) first, so the target's private flags() win */
        std::string user_flags;
        for (const auto& f : usage.flags)
            user_flags.append(f).push_back(' ');
        user_flags += t.flags();

        auto cflags = detail::build_flags(r.triple, proj.mode(), t.cpp_standard(), t.warnings(),
                                          t.debug_info(), t.optimization(), t.sanitizer(), t.lto(),
                                          t.hardening(), pic, defines, usage.includes, user_flags,
                                          usage.system_includes);
        if (!cflags)
            return std::unexpected(cflags.error());

        /* Manual mode: replace the regex import lists with clang-scan-deps'
         * authoritative closures (parallel; ~10 ms per TU). */
        if (t.modules_mode() == ModulesMode::Manual) {
            if (r.triple.family != CompilerFamily::Clang)
                return std::unexpected(Error {
                    Error::Kind::Config,
                    "modules_mode(Manual) requires a clang-family compiler (clang-scan-deps)",
                });
            if (!os::program_exists("clang-scan-deps"))
                return std::unexpected(Error {
                    Error::Kind::Toolchain, "modules_mode(Manual): clang-scan-deps not found on PATH",
                });
            /* scratch in a temp_scope: unique per run, removed at scope end
             * (declared before the pool so it outlives the workers) */
            const os::temp_scope scratch;
            if (!scratch.valid())
                return std::unexpected(Error {
                    Error::Kind::Io, "cannot create scan-deps scratch directory",
                });
            std::vector<Result<std::vector<std::string>>> scanned(r.units.size());
            const size_t nthreads = std::min<size_t>(r.units.size(), 8);
            {
                std::vector<std::jthread> pool;
                for (size_t w = 0; w < nthreads; ++w)
                    pool.emplace_back([&, w] {
                        for (size_t i = w; i < r.units.size(); i += nthreads)
                            scanned[i] = detail::scan_deps_requires(
                                r.triple.compiler, cflags->compile, r.units[i].path, proj.cwd(),
                                scratch.path());
                    });
                /* jthread destructors join at scope end */
            }
            for (size_t i = 0; i < r.units.size(); ++i) {
                if (!scanned[i])
                    return std::unexpected(scanned[i].error());
                r.units[i].imports = std::move(*scanned[i]);
            }
        }

        /* precompiled header: one generation edge; every C++ object of this
         * target depends on it and carries the use-flag. GCC finds the .gch
         * through -I + -include <base>; clang takes -include-pch directly. */
        std::string pch_out, pch_flag;
        if (!t.pch().empty()) {
            if (msvc)
                return std::unexpected(Error {
                    Error::Kind::Config, "pch() is not yet supported on MSVC targets", std::filesystem::path(t.pch()),
                });
            const std::filesystem::path hdr = t.pch();
            std::error_code pch_ec;
            if (!std::filesystem::exists(proj.cwd() / hdr, pch_ec))
                return std::unexpected(Error {Error::Kind::Config, "pch: header not found", hdr});
            const std::string base = hdr.filename().generic_string();
            const std::string pch_dir = (build_dir / "pch" / t.name()).generic_string();
            if (r.triple.family == CompilerFamily::Gcc) {
                pch_out = pch_dir + "/" + base + ".gch";
                pch_flag = "-I" + pch_dir + " -include " + base;
            } else {
                pch_out = pch_dir + "/" + base + ".pch";
                pch_flag = "-include-pch " + pch_out;
            }
            NinjaWriter::Edge pe;
            pe.outputs = {pch_out};
            pe.rule = "compile_pch";
            pe.inputs = {hdr.generic_string()};
            pe.vars = {
                {"cxx", r.triple.compiler},
                {"flags", cflags->compile},
                {"depfile", pch_out + ".d"},
            };
            w.build(std::move(pe));
        }

        /* BMIs visible to this target: own + transitive depends_on */
        std::map<std::string, std::string, std::less<>> bmis;
        std::map<std::string, std::vector<std::string>, std::less<>> module_imports;
        std::set<const Target*> seen;
        collect_bmis(r, by_name, bmis, module_imports, seen);

        /* std / std.compat resolve like any other module */
        StdModule std_mod;
        if (r.needs_std || r.needs_std_compat) {
            auto sm = std_for(r, r.needs_std_compat);
            if (!sm)
                return std::unexpected(sm.error());
            std_mod = std::move(*sm);
            bmis["std"] = std_mod.pcm;
            module_imports["std"] = {};
            if (r.needs_std_compat) {
                bmis["std.compat"] = std_mod.compat_pcm;
                module_imports["std.compat"] = {"std"}; /* re-exports std */
            }
        }

        std::map<std::string_view, const detail::ModuleUnitInfo*> unit_by_path;
        for (const auto& u : r.units)
            unit_by_path[std::string_view(u.path)] = &u;
        const auto unit_for = [&](const std::string& src) -> const detail::ModuleUnitInfo* {
            const auto it = unit_by_path.find(src);
            return it == unit_by_path.end() ? nullptr : it->second;
        };

        /* compile units: plain sources, or unity batch TUs that #include them
         * (module units never batched). A batch needs the union of every
         * member's BMI wiring, so imports are merged at generation time. */
        struct CxxUnit {
            std::string path;
            std::string imp_flags;
            std::vector<std::string> imp_ins;
        };
        std::vector<CxxUnit> cxx_units;
        const auto merge_imports = [&](std::span<const std::string> members) -> Result<CxxUnit> {
            CxxUnit unit;
            std::set<std::string> seen; /* dedupe bmi paths across members */
            for (const auto& m : members) {
                const detail::ModuleUnitInfo* u = unit_for(m);
                if (!u)
                    return std::unexpected(Error {
                        Error::Kind::Internal, "no scanned unit info for '" + m + "'", std::filesystem::path(m),
                    });
                auto imp = detail::resolve_module_imports(*u, bmis, module_imports);
                if (!imp)
                    return std::unexpected(imp.error());
                /* imp->first is space-joined -fmodule-file=NAME=PATH tokens,
                 * imp->second the same BMIs as dep paths (parallel order) */
                std::vector<std::string> tokens;
                {
                    std::string_view fs = imp->first;
                    while (!fs.empty()) {
                        const auto sp = fs.find(' ');
                        tokens.emplace_back(fs.substr(0, sp == std::string_view::npos ? fs.size() : sp));
                        if (sp == std::string_view::npos)
                            break;
                        fs.remove_prefix(sp + 1);
                    }
                }
                for (size_t i = 0; i < imp->second.size(); ++i) {
                    if (!seen.insert(imp->second[i]).second)
                        continue;
                    unit.imp_ins.push_back(imp->second[i]);
                    if (i < tokens.size()) {
                        if (!unit.imp_flags.empty())
                            unit.imp_flags += ' ';
                        unit.imp_flags += tokens[i];
                    }
                }
            }
            return unit;
        };
        if (t.unity() && !r.cxx_sources.empty()) {
            const std::string unity_dir = (build_dir / "unity" / t.name()).generic_string();
            if (auto r2 = detail::make_dirs(unity_dir); !r2)
                return std::unexpected(r2.error());
            constexpr size_t kBatch = 16;
            for (size_t begin = 0; begin < r.cxx_sources.size(); begin += kBatch) {
                const size_t end = std::min(begin + kBatch, r.cxx_sources.size());
                const std::span<const std::string> members(r.cxx_sources.data() + begin,
                                                           end - begin);
                auto unit = merge_imports(members);
                if (!unit)
                    return std::unexpected(unit.error());
                std::string body = "/* truss unity batch for target '" + t.name() + "' */\n";
                for (const auto& m : members)
                    body += "#include \"" + (proj.cwd() / m).generic_string() + "\"\n";
                const std::string gen = unity_dir + "/unity_" + std::to_string(begin / kBatch) + ".cpp";
                if (auto r2 = detail::write_file_if_changed(gen, body); !r2)
                    return std::unexpected(r2.error());
                unit->path = gen;
                cxx_units.push_back(std::move(*unit));
            }
        } else {
            for (const auto& src : r.cxx_sources) {
                auto unit = merge_imports(std::span<const std::string> {&src, 1});
                if (!unit)
                    return std::unexpected(unit.error());
                unit->path = src;
                cxx_units.push_back(std::move(*unit));
            }
        }

        /* compile edges */
        std::vector<std::string> objects;
        for (const auto& b : r.bmis) {
            const detail::ModuleUnitInfo* u = unit_for(b.source);
            if (!u)
                return std::unexpected(Error {
                    Error::Kind::Internal, "no scanned unit info for '" + b.source + "'",
                    std::filesystem::path(b.source),
                });
            auto imp = detail::resolve_module_imports(*u, bmis, module_imports);
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
        for (const auto& unit : cxx_units) {
            const std::string obj = r.objdir + "/" + unit.path + obj_ext(r.triple.family);
            NinjaWriter::Edge e;
            e.outputs = {obj};
            e.rule = msvc ? "compile_cxx_msvc" : "compile_cxx";
            e.inputs = {unit.path};
            e.implicit_ins = unit.imp_ins;
            if (!pch_out.empty())
                e.implicit_ins.push_back(pch_out);
            std::string flags = cflags->compile;
            if (!unit.imp_flags.empty())
                flags += " " + unit.imp_flags;
            if (!pch_flag.empty())
                flags += " " + pch_flag;
            e.vars = {
                {"cxx", r.triple.compiler},
                {"flags", flags},
            };
            if (!msvc)
                e.vars.emplace_back("depfile", obj + ".d");
            w.build(std::move(e));
            objects.push_back(obj);
        }
        const std::string cc = detail::c_driver(r.triple.compiler);
        for (const auto& src : r.c_sources) {
            const std::string obj = r.objdir + "/" + src + obj_ext(r.triple.family);
            NinjaWriter::Edge e;
            e.outputs = {obj};
            e.rule = msvc ? "compile_c_msvc" : "compile_c";
            e.inputs = {src};
            e.vars = {
                {"cc", cc},
                {"flags", cflags->compile_c},
            };
            if (!msvc)
                e.vars.emplace_back("depfile", obj + ".d");
            w.build(std::move(e));
            objects.push_back(obj);
        }

        /* dependency outputs (static archives / shared libs), transitive via
         * depends_on. Pre-order with dedupe gives GNU ld the right order:
         * direct deps first, their deps to the right of the archives that
         * need them. */
        std::vector<std::string> dep_outputs;
        bool has_shared_dep = false;
        const bool static_consumer = t.static_deps_enabled();
        std::set<const Target*> dep_seen {&t};
        std::function<Result<void>(const Target&)> add_dep_outputs =
            [&](const Target& d) -> Result<void> {
            for (const Target* dep : d.deps()) {
                if (!dep_seen.insert(dep).second)
                    continue;
                if (dep->kind() == TargetKind::Interface)
                    continue; /* header-only: no artifact to link */
                const auto it = by_name.find(dep->name());
                if (it == by_name.end())
                    return std::unexpected(Error {
                        Error::Kind::Config,
                        "target '" + d.name() + "' depends on unknown target '" + dep->name() + "'",
                    });
                /* MSVC links against a dll's import lib, never the dll;
                 * static_deps() consumers link a shared lib's static twin */
                if (dep->kind() != TargetKind::SharedLibrary)
                    dep_outputs.push_back(it->second->output);
                else if (static_consumer)
                    dep_outputs.push_back(it->second->static_twin);
                else {
                    dep_outputs.push_back(it->second->implib.empty() ? it->second->output
                                                                      : it->second->implib);
                    has_shared_dep = true;
                }
                if (auto sub = add_dep_outputs(*dep); !sub)
                    return std::unexpected(sub.error());
            }
            return {};
        };
        if (auto sub = add_dep_outputs(t); !sub)
            return std::unexpected(sub.error());

        /* std.o defines the module initializer: link it exactly once per
         * artifact, never inside an archive (a second copy is a duplicate
         * definition). Dependencies reached through a shared lib carry their
         * own copy. */
        std::vector<std::string> std_objects;
        if (t.kind() != TargetKind::StaticLibrary) {
            bool want_std = r.needs_std || r.needs_std_compat, want_compat = r.needs_std_compat;
            std::set<const Target*> std_seen {&t};
            std::function<void(const Target&)> walk = [&](const Target& d) {
                for (const Target* dep : d.deps()) {
                    if (!std_seen.insert(dep).second)
                        continue;
                    const auto it = by_name.find(dep->name());
                    if (it == by_name.end())
                        continue;
                    if (dep->kind() == TargetKind::SharedLibrary && !static_consumer)
                        continue;
                    want_std |= it->second->needs_std || it->second->needs_std_compat;
                    want_compat |= it->second->needs_std_compat;
                    walk(*dep);
                }
            };
            walk(t);
            if (want_std) {
                auto sm = std_for(r, want_compat);
                if (!sm)
                    return std::unexpected(sm.error());
                std_objects.push_back(sm->obj);
                if (want_compat)
                    std_objects.push_back(sm->compat_obj);
            }
        }

        if (t.kind() == TargetKind::StaticLibrary && !t.merged_libraries().empty()) {
            /* merged archive: combine the listed static libraries (and any own
             * objects) into one .a via an `ar -M` MRI script. */
            if (msvc)
                return std::unexpected(Error {
                    Error::Kind::Config,
                    "target '" + t.name() + "': merge() (ar -M) is not supported for MSVC targets",
                });
            std::vector<std::string> inputs = std::move(objects);
            std::string script = "CREATE " + r.output + "\n";
            for (const auto& obj : inputs)
                script += "ADDMOD " + obj + "\n";
            for (const Target* m : t.merged_libraries()) {
                const auto it = by_name.find(m->name());
                if (it == by_name.end() || m->kind() != TargetKind::StaticLibrary)
                    return std::unexpected(Error {
                        Error::Kind::Config,
                        "target '" + t.name() + "': merge('" + m->name() + "') must name a static library "
                        "that is part of this build",
                    });
                script += "ADDLIB " + it->second->output + "\n";
                inputs.push_back(it->second->output);
            }
            script += "SAVE\nEND\n";
            const std::string mri_dir = (build_dir / "mri" / detail::triple_dir(r.triple)).generic_string();
            if (auto mk = detail::make_dirs(mri_dir); !mk)
                return std::unexpected(mk.error());
            const std::string mri = mri_dir + "/" + t.name() + ".mri";
            if (auto wr = detail::write_file_if_changed(mri, script); !wr)
                return std::unexpected(wr.error());
            NinjaWriter::Edge e;
            e.outputs = {r.output};
            e.rule = "merge_archive";
            e.inputs = std::move(inputs);
            e.implicit_ins = {mri};
            e.vars = {{"ar", r.triple.ar}, {"mri", mri}};
            w.build(std::move(e));
        } else if (t.kind() == TargetKind::StaticLibrary) {
            NinjaWriter::Edge e;
            e.outputs = {r.output};
            e.rule = msvc ? "archive_msvc" : "archive";
            e.inputs = std::move(objects);
            e.vars = {{"ar", r.triple.ar}};
            w.build(std::move(e));
        } else {
            std::vector<std::string> extra_link;
            if (has_shared_dep && !msvc && r.triple.os != Os::Windows) {
                /* exe lives in bin/<tdir>, shared libs in lib/<tdir>;
                 * single-quote $ORIGIN so the shell passes it through to ld.
                 * (Windows has no rpath: dlls are found next to the exe.) */
                const std::string origin = r.triple.os == Os::MacOS ? "@loader_path" : "'$ORIGIN'";
                extra_link.push_back("-Wl,-rpath," + origin + "/../../lib/" + detail::triple_dir(r.triple));
            }
            std::vector<std::string> link_paths = t.link_paths();
            link_paths.insert(link_paths.end(), usage.link_paths.begin(), usage.link_paths.end());
            /* static_deps() routes every library through -Bstatic except the C
             * runtime family; static_link() Interface deps always do */
            std::vector<std::string> link_libs, static_libs;
            const auto route = [&](const std::string& lib, bool want_static) {
                (want_static && !detail::is_runtime_lib(lib) ? static_libs : link_libs).push_back(lib);
            };
            for (const auto& l : t.link_libs()) route(l, static_consumer);
            for (const auto& l : usage.link_libs) route(l, static_consumer);
            for (const auto& l : usage.static_link_libs) route(l, true);
            auto link_res = detail::link_flags(r.triple, proj.mode(), t.debug_info(), t.sanitizer(),
                                               t.lto(), t.hardening(), link_paths, link_libs,
                                               t.frameworks(),
                                               t.framework_paths(), t.linker_flags(),
                                               static_libs, t.static_stdlib());
            if (!link_res)
                return std::unexpected(link_res.error());
            std::string link = link_res->linker;
            for (const auto& x : extra_link) link += " " + x;

            const bool shared = t.kind() == TargetKind::SharedLibrary;
            if (!r.static_twin.empty()) {
                /* the twin archives this target's own objects only: its deps'
                 * artifacts reach the consumer's link through the dep walk */
                NinjaWriter::Edge twin;
                twin.outputs = {r.static_twin};
                twin.rule = "archive";
                twin.inputs = objects;
                twin.vars = {{"ar", r.triple.ar}};
                w.build(std::move(twin));
            }
            if (shared && msvc) /* link.exe defaults the import lib next to the dll (bin/) */
                link += " /IMPLIB:\"" + r.implib + "\"";
            NinjaWriter::Edge e;
            e.outputs = {r.output};
            e.rule = msvc ? (shared ? "link_shared_msvc" : "link_exe_msvc")
                          : (shared ? "link_shared" : "link_exe");
            e.inputs = std::move(objects);
            e.inputs.insert(e.inputs.end(), std::make_move_iterator(std_objects.begin()),
                            std::make_move_iterator(std_objects.end()));
            /* samu's $in only carries explicit inputs, so dependency artifacts
             * (archives / shared libs) must be explicit, after the objects. */
            e.inputs.insert(e.inputs.end(), std::make_move_iterator(dep_outputs.begin()),
                            std::make_move_iterator(dep_outputs.end()));
            e.vars = {
                {"cxx", r.triple.compiler},
                {"flags", link_res->driver},
                {"link", link},
            };
            if (shared) {
                if (!r.implib.empty()) {
                    e.implicit_outs = {r.implib}; /* the import lib the linker writes */
                    if (!msvc) /* without it zig names the import lib after the first object */
                        e.vars.emplace_back("soname_flag", "-Wl,--out-implib," + r.implib);
                } else {
                    const std::string soname = std::filesystem::path(r.output).filename().generic_string();
                    if (r.triple.os == Os::MacOS)
                        e.vars.emplace_back("soname_flag", "-Wl,-install_name,@rpath/" + soname);
                    else if (detail::is_elf(r.triple.os))
                        e.vars.emplace_back("soname_flag", "-Wl,-soname," + soname);
                }
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
            const auto it = by_name.find(name);
            if (it == by_name.end())
                return std::unexpected(Error {Error::Kind::Config, "unknown --target '" + name + "'"});
            selected.push_back(it->second->output);
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

namespace {

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
 * executable) or a header/BMI it depends on (truss.d) is newer than the executable, a stale build graph would be
 * generated — run bootstrap.sh and re-exec with the same argv. Without a
 * bootstrap.sh, report it (via build()) instead of building the stale graph.
 * TRUSS_NO_REBUILD=1 disables the check; TRUSS_REBUILT guards the re-exec loop. */
std::optional<Error> refresh_build_script(std::span<char* const> argv) {
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

    /* truss.d (written by bootstrap.sh via -MMD) lists the headers truss.cpp
     * includes plus the truss BMIs/libtruss.a it was built against; without it
     * (older projects) only truss.cpp itself is compared. */
    std::filesystem::path changed;
    if (script_time > exe_time)
        changed = script;
    if (auto deps = detail::read_file(dir / "truss.d"); deps && changed.empty()) {
        for (const auto& dep : detail::parse_depfile(*deps)) {
            std::error_code dep_ec;
            const auto dep_time = std::filesystem::last_write_time(dir / dep, dep_ec);
            if (!dep_ec && dep_time > exe_time) {
                changed = dir / dep;
                break;
            }
        }
    }
    if (changed.empty())
        return std::nullopt;

    const auto bootstrap = dir / "bootstrap.sh";
    if (std::getenv("TRUSS_REBUILT") != nullptr) {
        log::warn(changed.string() + " still looks newer than " + exe.string() + " after a rebuild (clock skew?); continuing");
        return std::nullopt;
    }
    if (!std::filesystem::is_regular_file(bootstrap, ec))
        return Error {
            Error::Kind::Config,
            "build script is stale: " + changed.string() + " is newer than " + exe.string()
                + "\nrebuild it first (no bootstrap.sh next to it), or set TRUSS_NO_REBUILD=1 to use it anyway",
            changed,
        };

    log::info(changed.filename().string() + " changed: rebuilding " + exe.filename().string() + " (bootstrap.sh)");
    const std::array cmd {std::string("sh"), bootstrap.string()};
    auto outcome = detail::run_process(cmd, false, dir);
    if (!outcome)
        return outcome.error();
    if (outcome->exit_code != 0)
        return Error {Error::Kind::Command, "rebuilding the build script failed (bootstrap.sh exited with "
                                                + std::to_string(outcome->exit_code) + ")"};

    /* execv needs a NULL-terminated array; argv spans need not be */
    std::vector<char*> args(argv.begin(), argv.end());
    args.push_back(nullptr);
    ::setenv("TRUSS_REBUILT", "1", 1);
    ::execv(exe.c_str(), args.data());
    return Error {Error::Kind::Io, "re-exec of the rebuilt build script failed: " + std::string(std::strerror(errno))};
}

} // namespace

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
    "  compdb           write compile_commands.json\n"
    "  reload           rebuild the truss binary (truss checkout only)\n"
    "  task NAME        build, then run a task registered with proj.task(...)\n"
    "  task list        list the registered tasks with their descriptions\n"
    "\n"
    "options:\n"
    "  -j, --jobs N     parallel jobs (default: CPU count)\n"
    "  -v, --verbose    print full command lines\n"
    "  -n, --dry-run    show what would run without running it\n"
    "      --explain    explain why each step is rebuilt\n"
    "      --target NAME  build (or run) only NAME; repeatable\n"
    "      --build-dir PATH  build root (default: build)\n"
    "  -V, --version    print the truss version\n"
    "  -h, --help       show this help\n"
    "  --               pass the remaining arguments to the program started by run\n"
    "\n"
    "examples:\n"
    "  truss release run -- --port 8080\n"
    "  truss test -j 8\n"
    "  truss run --target server\n";

Project::Project(std::string name, std::span<char* const> argv) : name_(std::move(name)) {
    if (auto stale = refresh_build_script(argv))
        cli_error_ = std::move(*stale);

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
        else if (a == "--explain") explain_ = true;
        else if (a == "-j" || a == "--jobs") {
            if (++i >= argv.size()) { cli_error_ = Error {Error::Kind::Config, "-j requires a number"}; continue; }
            const std::string_view n = argv[i];
            const auto [end, ec] = std::from_chars(n.data(), n.data() + n.size(), jobs_);
            if (ec != std::errc {} || end != n.data() + n.size() || jobs_ <= 0)
                cli_error_ = Error {Error::Kind::Config, "-j requires a positive number"};
        }
        else if (a == "--target") {
            if (++i >= argv.size()) { cli_error_ = Error {Error::Kind::Config, "--target requires a name"}; continue; }
            only_targets_.emplace_back(argv[i]);
        }
        else if (a == "--build-dir") {
            if (++i >= argv.size()) { cli_error_ = Error {Error::Kind::Config, "--build-dir requires a path"}; continue; }
            build_root_ = argv[i];
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
    
    // Auto-discover truss modules from standard installation paths
    truss_module_paths_ = os::find_truss_modules();
}

Project::Project(std::string name, int argc, char** argv)
    : Project(std::move(name), std::span<char* const>(argv, static_cast<std::size_t>(argc))) {}

void Project::task(std::string name, std::string description, TaskFn fn) {
    user_tasks_.push_back({std::move(name), std::move(description), std::move(fn)});
}

std::filesystem::path Project::build_dir() const {
    const std::filesystem::path root = build_root_.has_value() ? *build_root_ : cwd_ / "build";
    return root / mode_dir_name(mode_);
}

Result<void> Project::compile_commands(const std::filesystem::path& path) {
    // If path is a directory, use compile_commands.json inside it
    auto output_path = path;
    if (std::error_code dir_ec; std::filesystem::is_directory(path, dir_ec)) {
        output_path = path / "compile_commands.json";
    }
    
    // Get the manifest path
    const std::filesystem::path bld = build_dir();
    const std::filesystem::path manifest = bld / "ninja" / "build.ninja";
    
    // Run samurai's compdb tool
    std::error_code ec;
    if (!std::filesystem::exists(manifest, ec)) {
        return std::unexpected(Error {
            Error::Kind::Config,
            "compile_commands requires a built manifest (run build() first)",
            manifest,
        });
    }
    
    // Invoke samurai compdb tool
    if (auto r = detail::samu_compdb(manifest.generic_string(), output_path.generic_string()); !r) {
        return r;
    }
    
    log::success("wrote " + output_path.generic_string());
    return {};
}

Result<void> Project::build() {
    const auto start = std::chrono::steady_clock::now();
    if (cli_error_) {
        log::error(*cli_error_);
        return std::unexpected(*cli_error_);
    }

    const std::filesystem::path bld = build_dir();
    if (!path_is_safe(cwd_) || !path_is_safe(bld))
        return std::unexpected(Error {
            Error::Kind::Config,
            "paths containing spaces or ':' are not supported yet (project root or build dir)",
            cwd_,
        });

    /* user task registry: checked first so `task list` reports it too */
    for (std::size_t i = 0; i < user_tasks_.size(); ++i) {
        const auto& name = user_tasks_[i].name;
        Error bad {Error::Kind::Config, ""};
        if (name.empty() || name == "list" || name.starts_with('-'))
            bad.message = "invalid task name '" + name + "' (empty, 'list' and '-'-prefixed names are reserved)";
        else if (std::ranges::any_of(user_tasks_ | std::views::take(i), [&](const UserTask& u) { return u.name == name; }))
            bad.message = "task '" + name + "' is registered twice";
        if (!bad.message.empty()) {
            log::error(bad);
            return std::unexpected(bad);
        }
    }

    if (list_tasks_) {
        if (user_tasks_.empty()) {
            std::puts("no tasks defined (register one with proj.task(name, \"description\", fn))");
            return {};
        }
        std::size_t width = 0;
        for (const auto& u : user_tasks_)
            width = std::max(width, u.name.size());
        std::puts("tasks (run with ./truss task <name>):");
        for (const auto& u : user_tasks_)
            std::printf("  %-*s  %s\n", static_cast<int>(width), u.name.c_str(),
                        u.description.empty() ? "" : u.description.c_str());
        return {};
    }

    std::vector<std::string> tasks = requested_tasks_;
    if (tasks.empty())
        tasks.push_back("build");

    /* clean-only: never resolve sources or emit a manifest just to delete it;
     * clean also works on a project whose sources are broken */
    if (std::ranges::all_of(tasks, [](const std::string& t) { return t == "clean"; })) {
        if (auto r = detail::remove_tree(bld); !r) {
            log::error(r.error());
            return std::unexpected(r.error());
        }
        return {};
    }

    if (auto r = validate(targets_); !r) {
        log::error(r.error());
        return std::unexpected(r.error());
    }

    /* project-wide toolchain: Config overlaid on the host triple (per-target
     * cross_target() overrides completely) */
    auto base_triple = detail::apply_config(config_, host_triple());
    if (!base_triple) {
        log::error(base_triple.error());
        return std::unexpected(base_triple.error());
    }

    /* resolve + validate sources */
    std::vector<ResolvedTarget> resolved;
    resolved.reserve(targets_.size());
    for (const auto& t : targets_) {
        ResolvedTarget r;
        r.target = t.get();
        r.triple = t->triple_override().value_or(*base_triple);

        auto sources = resolve_sources(*t);
        if (!sources) {
            log::error(sources.error());
            return std::unexpected(sources.error());
        }
        if (sources->empty() && t->kind() != TargetKind::Interface && t->merged_libraries().empty()) {
            Error e {
                Error::Kind::Config,
                "target '" + t->name() + "' has no sources; add some with .sources(...) or .sources_in(...)",
            };
            log::error(e);
            return std::unexpected(std::move(e));
        }

        const ModulesMode mm =
            t->modules_mode() == ModulesMode::Default ? ModulesMode::Auto : t->modules_mode();

        /* modules_mode(Manual) drives clang-scan-deps, which cannot parse the
         * two-token `zig c++` command line (it reads 'zig' as the compiler and
         * 'c++' as an input file). Fail with the fix instead of a scan error. */
        if (mm == ModulesMode::Manual && detail::is_zig(r.triple)) {
            Error e {
                Error::Kind::Config,
                "target '" + t->name() + "' sets modules_mode(Manual), but clang-scan-deps cannot "
                "run the zig driver; use modules_mode(Auto) (the default) with clang for modules "
                "(the zig toolchain does not build C++20 modules at all)",
            };
            log::error(e);
            return std::unexpected(std::move(e));
        }

        const std::string tdir = detail::triple_dir(r.triple);
        r.objdir = (bld / "obj" / tdir / t->name()).generic_string();

        for (const auto& f : *sources) {
            const bool is_bmi = f.ends_with(".cppm") || f.ends_with(".ixx");
            if (is_bmi && mm == ModulesMode::Off) {
                Error e {
                    Error::Kind::Config,
                    "'" + f + "' is a module interface unit, but target '" + t->name() +
                        "' has modules_mode(Off)",
                    std::filesystem::path(f),
                };
                log::error(e);
                return std::unexpected(std::move(e));
            }
            if (is_bmi) {
                auto info = detail::scan_module_unit(f);
                if (!info) {
                    log::error(info.error());
                    return std::unexpected(info.error());
                }
                if (info->module_name.empty()) {
                    Error e {
                        Error::Kind::Config,
                        "no 'export module <name>;' declaration found in '" + f + "'",
                        std::filesystem::path(f),
                    };
                    log::error(e);
                    return std::unexpected(std::move(e));
                }
                info->is_bmi_source = true;
                std::string flat = info->module_name;
                std::replace(flat.begin(), flat.end(), ':', '_');
                BmiUnit b;
                b.source = f;
                b.module_name = info->module_name;
                b.bmi_path = r.objdir + "/bmi/" + flat + ".pcm";
                b.object = r.objdir + "/" + f + obj_ext(r.triple.family);
                r.units.push_back(std::move(*info));
                r.bmis.push_back(std::move(b));
            } else if (f.ends_with(".cpp") || f.ends_with(".cc") || f.ends_with(".cxx")) {
                auto info = detail::scan_module_unit(f);
                if (!info) {
                    log::error(info.error());
                    return std::unexpected(info.error());
                }
                r.cxx_sources.push_back(f);
                r.units.push_back(std::move(*info));
            } else if (f.ends_with(".c")) {
                r.c_sources.push_back(f);
            } else {
                Error e {Error::Kind::Config, "unsupported source file '" + f + "'", std::filesystem::path(f)};
                log::error(e);
                return std::unexpected(std::move(e));
            }
        }

        /* BMI wiring is clang-specific (-fmodule-output/-fmodule-file) */
        if (r.triple.family == CompilerFamily::Msvc && !r.bmis.empty()) {
            Error e {
                Error::Kind::Config,
                "target '" + t->name() + "' has .cppm sources, but C++ module BMI wiring is not "
                "implemented for MSVC targets (it relies on clang's -fmodule-output/-fmodule-file)",
            };
            log::error(e);
            return std::unexpected(std::move(e));
        }

        /* zig's `zig c++` wrapper does not route a module-interface compile
         * through its C++ setup: a .cppm gets the host's system headers and a
         * default triple (not zig's bundled libc++), and forcing -x c++ makes
         * its build cache discard the emitted BMI. No consumable BMI comes out.
         * (Verified with zig 0.17; see docs/cross-compilation.md "Known gaps".) */
        if (detail::is_zig(r.triple) && !r.bmis.empty()) {
            Error e {
                Error::Kind::Config,
                "target '" + t->name() + "' has .cppm sources, but the zig toolchain cannot build "
                "C++20 named modules (its driver gives module units the wrong headers and discards "
                "the BMI); build modules with clang, e.g:\n"
                "  correct:   proj.configure(Config().toolchain(truss::Toolchain::LLVM))\n"
                "  incorrect: Config().toolchain(truss::Toolchain::Zig)   // with .cppm sources",
            };
            log::error(e);
            return std::unexpected(std::move(e));
        }
        if (detail::is_zig(r.triple) && !t->pch().empty()) {
            Error e {
                Error::Kind::Config,
                "target '" + t->name() + "' uses pch(), but the zig toolchain cannot build a "
                "precompiled header (its driver sends the -x c++-header output to the linker); "
                "drop pch() for zig targets or build with clang",
            };
            log::error(e);
            return std::unexpected(std::move(e));
        }

        for (const auto& u : r.units) {
            r.needs_std |= std::ranges::find(u.imports, "std") != u.imports.end();
            r.needs_std_compat |= std::ranges::find(u.imports, "std.compat") != u.imports.end();
        }
        if (r.needs_std || r.needs_std_compat) {
            const bool libcxx_clang = r.triple.family == CompilerFamily::Clang && !detail::is_zig(r.triple) &&
                                      r.triple.stdlib == StdLib::Libcxx;
            if (!libcxx_clang) {
                Error e {
                    Error::Kind::Config,
                    "target '" + t->name() + "' uses `import std;`, which needs clang with libc++;\n"
                    "  correct:   proj.configure(Config().toolchain(truss::Toolchain::LLVM))\n"
                    "  incorrect: compiler '" + r.triple.compiler + "' (zig, g++, MSVC and libstdc++ "
                    "do not provide a buildable std module)",
                };
                log::error(e);
                return std::unexpected(std::move(e));
            }
            if (t->cpp_standard() != CppStandard::Default && t->cpp_standard() < CppStandard::Cpp23) {
                Error e {
                    Error::Kind::Config,
                    "target '" + t->name() + "': `import std;` requires -std=c++23 or later "
                    "(cpp_standard(CppStandard::Cpp23))",
                };
                log::error(e);
                return std::unexpected(std::move(e));
            }
        }

        r.output = output_path_for(*t, bld, tdir, r.triple);
        if (t->kind() == TargetKind::SharedLibrary &&
            (r.triple.family == CompilerFamily::Msvc || r.triple.os == Os::Windows))
            r.implib = import_lib_path(*t, bld, tdir, r.triple);
        resolved.push_back(std::move(r));
    }

    /* generate the manifest */
    NinjaWriter w;
    if (auto r = emit_manifest(*this, resolved, bld, w); !r) {
        log::error(r.error());
        return std::unexpected(r.error());
    }
    const std::filesystem::path manifest = bld / "ninja" / "build.ninja";
    /* if_changed: an unchanged manifest keeps its mtime (no disk churn) */
    if (auto r = detail::write_file_if_changed(manifest, w.str()); !r) {
        log::error(r.error());
        return std::unexpected(r.error());
    }

    /* dispatch tasks */
    const TaskContext ctx {name_, mode_, bld, cwd_, jobs_, verbose_};

    /* build hooks: pre_build once before the first compile step, post_build
     * once after the first successful one; a failing hook aborts */
    bool hooks_pre_done = false, hooks_post_done = false;
    auto run_samu = [&](std::span<const std::string> targets) -> Result<void> {
        if (!hooks_pre_done) {
            for (auto& fn : pre_build_)
                if (auto r = fn(ctx); !r)
                    return r;
            hooks_pre_done = true;
        }
        if (auto r = detail::samu_build(manifest.generic_string(), jobs_, verbose_, dry_run_, explain_, targets); !r)
            return r;
        if (!hooks_post_done) {
            for (auto& fn : post_build_)
                if (auto r = fn(ctx); !r)
                    return r;
            hooks_post_done = true;
        }
        return {};
    };

    for (const auto& task : tasks) {
        Result<void> step = [&]() -> Result<void> {
            if (task == "clean")
                return detail::remove_tree(bld);

            if (task == "build")
                return run_samu({});

            if (task == "compdb") {
                auto r = detail::samu_compdb(manifest.generic_string(), (bld / "compile_commands.json").generic_string());
                if (r) log::success("wrote " + (bld / "compile_commands.json").generic_string());
                return r;
            }

            if (task == "reload") {
                // reload: rebuild the truss executable and replace current binary
                // Only works when truss.cpp exists in cwd (self-hosting scenario)
                std::error_code ec;
                if (!std::filesystem::exists(cwd_ / "truss.cpp", ec)) {
                    return std::unexpected(Error {
                        Error::Kind::Config,
                        "reload: truss.cpp not found. This command only works from the truss repository root."
                    });
                }

                // Find the truss target's output path BEFORE rebuilding
                const ResolvedTarget* truss_rt = nullptr;
                for (const auto& rt : resolved) {
                    if (rt.target->name() == "truss" && rt.target->kind() == TargetKind::Executable) {
                        truss_rt = &rt;
                        break;
                    }
                }

                if (!truss_rt) {
                    return std::unexpected(Error {
                        Error::Kind::Config,
                        "reload: no 'truss' executable target found in this project"
                    });
                }

                // Rebuild all targets (to ensure dependencies are up to date)
                // but we only care about the truss output
                if (auto r = run_samu({}); !r) {
                    return r;
                }

                // Verify the truss binary was built
                if (truss_rt->output.empty() || !std::filesystem::exists(truss_rt->output, ec)) {
                    return std::unexpected(Error {
                        Error::Kind::Internal,
                        "reload: truss executable not found at " + truss_rt->output
                    });
                }

                // Replace current binary
                // On Linux, we cannot overwrite a running executable directly.
                // Instead, we rename the new binary over the old one atomically.
                // The running process continues with the old inode (in memory),
                // while the new file replaces it on disk.
                auto current_exe = std::filesystem::canonical("/proc/self/exe", ec);
                if (ec || current_exe.empty()) {
                    return std::unexpected(Error {
                        Error::Kind::Io,
                        "reload: cannot determine current executable path"
                    });
                }

                // Create a temporary file next to the current executable
                auto temp_path = current_exe;
                temp_path += ".tmp_reload";
                
                // Copy the new binary to temp location
                std::filesystem::copy_file(truss_rt->output, temp_path,
                                          std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) {
                    return std::unexpected(Error {
                        Error::Kind::Io,
                        "reload: failed to copy new binary to temp: " + ec.message(),
                        temp_path
                    });
                }

                // Make executable
                std::filesystem::permissions(temp_path, std::filesystem::perms::owner_all, ec);
                if (ec) {
                    const std::string why = ec.message();
                    std::filesystem::remove(temp_path, ec);
                    return std::unexpected(Error {
                        Error::Kind::Io,
                        "reload: failed to set permissions on temp binary: " + why,
                        temp_path
                    });
                }

                // Atomically replace the running binary with rename
                // On Linux, rename over an executing file works - the process
                // continues running from the old inode while the new file replaces it
                std::filesystem::rename(temp_path, current_exe, ec);
                if (ec) {
                    const std::string why = ec.message();
                    std::filesystem::remove(temp_path, ec);
                    return std::unexpected(Error {
                        Error::Kind::Io,
                        "reload: failed to replace binary: " + why,
                        current_exe
                    });
                }

                log::success("truss reloaded from source. New binary active for next run.");
                return {};
            }

            if (task == "test") {
                std::vector<std::string> test_outputs;
                for (const auto& r : resolved)
                    if (r.target->is_test_target())
                        test_outputs.push_back(r.output);
                if (test_outputs.empty()) {
                    log::warn("no targets marked is_test(); nothing to run");
                    return {};
                }
                if (auto r = run_samu(test_outputs); !r)
                    return r;
                int failures = 0;
                for (const auto& exe : test_outputs) {
                    log::info("running " + exe);
                    const std::array argv {exe};
                    auto outcome = detail::run_process(argv, false);
                    if (!outcome)
                        return std::unexpected(outcome.error());
                    if (outcome->exit_code != 0) {
                        log::error("test '" + exe + "' failed with exit code " + std::to_string(outcome->exit_code));
                        ++failures;
                    }
                }
                if (failures)
                    return std::unexpected(Error {Error::Kind::Command, std::to_string(failures) + " test(s) failed"});
                log::success("all tests passed");
                return {};
            }

            if (task == "run") {
                /* --target narrows the candidates; otherwise the first
                 * declared executable runs */
                std::vector<std::string> exe_outputs, exe_names;
                for (const auto& r : resolved) {
                    if (r.target->kind() != TargetKind::Executable || r.target->is_test_target())
                        continue;
                    if (!only_targets_.empty() && std::ranges::find(only_targets_, r.target->name()) == only_targets_.end())
                        continue;
                    exe_outputs.push_back(r.output);
                    exe_names.push_back(r.target->name());
                }
                if (exe_outputs.empty())
                    return std::unexpected(Error {Error::Kind::Config, "no executable to run (test executables are excluded)"});
                if (exe_outputs.size() > 1)
                    log::info("running '" + exe_names.front() + "' (" + std::to_string(exe_outputs.size())
                              + " executables; pick one with --target NAME)");
                if (auto r = run_samu(std::span(exe_outputs).first(1)); !r)
                    return r;
                std::vector<std::string> argv {exe_outputs.front()};
                argv.insert(argv.end(), run_args_.begin(), run_args_.end());
                auto outcome = detail::run_process(argv, false);
                if (!outcome)
                    return std::unexpected(outcome.error());
                if (outcome->exit_code != 0)
                    return std::unexpected(Error {Error::Kind::Command, "'" + exe_outputs.front() + "' exited with " + std::to_string(outcome->exit_code)});
                return {};
            }

            const auto find_user = [&](std::string_view name) {
                return std::ranges::find(user_tasks_, name, &UserTask::name);
            };
            if (task.starts_with("task:")) {
                const std::string_view name = std::string_view(task).substr(5);
                if (const auto it = find_user(name); it != user_tasks_.end())
                    return it->fn(ctx);
                return std::unexpected(Error {
                    Error::Kind::Config, "unknown task '" + std::string(name) + "' (see ./truss task list)",
                });
            }
            if (find_user(task) != user_tasks_.end())
                return std::unexpected(Error {
                    Error::Kind::Config,
                    "'" + task + "' is a user task: run it with ./truss task " + task,
                });
            return std::unexpected(Error {
                Error::Kind::Config,
                "unknown command '" + task + "' (built-ins: test, run, clean, compdb, reload; "
                    "user tasks: ./truss task list; see ./truss --help)",
            });
        }();

        if (!step) {
            log::error(step.error());
            return std::unexpected(step.error());
        }
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    log::info("truss: build took " + std::to_string(elapsed) + "ms");
    return {};
}

} // namespace truss
