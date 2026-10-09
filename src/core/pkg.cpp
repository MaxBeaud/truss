module;
#include <algorithm>
#include <cctype>
#include <expected>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <cstdlib>

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
Result<Target*> Project::add_subdirectory(const std::filesystem::path& dir) {
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
                return t.get(); /* idempotent re-registration */
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
    return target;
}

} // namespace truss
