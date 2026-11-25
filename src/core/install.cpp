module;
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

module truss;
import :api;
import :impl;

namespace truss::detail {
namespace {

std::filesystem::path inst_resolve_prefix(const std::filesystem::path& cwd,
                                          const std::optional<std::filesystem::path>& prefix_opt) {
    std::filesystem::path prefix;
    if (prefix_opt.has_value() && !prefix_opt->empty()) {
        prefix = *prefix_opt;
        if (prefix.is_relative())
            prefix = cwd / prefix;
    } else {
        const std::string home = os::env("HOME").value_or(".");
        prefix = std::filesystem::path(home) / ".local";
    }
    prefix = prefix.lexically_normal();

    if (auto destdir = os::env("DESTDIR"); destdir && !destdir->empty()) {
        std::filesystem::path rel = prefix;
        if (rel.is_absolute())
            rel = rel.relative_path();
        prefix = (std::filesystem::path(*destdir) / rel).lexically_normal();
    }
    return prefix;
}

Result<void> inst_copy_file_mode(const std::filesystem::path& src,
                                 const std::filesystem::path& dst,
                                 mode_t mode) {
    std::error_code ec;
    if (dst.has_parent_path()) {
        std::filesystem::create_directories(dst.parent_path(), ec);
        if (ec)
            return std::unexpected(Error {
                Error::Kind::Io,
                "install: failed to create directory '" + dst.parent_path().generic_string() + "': " + ec.message(),
                dst.parent_path(),
            });
    }
    static std::atomic<unsigned> seq {0};
    const std::filesystem::path tmp(
        dst.generic_string() + ".truss-tmp." + std::to_string(::getpid()) + "."
        + std::to_string(seq.fetch_add(1, std::memory_order_relaxed)));

    std::filesystem::copy_file(src, tmp, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return std::unexpected(Error {
            Error::Kind::Io,
            "install: failed to copy '" + src.generic_string() + "' to '" + dst.generic_string() + "': " + ec.message(),
            dst,
        });
    }
    if (::chmod(tmp.c_str(), mode) != 0) {
        const std::string why = std::strerror(errno);
        std::filesystem::remove(tmp, ec);
        return std::unexpected(Error {
            Error::Kind::Io,
            "install: failed to set permissions on '" + dst.generic_string() + "': " + why,
            dst,
        });
    }
    std::filesystem::rename(tmp, dst, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return std::unexpected(Error {
            Error::Kind::Io,
            "install: failed to install '" + dst.generic_string() + "': " + ec.message(),
            dst,
        });
    }
    return {};
}

Result<void> inst_strip_file(const std::filesystem::path& dst, TargetKind kind) {
    const auto strip_bin = os::find_program({"llvm-strip", "strip"});
    if (!strip_bin)
        return std::unexpected(Error {
            Error::Kind::Toolchain,
            "install --strip: neither 'llvm-strip' nor 'strip' found on PATH",
        });
    std::vector<std::string> argv {strip_bin->generic_string()};
    if (kind == TargetKind::SharedLibrary)
        argv.push_back("--strip-unneeded");
    argv.push_back(dst.generic_string());
    auto res = detail::run_process(argv, true, 120);
    if (!res)
        return std::unexpected(res.error());
    if (res->exit_code != 0)
        return std::unexpected(Error {
            Error::Kind::Command,
            "install --strip: strip failed on '" + dst.generic_string() + "'"
                + (res->output.empty() ? "" : ":\n" + res->output),
            dst,
        });
    return {};
}

} // namespace

Result<void> run_install(const std::filesystem::path& cwd,
                         const std::filesystem::path& build_dir,
                         const std::optional<std::filesystem::path>& prefix_opt,
                         bool strip,
                         bool dry_run,
                         std::span<const InstallItem> items) {
    const std::filesystem::path root = inst_resolve_prefix(cwd, prefix_opt);
    std::vector<std::string> manifest_lines;

    for (const auto& item : items) {
        const Target& t = *item.target;
        if (t.is_installed() && !item.output.empty()) {
            const std::filesystem::path src(item.output);
            std::error_code ec;
            if (!dry_run && !std::filesystem::is_regular_file(src, ec))
                return std::unexpected(Error {
                    Error::Kind::Io,
                    "install: built artifact not found at '" + src.generic_string() + "'",
                    src,
                });

            std::string subdir = t.install_subdir();
            if (subdir.empty())
                subdir = (t.kind() == TargetKind::Executable) ? "bin" : "lib";
            const std::filesystem::path dst = root / subdir / src.filename();
            const mode_t mode = (t.kind() == TargetKind::StaticLibrary) ? 0644 : 0755;

            if (dry_run) {
                log::info("dry run: would install " + dst.generic_string());
            } else {
                if (auto r = inst_copy_file_mode(src, dst, mode); !r)
                    return r;
                if (strip && (t.kind() == TargetKind::Executable || t.kind() == TargetKind::SharedLibrary)) {
                    if (auto r = inst_strip_file(dst, t.kind()); !r)
                        return r;
                }
                log::info("installed " + dst.generic_string());
            }
            manifest_lines.push_back(dst.generic_string());
        }

        for (const auto& hi : t.installed_headers()) {
            const std::filesystem::path src_dir = std::filesystem::path(hi.dir).is_absolute()
                                                      ? std::filesystem::path(hi.dir)
                                                      : (cwd / hi.dir);
            std::error_code ec;
            if (!std::filesystem::is_directory(src_dir, ec))
                return std::unexpected(Error {
                    Error::Kind::Config,
                    "target '" + t.name() + "': install_headers('" + hi.dir + "') directory not found",
                    src_dir,
                });

            std::filesystem::path dst_base = root / "include";
            if (!hi.subdir.empty())
                dst_base /= hi.subdir;

            std::vector<std::filesystem::path> files;
            for (auto it = std::filesystem::recursive_directory_iterator(
                     src_dir, std::filesystem::directory_options::skip_permission_denied, ec);
                 it != std::filesystem::recursive_directory_iterator {}; it.increment(ec)) {
                if (ec)
                    break;
                const std::string fname = it->path().filename().generic_string();
                if (!fname.empty() && fname.front() == '.') {
                    if (it->is_directory(ec))
                        it.disable_recursion_pending();
                    continue;
                }
                if (it->is_symlink(ec)) {
                    if (it->is_directory(ec))
                        it.disable_recursion_pending();
                    continue;
                }
                if (it->is_regular_file(ec))
                    files.push_back(it->path());
            }
            std::ranges::sort(files);

            for (const auto& f : files) {
                const auto rel = f.lexically_relative(src_dir);
                const std::filesystem::path dst = dst_base / rel;
                if (dry_run) {
                    log::info("dry run: would install " + dst.generic_string());
                } else {
                    if (auto r = inst_copy_file_mode(f, dst, 0644); !r)
                        return r;
                }
                manifest_lines.push_back(dst.generic_string());
            }
        }
    }

    if (dry_run)
        return {};

    std::ranges::sort(manifest_lines);
    manifest_lines.erase(std::ranges::unique(manifest_lines).begin(), manifest_lines.end());
    std::string manifest_text;
    for (const auto& line : manifest_lines) {
        manifest_text += line;
        manifest_text += '\n';
    }
    const std::filesystem::path manifest_path = build_dir / "install_manifest.txt";
    if (auto r = detail::write_file(manifest_path, manifest_text); !r)
        return r;
    log::success("installed " + std::to_string(manifest_lines.size()) + " file(s) into " + root.generic_string());
    return {};
}

Result<void> run_uninstall(const std::filesystem::path& build_dir, bool dry_run) {
    const std::filesystem::path manifest_path = build_dir / "install_manifest.txt";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(manifest_path, ec))
        return std::unexpected(Error {
            Error::Kind::Config,
            "uninstall: install manifest not found at '" + manifest_path.generic_string()
                + "' (run ./truss install first)",
            manifest_path,
        });

    auto text = detail::read_file(manifest_path);
    if (!text)
        return std::unexpected(text.error());

    std::size_t removed = 0;
    std::size_t pos = 0;
    while (pos < text->size()) {
        std::size_t nl = text->find('\n', pos);
        if (nl == std::string::npos)
            nl = text->size();
        std::string_view line(text->data() + pos, nl - pos);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        pos = nl + 1;
        if (line.empty())
            continue;
        const std::filesystem::path p(line);
        if (dry_run) {
            log::info("dry run: would remove " + p.generic_string());
            ++removed;
            continue;
        }
        if (std::filesystem::exists(p, ec)) {
            if (!std::filesystem::remove(p, ec))
                return std::unexpected(Error {
                    Error::Kind::Io,
                    "uninstall: failed to remove '" + p.generic_string() + "': " + ec.message(),
                    p,
                });
            ++removed;
        }
    }

    if (dry_run)
        return {};

    std::filesystem::remove(manifest_path, ec);
    log::success("uninstalled " + std::to_string(removed) + " file(s)");
    return {};
}

} // namespace truss::detail
