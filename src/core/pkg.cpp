module;
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <expected>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include <cstdlib>
#include <unistd.h>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/file.h>
#endif

#include <nlohmann/json.hpp>

module truss;
import :api;
import :impl;

namespace truss {

namespace {

/* Unique staging path next to `p` (pid + sequence). Two fetches of the same
 * package — two truss processes, or two packages sharing a url — must never
 * share a temp file: one could rename the other's unverified bytes into the
 * permanent cache. */
std::filesystem::path staging_path(const std::filesystem::path& p) {
    static std::atomic<unsigned> seq {0};
    return p.string() + ".tmp." + std::to_string(::getpid()) + "."
         + std::to_string(seq.fetch_add(1, std::memory_order_relaxed));
}

/* Advisory exclusive lock over the lockfile's read-modify-write: two
 * concurrent truss processes must not lose each other's pins. Taken on the
 * lockfile itself; write_lockfile publishes through temp+rename, so a third
 * process opening the renamed file can in principle see a stale inode —
 * self-healing (the pin is re-fetched and rewritten). POSIX only; a no-op
 * elsewhere (single-user machines). */
class lockfile_guard {
public:
    explicit lockfile_guard(const std::filesystem::path& file, int wait_seconds = 120) {
#if !defined(_WIN32)
        /* O_CLOEXEC: the guard is held across materialize_fetch, which spawns
         * git/curl/tar — an inherited fd would carry the flock into children
         * that can outlive truss and hold the lock forever */
        fd_ = ::open(file.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666);
        if (fd_ == -1) {
            why_ = "could not open it: " + std::string(std::strerror(errno));
            return;
        }
        /* non-blocking retry loop with a deadline: a wedged holder must
         * produce a clear error, not a silently blocked build */
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(wait_seconds);
        for (;;) {
            if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) {
                locked_ = true;
                return;
            }
            if (errno != EWOULDBLOCK) { /* a real error, not a held lock */
                why_ = "flock failed: " + std::string(std::strerror(errno));
                return;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                why_ = "another truss process held it for over " + std::to_string(wait_seconds) + "s";
                return;
            }
            ::usleep(100 * 1000);
        }
#endif
    }
    lockfile_guard(const lockfile_guard&) = delete;
    lockfile_guard& operator=(const lockfile_guard&) = delete;
    ~lockfile_guard() {
#if !defined(_WIN32)
        if (fd_ != -1) {
            ::flock(fd_, LOCK_UN);
            ::close(fd_);
        }
#endif
    }
    /// False when the lockfile could not be opened/locked — callers must fail
    /// instead of silently racing on the read-modify-write.
    bool locked() const { return locked_; }
    /// Why locked() is false ("could not open ..." / "timed out after Ns").
    std::string why() const { return why_; }

private:
    int fd_ {-1};
    bool locked_ {false};
    std::string why_ {"locking unavailable on this platform"};
};

/* --- truss.lock: tiny key=value pin file in the project root -------------- */

std::string lock_key(std::string_view name) { return "pkg." + std::string(name); }

/* --- cache layout ---------------------------------------------------------- */

std::filesystem::path cache_root() {
    if (auto xdg = os::env("XDG_CACHE_HOME"); xdg)
        return std::filesystem::path(*xdg) / "truss";
    return std::filesystem::path(os::env("HOME").value_or(".")) / ".cache" / "truss";
}

bool looks_like_commit(std::string_view ref) {
    if (ref.size() != 40)
        return false;
    for (const char c : ref)
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    return true;
}

/* True when the child's stderr reaches a terminal: download tools render their
 * own progress bars there (git clone/fetch, curl --progress-bar), and a
 * captured stream cannot show progress. Piped/CI runs keep the capture so
 * failures still carry the tool's diagnostics. */
bool progress_to_terminal() { return ::isatty(STDERR_FILENO) != 0; }

Result<void> run_tool(std::span<const std::string> argv, std::string_view what, int timeout_seconds = 600) {
    /* interactive: let stderr through (progress bars), capture stdout only;
     * non-interactive: capture both so the error message carries the detail */
    auto outcome = progress_to_terminal()
        ? detail::run_process_stdout(argv, timeout_seconds)
        : detail::run_process(argv, true, timeout_seconds);
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

Result<void> verify_no_escaping_links(const std::filesystem::path& dest);

Result<void> git_fetch(const std::string& url, const std::string& ref,
                       const std::filesystem::path& dest, int timeout_seconds) {
    const std::filesystem::path tmp = staging_path(dest);
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec); /* best effort */
    const auto cleanup = [&] { std::filesystem::remove_all(tmp, ec); };
    /* a stalled mirror must not hold the fetch (and the lockfile) forever:
     * abort transfers crawling under 1 KiB/s for 30 s */
    const std::vector<std::string> git_slow {"git", "-c", "http.lowSpeedLimit=1024",
                                             "-c", "http.lowSpeedTime=30",
                                             /* the commit path checks out --detach:
                                              * the wall of advice is pure noise */
                                             "-c", "advice.detachedHead=false"};
    if (!looks_like_commit(ref)) {
        std::vector<std::string> argv = git_slow;
        argv.insert(argv.end(), {"clone", "--depth", "1", "--branch", ref, url,
                                 tmp.generic_string()});
        if (auto r = run_tool(argv, "git clone", timeout_seconds); !r) {
            cleanup(); /* a failed clone must not leave staging garbage */
            return r;
        }
    } else {
        const std::string d = tmp.generic_string();
        const auto step = [&](std::vector<std::string> tail) {
            std::vector<std::string> argv = git_slow;
            argv.insert(argv.end(), tail.begin(), tail.end());
            return argv;
        };
        for (const auto& argv : {step({"init", d}),
                                 step({"-C", d, "remote", "add", "origin", url}),
                                 step({"-C", d, "fetch", "--depth", "1", "origin", ref}),
                                 step({"-C", d, "checkout", "--detach", "FETCH_HEAD"})}) {
            if (auto r = run_tool(argv, "git", timeout_seconds); !r) {
                cleanup();
                return r;
            }
        }
    }
    /* the escape check runs BEFORE the tree is published to the shared cache:
     * a hostile repo must never land where another fetch (or a later run) can
     * pick it up without re-verification */
    if (auto r = verify_no_escaping_links(tmp); !r) {
        cleanup();
        return r;
    }
    std::filesystem::rename(tmp, dest, ec);
    if (ec) {
        /* a concurrent truss process may have won the clone race: its
         * completed cache is as good as ours */
        std::error_code dir_ec;
        if (std::filesystem::is_directory(dest, dir_ec)) {
            cleanup();
            return {};
        }
        cleanup(); /* the rename failed outright: do not leak the staging tree */
        return std::unexpected(Error {Error::Kind::Io, "could not move git cache into place", tmp});
    }
    return {};
}

Result<void> tarball_fetch(const std::string& url, const std::string& sha256,
                           const std::filesystem::path& cache_file, int timeout_seconds) {
    std::error_code ec;
    if (std::filesystem::exists(cache_file, ec)) {
        /* verify the cached bytes too: an entry written by an interrupted or
         * raced earlier run must not be reused unchecked forever */
        auto actual = fs::sha256_file(cache_file);
        if (actual && *actual == sha256)
            return {};
        std::filesystem::remove(cache_file, ec); /* corrupt or unreadable: refetch */
    }
    const std::filesystem::path tmp = staging_path(cache_file);
    /* -s would silence the progress meter too: on a terminal the bar is the
     * point, so only -f (fail on HTTP errors) and -L (follow redirects) apply */
    std::vector<std::string> curl {"curl", progress_to_terminal() ? "-fL" : "-fsSL", "--retry", "2",
                                   "--connect-timeout", "15",
                                   /* a stalled mirror must not hang the build:
                                    * abort transfers under 1 KiB/s for 30 s */
                                   "--speed-limit", "1024", "--speed-time", "30"};
    if (timeout_seconds > 0)
        curl.insert(curl.end(), {"--max-time", std::to_string(timeout_seconds)});
    /* one-line meter instead of curl's 4-row table; only on a terminal (a
     * captured stream would collect carriage-return spam) */
    if (progress_to_terminal())
        curl.insert(curl.end(), "--progress-bar");
    curl.insert(curl.end(), {"-o", tmp.generic_string(), url});
    if (auto r = run_tool(curl, "curl", timeout_seconds); !r) {
        std::filesystem::remove(tmp, ec);
        return r;
    }
    auto actual = fs::sha256_file(tmp);
    if (!actual) {
        std::filesystem::remove(tmp, ec);
        return std::unexpected(actual.error());
    }
    if (*actual != sha256) {
        std::filesystem::remove(tmp, ec);
        return std::unexpected(Error {
            Error::Kind::Fetch,
            "sha256 mismatch for '" + url + "': expected " + sha256 + ", got " + *actual,
        });
    }
    std::filesystem::rename(tmp, cache_file, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return std::unexpected(Error {Error::Kind::Io, "could not move tarball into cache", tmp});
    }
    return {};
}

/* Post-extraction hardening (defense in depth on top of system tar, which
 * already refuses `..` members and strips leading '/'): a tarball can still
 * ship a symlink (created early) whose target escapes dest, and subsequent
 * members or later build code would read/write through it outside the
 * package directory. Walk the tree and reject any symlink whose normalized
 * target lands outside dest; same check for the directories the tree
 * exposes (dangling dir links catch at target resolution, this catches at
 * fetch time with the package named). */
Result<void> verify_no_escaping_links(const std::filesystem::path& dest) {
    std::error_code ec;
    const std::string dest_str = std::filesystem::weakly_canonical(dest, ec).generic_string();
    if (ec)
        return std::unexpected(Error {Error::Kind::Io, "cannot stat extracted package dir", dest});
    std::filesystem::path it_err;
    for (auto it = std::filesystem::recursive_directory_iterator(
             dest, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator {}; it.increment(ec)) {
        if (ec) {
            it_err = it->path();
            break;
        }
        /* never descend through a link: the scan must see what shipping the
         * tarball planted, not what it points at */
        if (it->is_symlink(ec)) {
            auto target = std::filesystem::read_symlink(it->path(), ec);
            if (ec) {
                it_err = it->path();
                break;
            }
            const bool escapes = target.is_absolute() ?
                                     /* absolute link target: escapes unless it stays inside */
                                     !std::filesystem::weakly_canonical(target, ec).generic_string()
                                          .starts_with(dest_str + "/") :
                                     [&] {
                                         /* compare like with like: canonicalize the link's
                                          * parent too, or a symlinked project path (macOS
                                          * /tmp) would flag every package-internal link */
                                         std::error_code pc;
                                         const auto parent = std::filesystem::weakly_canonical(
                                             it->path().parent_path(), pc);
                                         if (pc)
                                             return true; /* cannot resolve: refuse the package */
                                         return !std::filesystem::path((parent / target).lexically_normal())
                                                       .generic_string()
                                                       .starts_with(dest_str + "/");
                                     }();
            if (ec)
                return std::unexpected(Error {Error::Kind::Io, "cannot read symlink in package", it->path()});
            if (escapes)
                return std::unexpected(Error {
                    Error::Kind::Fetch,
                    "refusing package: '" + it->path().lexically_relative(dest).generic_string() +
                        "' is a link escaping the package directory (target '" + target.generic_string() + "')",
                    it->path(),
                });
            it.disable_recursion_pending();
        }
    }
    if (ec)
        return std::unexpected(Error {Error::Kind::Io, "cannot scan extracted package", it_err.empty() ? dest : it_err});
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
                                         "-C", dest.generic_string(), "--strip-components=1",
                                         /* never restore ownership or permission bits from the
                                          * tarball (a hostile archive must not land setuid) */
                                         "--no-same-owner", "--no-same-permissions"};
    if (auto r = run_tool(argv, "tar"); !r)
        return r;
    return verify_no_escaping_links(dest);
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

namespace detail {

/* materialize a cached tree into the build dir with hardlinks (instant, no
 * extra disk), falling back to real copies across filesystems. Caveat: linked
 * files share their inode with the cache, so editing a fetched source in the
 * build tree also edits the cached copy — compilers replace files rather than
 * write in place, so normal builds never hit this. */
Result<void> copy_tree(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::create_directories(to, ec);
    if (ec)
        return std::unexpected(Error {Error::Kind::Io, "cannot create directory", to});
    for (auto it = std::filesystem::recursive_directory_iterator(from, ec);
         it != std::filesystem::recursive_directory_iterator {}; it.increment(ec)) {
        if (ec)
            break;
        const std::filesystem::path rel = it->path().lexically_relative(from);
        if (rel.empty())
            break; // cannot happen: the iterator builds its paths from `from`
        if (rel.filename() == ".git") { /* vendored copies never carry the repo */
            if (it->is_directory(ec))
                it.disable_recursion_pending();
            continue;
        }
        const std::filesystem::path dest = to / rel;
        if (it->is_symlink(ec)) {
            /* branch BEFORE the following is_regular/is_directory checks: a
             * link must be recreated as a link (hardlinking through it would
             * copy its target's content, a link-to-dir would otherwise become
             * an empty real directory) — and its target must stay inside the
             * tree, same discipline as the tarball path */
            auto target = std::filesystem::read_symlink(it->path(), ec);
            if (ec)
                return std::unexpected(Error {Error::Kind::Io, "cannot read symlink", it->path()});
            const std::string resolved =
                (it->path().parent_path() / target).lexically_normal().generic_string();
            if (!resolved.starts_with(from.generic_string() + "/"))
                return std::unexpected(Error {
                    Error::Kind::Fetch,
                    "refusing package: '" + rel.generic_string() +
                        "' is a link escaping the package directory (target '" +
                        target.generic_string() + "')",
                    it->path(),
                });
            it.disable_recursion_pending();
            std::filesystem::create_symlink(target, dest, ec);
            if (ec)
                return std::unexpected(Error {Error::Kind::Io, "cannot create symlink", dest});
        } else if (it->is_directory(ec)) {
            std::filesystem::create_directories(dest, ec);
            if (ec)
                return std::unexpected(Error {Error::Kind::Io, "cannot create directory", dest});
        } else if (it->is_regular_file(ec)) {
            if (std::filesystem::exists(dest, ec))
                continue; /* skip_existing semantics */
            std::filesystem::create_hard_link(it->path(), dest, ec);
            if (ec) {
                std::filesystem::copy_file(it->path(), dest,
                                           std::filesystem::copy_options::skip_existing, ec);
                if (ec)
                    return std::unexpected(Error {Error::Kind::Io, "copy failed", it->path()});
            }
        }
    }
    std::error_code git_ec;
    std::filesystem::remove_all(to / ".git", git_ec); /* older materializations may have it */
    return {};
}

} // namespace detail

/* --- Project::fetch --------------------------------------------------------
 * fetch() fetches one package; fetch_all() overlaps the network-bound work
 * of several fetches on worker threads. Both share validation, caching and
 * lockfile pinning. */
namespace {

Result<void> validate_fetch(const std::string& pkg_name, const FetchOptions& opts) {
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
    /* a leading '-' would be parsed as an option by git/curl (e.g.
     * --upload-pack=<cmd>): URLs and refs never start with one */
    for (const std::string* v : {&opts.git, &opts.tag, &opts.url})
        if (v->starts_with('-'))
            return std::unexpected(Error {
                Error::Kind::Config, "fetch('" + pkg_name + "'): '" + *v + "' must not start with '-'",
            });
    if (opts.tag == "." || opts.tag == ".." || opts.tag.contains('\\'))
        return std::unexpected(Error {
            Error::Kind::Config, "fetch('" + pkg_name + "'): invalid tag '" + opts.tag + "'",
        });
    /* tags/urls land in argv (never a shell) and in the lockfile (one pin per
     * line): whitespace and control characters serve no purpose there and
     * would corrupt the pin file */
    for (const std::string* v : {&opts.git, &opts.tag, &opts.url})
        for (const char c : *v)
            if (c == ' ' || static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
                return std::unexpected(Error {
                    Error::Kind::Config, "fetch('" + pkg_name + "'): '" + *v +
                                             "' must not contain whitespace or control characters",
                });
    if (use_git && opts.tag.empty())
        return std::unexpected(Error {Error::Kind::Config, "fetch('" + pkg_name + "'): git source needs a tag"});
    if (use_tarball && opts.sha256.empty())
        return std::unexpected(Error {Error::Kind::Config, "fetch('" + pkg_name + "'): tarball source needs sha256"});
    /* the sha256 names the cache file and pins the tarball: 64 hex chars */
    if (use_tarball && (opts.sha256.size() != 64 ||
                        !std::ranges::all_of(opts.sha256, [](char c) {
                            return std::isxdigit(static_cast<unsigned char>(c));
                        })))
        return std::unexpected(Error {
            Error::Kind::Config,
            "fetch('" + pkg_name + "'): sha256 must be 64 hex characters",
        });
    if (opts.timeout < 0)
        return std::unexpected(Error {
            Error::Kind::Config, "fetch('" + pkg_name + "'): timeout must not be negative",
        });
    return {};
}

/* cache + materialize (no lockfile I/O): worker-thread safe — every job
 * touches its own dest, caches are per-package, log lines are single printfs */
Result<void> materialize_fetch(const std::string& pkg_name, const FetchOptions& opts,
                               const std::filesystem::path& dest) {
    const auto root = cache_root();
    std::error_code ec;
    if (!opts.git.empty()) {
        const std::string flat_tag = [&] {
            std::string s = opts.tag;
            for (char& c : s)
                if (c == '/' || c == ':')
                    c = '_';
            return s;
        }();
        const std::filesystem::path git_cache = root / "git" / pkg_name / flat_tag;
        if (!std::filesystem::exists(git_cache, ec)) {
            log::info("fetching " + pkg_name + " (" + opts.git + " @ " + opts.tag + ")");
            if (auto r = detail::make_dirs(git_cache.parent_path()); !r)
                return std::unexpected(r.error());
            /* git_fetch verifies the symlink escape check before publishing
             * the cache, so what lands here is already vetted */
            if (auto r = git_fetch(opts.git, opts.tag, git_cache, opts.timeout); !r)
                return std::unexpected(r.error());
        }
        std::filesystem::remove_all(dest, ec);
        return detail::copy_tree(git_cache, dest);
    }
    std::string ext {".tar.gz"};
    for (std::string_view cand : {".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2", ".tar"})
        if (opts.url.ends_with(cand)) {
            ext = std::string(cand);
            break;
        }
    const std::filesystem::path tar_cache = root / "tarballs" / (opts.sha256 + ext);
    if (auto r = detail::make_dirs(tar_cache.parent_path()); !r)
        return std::unexpected(r.error());
    if (!std::filesystem::exists(tar_cache, ec)) {
        log::info("fetching " + pkg_name + " (" + opts.url + ")");
        if (auto r = tarball_fetch(opts.url, opts.sha256, tar_cache, opts.timeout); !r)
            return std::unexpected(r.error());
    }
    std::filesystem::remove_all(dest, ec);
    if (auto r = detail::make_dirs(dest); !r)
        return std::unexpected(r.error());
    if (auto r = extract_tarball(tar_cache, dest); !r) {
        /* a refused tarball must not leave a (partly escaped) tree behind */
        std::filesystem::remove_all(dest, ec);
        return r;
    }
    /* --strip-components=1 silently drops root-level members: an archive
     * without the expected single top-level directory extracts to nothing */
    bool empty = true;
    for (auto it = std::filesystem::directory_iterator(dest, ec);
         empty && it != std::filesystem::directory_iterator {}; it.increment(ec)) {
        if (ec) break;
        empty = false;
    }
    if (empty) {
        std::filesystem::remove_all(dest, ec);
        return std::unexpected(Error {
            Error::Kind::Fetch,
            "tarball '" + opts.url + "' extracted nothing (truss strips one wrapper "
            "directory; the archive's members must sit inside a single top-level directory)",
        });
    }
    return {};
}

} // namespace

Result<Package> Project::fetch(std::string_view name, const FetchOptions& opts) {
    /* thin wrapper over fetch_all: identical validation, caching, pinning and
     * lockfile discipline — one code path, no drift */
    auto all = fetch_all({{std::string(name), opts}});
    if (!all)
        return std::unexpected(all.error());
    return std::move((*all)[0]);
}

Result<std::vector<Package>> Project::fetch_all(std::vector<std::pair<std::string, FetchOptions>> requests) {
    for (const auto& [name, opts] : requests)
        if (auto r = validate_fetch(name, opts); !r)
            return std::unexpected(r.error());
    /* duplicate names: linear via sort + adjacent_find */
    auto names = requests | std::views::keys | std::ranges::to<std::vector>();
    std::ranges::sort(names);
    if (auto dup = std::ranges::adjacent_find(names); dup != names.end())
        return std::unexpected(Error {
            Error::Kind::Config, "fetch_all: duplicate package name '" + *dup + "'",
        });

    struct Job {
        std::string name;
        std::string pin;
        std::filesystem::path dest;
        bool offline {false};   /* lockfile pin hit: nothing to do */
        bool fetched {false};   /* materialized by this call: needs a lock entry */
        Result<void> outcome {};
    };
    const std::filesystem::path lock_file = cwd_ / "truss.lock";
    /* held across the pool: the final read-modify-write below must be atomic
     * against concurrent truss processes (see lockfile_guard). Failing loudly
     * when the lock cannot be taken beats silently racing on the pins. */
    const lockfile_guard lock_guard(lock_file);
    if (!lock_guard.locked())
        return std::unexpected(Error {
            Error::Kind::Io,
            "fetch_all: cannot lock " + lock_file.generic_string() + " — "
                + lock_guard.why() + " (concurrent fetches would corrupt the pin file)",
        });
    auto lock = detail::read_lockfile(lock_file);
    std::vector<Job> jobs;
    jobs.reserve(requests.size());
    std::error_code ec;
    for (auto& [name, opts] : requests) {
        Job job;
        job.name = name;
        job.pin = opts.git.empty() ? opts.sha256 : opts.tag;
        job.dest = build_dir() / "packages" / name;
        const auto pin_it = lock.find(lock_key(name));
        /* the completion marker (written after a full materialize) must be
         * present AND carry this run's pin: a dest materialized under an old
         * pin must not pass as fetched */
        const auto marker_ok = [&](const Job& j) {
            auto marker = detail::read_file(j.dest / ".truss-ok");
            return marker && *marker == j.pin;
        };
        job.offline = pin_it != lock.end() && pin_it->second == job.pin
                      && marker_ok(job) && std::filesystem::exists(job.dest, ec);
        jobs.push_back(std::move(job));
    }

    /* worker threads pull disjoint jobs (round-robin chunks); the network and
     * tar/git processes dominate, so 8 concurrent fetches is plenty */
    /* nthreads must outlive the pool: workers read it on every iteration,
     * and it is destroyed before the pool joins at scope end otherwise */
    const size_t nthreads = std::min<size_t>(jobs.size(), 8);
    {
        std::vector<std::jthread> pool;
        for (size_t t = 0; t < nthreads; ++t)
            pool.emplace_back([&, t] {
                for (size_t i = t; i < jobs.size(); i += nthreads) {
                    if (jobs[i].offline)
                        continue;
                    jobs[i].outcome = materialize_fetch(jobs[i].name, requests[i].second, jobs[i].dest);
                    if (jobs[i].outcome) {
                        /* completion marker for the offline fast path */
                        auto stamp = detail::write_file(jobs[i].dest / ".truss-ok", jobs[i].pin);
                        if (!stamp)
                            jobs[i].outcome = std::unexpected(stamp.error());
                    }
                    jobs[i].fetched = jobs[i].outcome.has_value();
                }
            });
        /* jthread destructors join at scope end */
    }

    for (auto& job : jobs)
        if (!job.outcome)
            return std::unexpected(job.outcome.error());

    bool lock_dirty = false;
    for (const auto& job : jobs)
        if (job.fetched && lock[lock_key(job.name)] != job.pin) {
            lock[lock_key(job.name)] = job.pin;
            lock_dirty = true;
        }
    if (lock_dirty)
        if (auto r = detail::write_lockfile(lock_file, lock); !r)
            return std::unexpected(r.error());

    std::vector<Package> out;
    out.reserve(jobs.size());
    for (auto& job : jobs) /* jobs are dead here: move their fields out */
        out.push_back(Package {std::move(job.name), std::move(job.dest)});
    return out;
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

/* --- configure-skip fingerprinting ------------------------------------------
 * sha256 of a file's contents (native hasher); used to detect unchanged
 * conanfile/vcpkg.json so the tool invocation can be skipped. */

/* the conanfile a conan install dir argument would consume ("" if none) */
std::string conanfile_in(const std::filesystem::path& cf) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(cf, ec))
        return cf.generic_string();
    if (std::filesystem::is_directory(cf, ec))
        for (const char* name : {"conanfile.txt", "conanfile.py"})
            if (std::filesystem::is_regular_file(cf / name, ec))
                return (cf / name).generic_string();
    return {};
}

/* string array member of a JSON object (empty when absent or malformed) */
std::vector<std::string> json_str_array(const nlohmann::json& obj, const char* key) {
    std::vector<std::string> out;
    if (obj.contains(key) && obj[key].is_array())
        for (const auto& v : obj[key])
            if (v.is_string())
                out.push_back(v.get<std::string>());
    return out;
}

/* Spec values become -I/-L/-l/-D flags inside rule commands that samurai runs
 * through /bin/sh -c, and package specs come from remote package managers:
 * treat them as untrusted and refuse anything a shell or the flag grammar
 * would interpret (a hostile recipe must not reach command execution). */
bool spec_value_is_safe(std::string_view v) {
    if (v.empty())
        return false;
    /* a leading '-' would smuggle a compiler flag through -D/-I/-l prefixes */
    if (v.front() == '-')
        return false;
    for (const char c : v)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '/' || c == '.'
              || c == '_' || c == '-' || c == '+' || c == '='))
            return false;
    return true;
}

Result<void> check_spec_values(const std::vector<std::string>& values, std::string_view what) {
    for (const auto& v : values)
        if (!spec_value_is_safe(v))
            return std::unexpected(Error {
                Error::Kind::Fetch,
                std::string("package spec has an unsupported ") + std::string(what) +
                    " entry '" + v + "' (only letters, digits and ./_+-= are accepted)",
            });
    return {};
}

/* --- Project::conan ---------------------------------------------------------
 * Runs conan 2, parses the --format=json dependency graph, and returns one
 * Interface target ("conan") carrying the whole graph's usage requirements. */
Result<TargetRef> Project::conan(std::string_view conanfile) {
    for (const auto& t : targets_)
        if (t->name() == "conan")
            return TargetRef{*t}; /* idempotent */
    if (!os::program_exists("conan"))
        return std::unexpected(Error {Error::Kind::Toolchain, "conan not found on PATH (conan 2 required)"});

    const std::filesystem::path cf = cwd_ / std::filesystem::path(conanfile.empty() ? "." : std::string(conanfile));
    std::error_code ec;
    if (!std::filesystem::exists(cf, ec))
        return std::unexpected(Error {Error::Kind::Config, "conan: conanfile not found", cf});

    const std::filesystem::path out = build_dir() / "packages" / "conan";
    if (auto r = detail::make_dirs(out); !r)
        return std::unexpected(r.error());
    const std::filesystem::path spec_path = out / "truss-spec.json";

    /* configure skip: sha256 of the conanfile + a cached usage-requirements
     * spec; unchanged input with surviving package dirs means re-running
     * conan install could not change anything (~1-3 s saved per invocation) */
    std::string fingerprint;
    if (const std::string cf_file = conanfile_in(cf); !cf_file.empty())
        if (auto fp = fs::sha256_file(cf_file); fp)
            fingerprint = *fp;

    std::vector<std::string> includes, link_paths, libs, defines;
    const auto apply_spec = [&](const nlohmann::json& spec) -> Result<TargetRef> {
        includes = json_str_array(spec, "includes");
        link_paths = json_str_array(spec, "link_paths");
        libs = json_str_array(spec, "libs");
        defines = json_str_array(spec, "defines");
        for (const auto& [values, what] :
             {std::pair{std::cref(includes), "include dir"},
              std::pair{std::cref(link_paths), "library path"},
              std::pair{std::cref(libs), "library"}, std::pair{std::cref(defines), "define"}})
            if (auto r = check_spec_values(values.get(), what); !r)
                return std::unexpected(r.error());
        auto& iface = add_interface_library("conan");
        for (const auto& d : includes)
            iface.system_include_dirs(d);
        for (const auto& d : link_paths)
            iface.link_paths(d);
        for (const auto& l : libs)
            iface.link_libs(l);
        for (const auto& d : defines)
            iface.defines(d);
        return TargetRef {iface};
    };
    if (!fingerprint.empty() && std::filesystem::exists(spec_path, ec)) {
        auto cached = detail::read_file(spec_path);
        const auto j = cached
            ? nlohmann::json::parse(*cached, nullptr, false)
            : nlohmann::json {};
        bool alive = j.is_object();
        if (alive) { /* .value() would throw on a non-string key: use find() */
            const auto fp = j.find("fingerprint");
            alive = fp != j.end() && fp->is_string() && fp->get<std::string>() == fingerprint;
        }
        if (alive)
            for (const char* key : {"includes", "link_paths"})
                if (j.contains(key) && j[key].is_array())
                    for (const auto& v : j[key])
                        if (v.is_string() && !std::filesystem::is_directory(v.get<std::string>(), ec))
                            alive = false;
        if (alive) {
            log::info("conan: up to date (cached spec, skipped conan install)");
            return apply_spec(j);
        }
    }

    log::info("conan install " + cf.generic_string());
    const std::vector<std::string> argv {"conan", "install", cf.generic_string(),
                                         "--format=json", "--output-folder", out.generic_string(),
                                         "--build=missing"};
    auto outcome = detail::run_process_stdout(argv, 900);
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

    auto dir_array = [](const nlohmann::json& obj, const char* key,
                        const std::filesystem::path& pkg_folder) {
        return json_str_array(obj, key)
             | std::views::transform([&](const std::string& v) {
                   std::filesystem::path p(v);
                   if (p.is_relative() && !pkg_folder.empty())
                       p = pkg_folder / p;
                   return p.generic_string();
               })
             | std::ranges::to<std::vector>();
    };

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
            includes.push_back(d);
        for (const auto& d : dir_array(root, "libdirs", pkg_folder))
            link_paths.push_back(d);
        for (const auto& l : json_str_array(root, "libs"))
            libs.push_back(l);
        for (const auto& l : json_str_array(root, "system_libs"))
            libs.push_back(l);
        for (const auto& d : json_str_array(root, "defines"))
            defines.push_back(d);
    }

    nlohmann::json spec {
        {"fingerprint", fingerprint},
        {"includes", includes},
        {"link_paths", link_paths},
        {"libs", libs},
        {"defines", defines},
    };
    if (!fingerprint.empty())
        if (auto r = detail::write_file(spec_path, spec.dump(2) + "\n"); !r)
            return std::unexpected(r.error());

    return apply_spec(spec);
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
    if (auto root = os::env("VCPKG_ROOT"); root) {
        const std::filesystem::path p = std::filesystem::path(*root) / "vcpkg";
        std::error_code vec;
        if (std::filesystem::exists(p, vec))
            exe = p.generic_string();
    }
    if (!os::program_exists(exe))
        return std::unexpected(Error {Error::Kind::Toolchain, "vcpkg not found (put it on PATH or set VCPKG_ROOT)"});

    const std::filesystem::path manifest = cwd_ / "vcpkg.json";
    std::error_code ec;
    if (!std::filesystem::exists(manifest, ec)) {
        if (packages.empty())
            return std::unexpected(Error {
                Error::Kind::Config, "vcpkg: no vcpkg.json in the project root and no packages listed",
            });
        const nlohmann::json body {
            {"name", name_}, {"version", "1.0.0"}, {"dependencies", packages},
        };
        if (auto r = detail::write_file(manifest, body.dump(2) + "\n"); !r)
            return std::unexpected(r.error());
    }

    const TargetTriple triple = host_triple();
    if (triple.os != Os::Linux)
        return std::unexpected(Error {Error::Kind::Toolchain, "the vcpkg integration currently supports Linux only"});
    /* an unmapped architecture must not silently install x64 packages */
    std::string arch;
    switch (triple.arch) {
    case Arch::X64: arch = "x64"; break;
    case Arch::X86: arch = "x86"; break;
    case Arch::Arm64: arch = "arm64"; break;
    default:
        return std::unexpected(Error {
            Error::Kind::Toolchain,
            "the vcpkg integration has no triplet for this architecture",
        });
    }
    const std::string triplet = arch + "-linux";

    const std::filesystem::path install_root = build_dir() / "vcpkg_installed";
    const std::filesystem::path tdir = install_root / triplet;

    /* configure skip: unchanged vcpkg.json + surviving install tree means
     * re-running vcpkg install could not change anything (~1-3 s saved) */
    const std::filesystem::path spec_path = build_dir() / "packages" / "vcpkg-truss-spec.json";
    std::string fingerprint;
    if (auto fp = fs::sha256_file(manifest); fp)
        fingerprint = *fp + "-" + triplet;
    bool skip_install = false;
    if (!fingerprint.empty() && std::filesystem::is_directory(tdir, ec))
        if (auto cached = detail::read_file(spec_path); cached) {
            const auto j = nlohmann::json::parse(*cached, nullptr, false);
            /* .value() would throw on a non-string key: use find() */
            const auto fp = j.is_object() ? j.find("fingerprint") : j.end();
            skip_install = fp != j.end() && fp->is_string() && fp->get<std::string>() == fingerprint;
        }
    if (skip_install) {
        log::info("vcpkg: up to date (skipped vcpkg install)");
    } else {
        log::info("vcpkg install (" + triplet + ")");
        /* manifest mode is auto-detected from vcpkg.json in the process cwd:
         * pass the project root explicitly instead of assuming they match */
        const std::vector<std::string> argv {exe, "install", "--triplet", triplet,
                                             "--x-install-root", install_root.generic_string(),
                                             "--no-print-usage"};
        auto outcome = detail::run_process(argv, false, cwd_, 900);
        if (!outcome)
            return std::unexpected(outcome.error());
        if (outcome->exit_code != 0)
            return std::unexpected(Error {
                Error::Kind::Fetch,
                "vcpkg install failed (exit " + std::to_string(outcome->exit_code) + ")",
            });
    }
    auto& iface = add_interface_library("vcpkg");
    if (std::filesystem::is_directory(tdir / "include", ec))
        iface.system_include_dirs((tdir / "include").generic_string());
    const std::filesystem::path libdir = tdir / "lib";
    if (std::filesystem::is_directory(libdir, ec)) {
        iface.link_paths(libdir.generic_string());
        std::vector<std::string> names;
        std::unordered_set<std::string> seen;
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
            if (!base.empty() && seen.insert(base).second)
                names.push_back(base);
        }
        if (auto r = check_spec_values(names, "library"); !r)
            return std::unexpected(r.error());
        for (const auto& n : names)
            iface.link_libs(n);
    }
    if (!fingerprint.empty())
        if (auto r = detail::write_file(spec_path,
                                        nlohmann::json {{"fingerprint", fingerprint}}.dump(2) + "\n");
            !r)
            return std::unexpected(r.error());
    return TargetRef{iface};
}

} // namespace truss
