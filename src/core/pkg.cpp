module;
#include <algorithm>
#include <cctype>
#include <expected>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <cstdlib>

#include <nlohmann/json.hpp>

module truss;
import :api;
import :impl;

namespace truss {

namespace {

/* --- truss.lock: tiny key=value pin file in the project root -------------- */

std::string lock_key(std::string_view name) { return "pkg." + std::string(name); }

/* --- cache layout ---------------------------------------------------------- */

std::filesystem::path cache_root() {
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "truss";
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / ".cache" / "truss";
}

bool looks_like_commit(std::string_view ref) {
    if (ref.size() != 40)
        return false;
    for (const char c : ref)
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    return true;
}

Result<void> run_tool(std::span<const std::string> argv, std::string_view what) {
    auto outcome = detail::run_process(argv, true);
    if (!outcome)
        return std::unexpected(outcome.error());
    if (outcome->exit_code != 0) {
        std::string msg = std::string(what) + " failed (exit " + std::to_string(outcome->exit_code) + ")";
        if (!outcome->output.empty())
            msg += ":\n" + outcome->output;
        return std::unexpected(Error {Error::Kind::Fetch, std::move(msg)});
    }
    return {};
}

Result<void> git_fetch(const std::string& url, const std::string& ref,
                       const std::filesystem::path& dest) {
    const std::filesystem::path tmp = dest.string() + ".tmp";
    std::filesystem::remove_all(tmp);
    if (!looks_like_commit(ref)) {
        const std::vector<std::string> argv {"git", "clone", "--depth", "1", "--branch", ref, url,
                                            tmp.generic_string()};
        if (auto r = run_tool(argv, "git clone"); !r)
            return r;
    } else {
        const std::string d = tmp.generic_string();
        for (const std::vector<std::string>& argv :
             {std::vector<std::string> {"git", "init", d},
              std::vector<std::string> {"git", "-C", d, "remote", "add", "origin", url},
              std::vector<std::string> {"git", "-C", d, "fetch", "--depth", "1", "origin", ref},
              std::vector<std::string> {"git", "-C", d, "checkout", "--detach", "FETCH_HEAD"}}) {
            if (auto r = run_tool(argv, "git"); !r)
                return r;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, dest, ec);
    if (ec)
        return std::unexpected(Error {Error::Kind::Io, "could not move git cache into place", tmp});
    return {};
}

Result<void> tarball_fetch(const std::string& url, const std::string& sha256,
                           const std::filesystem::path& cache_file) {
    if (std::filesystem::exists(cache_file))
        return {};
    const std::filesystem::path tmp = cache_file.string() + ".tmp";
    const std::vector<std::string> curl {"curl", "-fsSL", "--retry", "2", "-o", tmp.generic_string(), url};
    if (auto r = run_tool(curl, "curl"); !r) {
        std::filesystem::remove(tmp);
        return r;
    }
    const std::vector<std::string> sum {"sha256sum", tmp.generic_string()};
    auto outcome = detail::run_process(sum, true);
    if (!outcome)
        return std::unexpected(outcome.error());
    if (outcome->exit_code != 0)
        return std::unexpected(Error {Error::Kind::Fetch, "sha256sum failed on downloaded file", tmp});
    std::string actual = outcome->output;
    actual.erase(actual.find_first_of(" \t\n"));
    if (actual != sha256) {
        std::filesystem::remove(tmp);
        return std::unexpected(Error {
            Error::Kind::Fetch,
            "sha256 mismatch for '" + url + "': expected " + sha256 + ", got " + actual,
        });
    }
    std::error_code ec;
    std::filesystem::rename(tmp, cache_file, ec);
    if (ec)
        return std::unexpected(Error {Error::Kind::Io, "could not move tarball into cache", tmp});
    return {};
}

Result<void> extract_tarball(const std::filesystem::path& tarball, const std::filesystem::path& dest) {
    const std::string name = tarball.filename().generic_string();
    std::string_view mode = "xzf"; /* .tar.gz / .tgz */
    if (name.ends_with(".bz2"))
        mode = "xjf";
    else if (name.ends_with(".xz") || name.ends_with(".txz"))
        mode = "xJf";
    else if (name.ends_with(".tar"))
        mode = "xf";
    const std::vector<std::string> argv {"tar", std::string(mode), tarball.generic_string(),
                                         "-C", dest.generic_string(), "--strip-components=1"};
    return run_tool(argv, "tar");
}

Result<void> copy_tree(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::create_directories(to, ec);
    if (ec)
        return std::unexpected(Error {Error::Kind::Io, "cannot create directory", to});
    std::filesystem::copy(from, to,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::skip_existing,
                          ec);
    if (ec)
        return std::unexpected(Error {Error::Kind::Io, "copy failed", from});
    std::filesystem::remove_all(to / ".git"); /* vendored copies never carry the repo */
    return {};
}

/* Any compilable source directly in this directory (not nested)? */
bool has_top_level_sources(const std::filesystem::path& dir) {
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(dir, ec); it != std::filesystem::directory_iterator {};
         it.increment(ec)) {
        if (ec)
            break;
        if (!it->is_regular_file(ec))
            continue;
        const std::string ext = it->path().extension().generic_string();
        if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" || ext == ".cppm")
            return true;
    }
    return false;
}

} // namespace

/* --- Project::fetch --------------------------------------------------------
 * v0.1 fetches sequentially; parallel fetch is a later optimization (network
 * bound, rare path — not worth the complexity yet). */
Result<Package> Project::fetch(std::string_view name, const FetchOptions& opts) {
    const std::string pkg_name(name);
    if (pkg_name.empty() || pkg_name == "." || pkg_name == "..")
        return std::unexpected(Error {Error::Kind::Config, "fetch: package name must not be empty, '.' or '..'"});
    for (const char c : pkg_name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.')
            return std::unexpected(Error {
                Error::Kind::Config,
                "fetch('" + pkg_name + "'): name may only contain letters, digits, '-', '_' and '.'",
            });
    }
    const bool use_git = !opts.git.empty();
    const bool use_tarball = !opts.url.empty();
    if (use_git == use_tarball)
        return std::unexpected(Error {
            Error::Kind::Config,
            "fetch('" + pkg_name + "'): set exactly one of git+tag or url+sha256",
        });
    if (use_git && opts.tag.empty())
        return std::unexpected(Error {Error::Kind::Config, "fetch('" + pkg_name + "'): git source needs a tag"});
    if (use_tarball && opts.sha256.empty())
        return std::unexpected(Error {Error::Kind::Config, "fetch('" + pkg_name + "'): tarball source needs sha256"});

    const std::string pin = use_git ? opts.tag : opts.sha256;
    const std::filesystem::path dest = build_dir() / "packages" / pkg_name;
    const std::filesystem::path lock_file = cwd_ / "truss.lock";

    auto lock = detail::read_lockfile(lock_file);
    const auto pin_it = lock.find(lock_key(pkg_name));
    const bool pinned = pin_it != lock.end() && pin_it->second == pin;
    if (pinned && std::filesystem::exists(dest))
        return Package {pkg_name, dest}; /* offline fast path */

    const auto root = cache_root();
    if (use_git) {
        const std::string flat_tag = [&] {
            std::string s = opts.tag;
            for (char& c : s)
                if (c == '/' || c == ':')
                    c = '_';
            return s;
        }();
        const std::filesystem::path git_cache = root / "git" / pkg_name / flat_tag;
        if (!std::filesystem::exists(git_cache)) {
            detail::log_info("fetching " + pkg_name + " (" + opts.git + " @ " + opts.tag + ")");
            if (auto r = detail::make_dirs(git_cache.parent_path()); !r)
                return std::unexpected(r.error());
            if (auto r = git_fetch(opts.git, opts.tag, git_cache); !r)
                return std::unexpected(r.error());
        }
        std::filesystem::remove_all(dest);
        if (auto r = copy_tree(git_cache, dest); !r)
            return std::unexpected(r.error());
    } else {
        std::string ext {".tar.gz"};
        for (std::string_view cand : {".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2", ".tar"})
            if (opts.url.ends_with(cand)) {
                ext = std::string(cand);
                break;
            }
        const std::filesystem::path tar_cache = root / "tarballs" / (opts.sha256 + ext);
        if (auto r = detail::make_dirs(tar_cache.parent_path()); !r)
            return std::unexpected(r.error());
        if (!std::filesystem::exists(tar_cache)) {
            detail::log_info("fetching " + pkg_name + " (" + opts.url + ")");
            if (auto r = tarball_fetch(opts.url, opts.sha256, tar_cache); !r)
                return std::unexpected(r.error());
        }
        std::filesystem::remove_all(dest);
        if (auto r = detail::make_dirs(dest); !r)
            return std::unexpected(r.error());
        if (auto r = extract_tarball(tar_cache, dest); !r)
            return std::unexpected(r.error());
    }

    lock[lock_key(pkg_name)] = pin;
    if (auto r = detail::write_lockfile(lock_file, lock); !r)
        return std::unexpected(r.error());
    return Package {pkg_name, dest};
}

/* --- Project::add_subdirectory ---------------------------------------------
 * Convention: static library named after the directory, sources from its src/
 * tree (recursively), includes from its include/ dir when present. */
Result<TargetRef> Project::add_subdirectory(const std::filesystem::path& dir) {
    const std::filesystem::path abs = dir.is_absolute() ? dir : cwd_ / dir;
    std::error_code ec;
    if (!std::filesystem::is_directory(abs, ec))
        return std::unexpected(Error {Error::Kind::Config, "not a directory", abs});

    const std::string name = abs.filename().generic_string();
    if (name.empty())
        return std::unexpected(Error {Error::Kind::Config, "add_subdirectory: directory has no name", abs});

    const bool is_subdir = std::ranges::find(subdir_target_names_, name) != subdir_target_names_.end();
    for (const auto& t : targets_) {
        if (t->name() == name) {
            if (is_subdir)
                return TargetRef{*t}; /* idempotent re-registration */
            return std::unexpected(Error {
                Error::Kind::Config,
                "add_subdirectory: a target named '" + name + "' already exists (pick a distinct directory name)",
            });
        }
    }

    const std::string rel = std::filesystem::relative(abs, cwd_, ec).generic_string();
    if (ec)
        return std::unexpected(Error {Error::Kind::Io, "cannot relativize subdirectory path", abs});

    Target* target {};
    if (std::filesystem::is_directory(abs / "src", ec)) {
        auto& lib = add_static_library(name);
        lib.sources_in(rel + "/src");
        target = &lib;
    } else if (has_top_level_sources(abs)) {
        auto& lib = add_static_library(name);
        lib.sources(rel + "/*.{c,cpp,cc,cxx}"); /* root-layout: top-level sources only */
        target = &lib;
    } else {
        target = &add_interface_library(name); /* header-only */
    }
    /* include dirs: include/ when present, else the package root (root-layout libs) */
    if (std::filesystem::is_directory(abs / "include", ec))
        target->include_dirs(rel + "/include");
    else
        target->include_dirs(rel);
    subdir_target_names_.push_back(name);
    return TargetRef{*target};
}

/* --- Project::conan ---------------------------------------------------------
 * Runs conan 2, parses the --format=json dependency graph, and returns one
 * Interface target ("conan") carrying the whole graph's usage requirements. */
Result<TargetRef> Project::conan(std::string_view conanfile) {
    for (const auto& t : targets_)
        if (t->name() == "conan")
            return TargetRef{*t}; /* idempotent */
    if (!detail::program_exists("conan"))
        return std::unexpected(Error {Error::Kind::Toolchain, "conan not found on PATH (conan 2 required)"});

    const std::filesystem::path cf = cwd_ / std::filesystem::path(conanfile.empty() ? "." : std::string(conanfile));
    std::error_code ec;
    if (!std::filesystem::exists(cf, ec))
        return std::unexpected(Error {Error::Kind::Config, "conan: conanfile not found", cf});

    const std::filesystem::path out = build_dir() / "packages" / "conan";
    if (auto r = detail::make_dirs(out); !r)
        return std::unexpected(r.error());

    detail::log_info("conan install " + cf.generic_string());
    const std::vector<std::string> argv {"conan", "install", cf.generic_string(),
                                         "--format=json", "--output-folder", out.generic_string(),
                                         "--build=missing"};
    auto outcome = detail::run_process_stdout(argv);
    if (!outcome)
        return std::unexpected(outcome.error());
    if (outcome->exit_code != 0)
        return std::unexpected(Error {
            Error::Kind::Fetch,
            "conan install failed (exit " + std::to_string(outcome->exit_code) + ")",
        });

    const auto j = nlohmann::json::parse(outcome->output, nullptr, false);
    if (!j.is_object() || !j.contains("graph") || !j["graph"].contains("nodes") ||
        !j["graph"]["nodes"].is_object())
        return std::unexpected(Error {Error::Kind::Fetch, "conan: unexpected --format=json output"});

    auto str_array = [](const nlohmann::json& obj, const char* key) {
        std::vector<std::string> out;
        if (obj.contains(key) && obj[key].is_array())
            for (const auto& v : obj[key])
                if (v.is_string())
                    out.push_back(v.get<std::string>());
        return out;
    };
    auto dir_array = [](const nlohmann::json& obj, const char* key,
                        const std::filesystem::path& pkg_folder) {
        std::vector<std::string> out;
        if (obj.contains(key) && obj[key].is_array())
            for (const auto& v : obj[key]) {
                if (!v.is_string())
                    continue;
                std::filesystem::path p(v.get<std::string>());
                if (p.is_relative() && !pkg_folder.empty())
                    p = pkg_folder / p;
                out.push_back(p.generic_string());
            }
        return out;
    };

    auto& iface = add_interface_library("conan");
    for (const auto& [id, node] : j["graph"]["nodes"].items()) {
        /* skip the consumer root node: it has no package folder */
        std::filesystem::path pkg_folder;
        if (!node.contains("package_folder") || !node["package_folder"].is_string())
            continue;
        pkg_folder = node["package_folder"].get<std::string>();
        if (!node.contains("cpp_info") || !node["cpp_info"].contains("root"))
            continue;
        const auto& root = node["cpp_info"]["root"];
        for (const auto& d : dir_array(root, "includedirs", pkg_folder))
            iface.include_dirs(d);
        for (const auto& d : dir_array(root, "libdirs", pkg_folder))
            iface.link_paths(d);
        for (const auto& l : str_array(root, "libs"))
            iface.link_libs(l);
        for (const auto& l : str_array(root, "system_libs"))
            iface.link_libs(l);
        for (const auto& d : str_array(root, "defines"))
            iface.defines(d);
    }
    return TargetRef{iface};
}

/* conan({"zlib/1.3.1", "fmt/11.0.2"}): generates a conanfile.txt under
 * build/{mode}/packages/conan_generated/ and runs the path overload on it.
 * conan refs must be versioned (unlike vcpkg names). */
Result<TargetRef> Project::conan(std::initializer_list<std::string_view> refs) {
    if (refs.size() == 0) /* module purview: initializer_list::empty is not found, size() is builtin */
        return std::unexpected(Error {
            Error::Kind::Config, "conan: empty require list (refs must be versioned, e.g. \"zlib/1.3.1\")",
        });
    for (const auto& r : refs)
        if (r.find('/') == std::string_view::npos)
            return std::unexpected(Error {
                Error::Kind::Config,
                "conan: ref '" + std::string(r) + "' is missing a version (conan refs are versioned, e.g. \"zlib/1.3.1\")",
            });

    const std::filesystem::path dir = build_dir() / "packages" / "conan_generated";
    if (auto r = detail::make_dirs(dir); !r)
        return std::unexpected(r.error());

    std::string body = "[requires]\n";
    for (const auto& r : refs)
        body += std::string(r) + "\n";
    if (auto r = detail::write_file((dir / "conanfile.txt").generic_string(), body + "\n"); !r)
        return std::unexpected(r.error());

    return conan((dir / "conanfile.txt").generic_string());
}

/* --- Project::vcpkg ---------------------------------------------------------
 * Manifest mode: uses the project's vcpkg.json (generating one from `packages`
 * when absent), installs under build/{mode}/vcpkg_installed, and returns one
 * Interface target ("vcpkg") for the triplet's include/ + lib/ trees. */
Result<TargetRef> Project::vcpkg(std::vector<std::string> packages) {
    for (const auto& t : targets_)
        if (t->name() == "vcpkg")
            return TargetRef{*t}; /* idempotent */

    std::string exe = "vcpkg";
    if (const char* root = std::getenv("VCPKG_ROOT"); root && *root) {
        const std::filesystem::path p = std::filesystem::path(root) / "vcpkg";
        if (std::filesystem::exists(p))
            exe = p.generic_string();
    }
    if (!detail::program_exists(exe))
        return std::unexpected(Error {Error::Kind::Toolchain, "vcpkg not found (put it on PATH or set VCPKG_ROOT)"});

    const std::filesystem::path manifest = cwd_ / "vcpkg.json";
    std::error_code ec;
    if (!std::filesystem::exists(manifest, ec)) {
        if (packages.empty())
            return std::unexpected(Error {
                Error::Kind::Config, "vcpkg: no vcpkg.json in the project root and no packages listed",
            });
        std::string body = "{\n  \"name\": \"" + name_ + "\",\n  \"version\": \"1.0.0\",\n  \"dependencies\": [";
        for (size_t i = 0; i < packages.size(); ++i) {
            if (i)
                body += ",";
            body += "\n    \"" + packages[i] + "\"";
        }
        body += "\n  ]\n}\n";
        if (auto r = detail::write_file(manifest, body); !r)
            return std::unexpected(r.error());
    }

    const TargetTriple triple = host_triple();
    if (triple.os != Os::Linux)
        return std::unexpected(Error {Error::Kind::Toolchain, "the vcpkg integration currently supports Linux only"});
    std::string arch = "x64";
    if (triple.arch == Arch::X86)
        arch = "x86";
    else if (triple.arch == Arch::Arm64)
        arch = "arm64";
    const std::string triplet = arch + "-linux";

    const std::filesystem::path install_root = build_dir() / "vcpkg_installed";
    detail::log_info("vcpkg install (" + triplet + ")");
    /* manifest mode is auto-detected from vcpkg.json in the current directory,
     * which is the project root (truss runs from cwd_) */
    const std::vector<std::string> argv {exe, "install", "--triplet", triplet,
                                         "--x-install-root", install_root.generic_string(),
                                         "--no-print-usage"};
    auto outcome = detail::run_process(argv, false);
    if (!outcome)
        return std::unexpected(outcome.error());
    if (outcome->exit_code != 0)
        return std::unexpected(Error {
            Error::Kind::Fetch,
            "vcpkg install failed (exit " + std::to_string(outcome->exit_code) + ")",
        });

    const std::filesystem::path tdir = install_root / triplet;
    auto& iface = add_interface_library("vcpkg");
    if (std::filesystem::is_directory(tdir / "include", ec))
        iface.include_dirs((tdir / "include").generic_string());
    const std::filesystem::path libdir = tdir / "lib";
    if (std::filesystem::is_directory(libdir, ec)) {
        iface.link_paths(libdir.generic_string());
        std::vector<std::string> names;
        for (auto it = std::filesystem::directory_iterator(libdir, ec);
             it != std::filesystem::directory_iterator {}; it.increment(ec)) {
            if (ec)
                break;
            if (!it->is_regular_file(ec))
                continue; /* skip pkgconfig/ and other subdirs */
            const std::string fn = it->path().filename().generic_string();
            std::string base;
            if (fn.ends_with(".a") || fn.contains(".so"))
                base = fn.starts_with("lib") ? fn.substr(3) : fn;
            else
                continue;
            if (const auto dot = base.find(".so"); dot != std::string::npos)
                base.erase(dot);
            else
                base.erase(base.size() - 2); /* strip ".a" */
            if (!base.empty() && std::ranges::find(names, base) == names.end())
                names.push_back(base);
        }
        for (const auto& n : names)
            iface.link_libs(n);
    }
    return TargetRef{iface};
}

} // namespace truss
