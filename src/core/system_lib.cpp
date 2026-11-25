module;
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

module truss;
import :api;
import :impl;

namespace truss {
namespace {

std::vector<std::string> sys_split_shell_tokens(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i >= text.size())
            break;
        std::string tok;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) {
            if (text[i] == '\\' && i + 1 < text.size()) {
                tok.push_back(text[++i]);
                ++i;
            } else if (text[i] == '\'' || text[i] == '"') {
                const char q = text[i++];
                while (i < text.size() && text[i] != q) {
                    if (q == '"' && text[i] == '\\' && i + 1 < text.size())
                        tok.push_back(text[++i]);
                    else
                        tok.push_back(text[i]);
                    ++i;
                }
                if (i < text.size())
                    ++i;
            } else {
                tok.push_back(text[i++]);
            }
        }
        if (!tok.empty())
            out.push_back(std::move(tok));
    }
    return out;
}

bool sys_token_is_safe(std::string_view tok) {
    for (std::size_t i = 0; i < tok.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(tok[i]);
        if (c < 0x20 || c == 0x7f)
            return false;
        if (c == '`' || c == ';' || c == '|' || c == '&' || c == '>' || c == '<')
            return false;
        if (c == '$' && i + 1 < tok.size() && tok[i + 1] == '(')
            return false;
    }
    return true;
}

std::string sys_target_name(const SystemLibSpec& spec) {
    std::string out = "sys_";
    out.reserve(4 + spec.name.size() + 10);
    for (const unsigned char uc : spec.name) {
        const char c = static_cast<char>(uc);
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                     || (c >= '0' && c <= '9') || c == '-' || c == '.';
        if (ok) {
            out.push_back(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "_%02x", static_cast<unsigned>(uc));
            out += buf;
        }
    }
    const bool has_custom_spec = !spec.use_pkg_config || !spec.min_version.empty()
                              || !spec.headers.empty() || !spec.libs.empty() || !spec.paths.empty();
    if (has_custom_spec) {
        std::uint64_t h = 14695981039346656037ULL;
        auto mix = [&](std::string_view s) {
            for (const unsigned char c : s) {
                h ^= c;
                h *= 1099511628211ULL;
            }
            h ^= 0xffU;
            h *= 1099511628211ULL;
        };
        mix(spec.use_pkg_config ? "1" : "0");
        mix(spec.min_version);
        for (const auto& s : spec.headers) mix(s);
        for (const auto& s : spec.libs) mix(s);
        for (const auto& s : spec.paths) mix(s);
        char buf[20];
        std::snprintf(buf, sizeof(buf), "_%08x", static_cast<unsigned>(h ^ (h >> 32)));
        out += buf;
    }
    return out;
}

} // namespace

Result<TargetRef> Project::resolve_system_lib(const SystemLibSpec& spec) {
    if (spec.name.empty() || spec.name.starts_with('-')
        || spec.name.find_first_of(" \t\r\n") != std::string::npos) {
        return std::unexpected(Error {
            Error::Kind::Config,
            "system_lib(): invalid library name '" + spec.name + "'",
        });
    }

    const std::string target_name = sys_target_name(spec);
    for (const auto& t : targets_) {
        if (t->name() == target_name && t->kind() == TargetKind::Interface)
            return TargetRef {*t};
    }

    std::string pkg_err;
    if (spec.use_pkg_config) {
        if (!os::program_exists("pkg-config")) {
            pkg_err = "pkg-config not found on PATH";
        } else {
            std::vector<std::string> check_argv {"pkg-config"};
            if (!spec.min_version.empty())
                check_argv.push_back("--atleast-version=" + spec.min_version);
            else
                check_argv.push_back("--exists");
            check_argv.push_back("--");
            check_argv.push_back(spec.name);
            auto chk = detail::run_process(check_argv, true, 30);
            if (chk && chk->exit_code == 0) {
                auto cflags_res = detail::run_process_stdout(
                    std::vector<std::string> {"pkg-config", "--cflags", "--", spec.name}, 30);
                auto libs_res = detail::run_process_stdout(
                    std::vector<std::string> {"pkg-config", "--libs", "--", spec.name}, 30);
                if (cflags_res && cflags_res->exit_code == 0 && libs_res && libs_res->exit_code == 0) {
                    const auto cflags_toks = sys_split_shell_tokens(cflags_res->output);
                    const auto libs_toks = sys_split_shell_tokens(libs_res->output);
                    for (const auto& tok : cflags_toks) {
                        if (!sys_token_is_safe(tok))
                            return std::unexpected(Error {
                                Error::Kind::Config,
                                "pkg-config '" + spec.name + "' emitted unsafe cflag token '" + tok + "'",
                            });
                    }
                    for (const auto& tok : libs_toks) {
                        if (!sys_token_is_safe(tok))
                            return std::unexpected(Error {
                                Error::Kind::Config,
                                "pkg-config '" + spec.name + "' emitted unsafe lib token '" + tok + "'",
                            });
                    }

                    Interface& iface = add_interface_library(target_name);
                    for (std::size_t i = 0; i < cflags_toks.size(); ++i) {
                        const std::string& tok = cflags_toks[i];
                        if (tok == "-I" && i + 1 < cflags_toks.size()) {
                            iface.system_include_dirs(cflags_toks[++i]);
                        } else if (tok.starts_with("-I") && tok.size() > 2) {
                            iface.system_include_dirs(tok.substr(2));
                        } else if (tok == "-isystem" && i + 1 < cflags_toks.size()) {
                            iface.system_include_dirs(cflags_toks[++i]);
                        } else if (tok == "-D" && i + 1 < cflags_toks.size()) {
                            iface.defines(cflags_toks[++i]);
                        } else if (tok.starts_with("-D") && tok.size() > 2) {
                            iface.defines(tok.substr(2));
                        } else if (!tok.empty()) {
                            iface.public_flags(tok);
                        }
                    }
                    for (std::size_t i = 0; i < libs_toks.size(); ++i) {
                        const std::string& tok = libs_toks[i];
                        if (tok == "-L" && i + 1 < libs_toks.size()) {
                            iface.link_paths(libs_toks[++i]);
                        } else if (tok.starts_with("-L") && tok.size() > 2) {
                            iface.link_paths(tok.substr(2));
                        } else if (tok == "-l" && i + 1 < libs_toks.size()) {
                            const std::string& lib = libs_toks[++i];
                            if (!lib.starts_with('-'))
                                iface.link_libs(lib);
                        } else if (tok.starts_with("-l") && tok.size() > 2) {
                            iface.link_libs(tok.substr(2));
                        } else if (tok == "-pthread") {
                            iface.public_flags("-pthread");
                        }
                    }
                    return TargetRef {iface};
                }
                pkg_err = "pkg-config --cflags/--libs failed for '" + spec.name + "'";
            } else {
                pkg_err = "pkg-config could not find '" + spec.name + "'"
                        + (spec.min_version.empty() ? "" : " (>= " + spec.min_version + ")");
            }
        }
    }

    const bool try_manual = !spec.use_pkg_config
                         || !spec.headers.empty() || !spec.libs.empty() || !spec.paths.empty();
    if (!try_manual)
        return std::unexpected(Error {Error::Kind::Config, pkg_err});

    std::vector<std::filesystem::path> inc_dirs;
    std::vector<std::filesystem::path> lib_dirs;
    for (const auto& p : spec.paths) {
        const std::filesystem::path base(p);
        inc_dirs.push_back(base / "include");
        inc_dirs.push_back(base);
        lib_dirs.push_back(base / "lib");
        lib_dirs.push_back(base / "lib64");
        lib_dirs.push_back(base);
    }
    if (spec.paths.empty()) {
        inc_dirs.emplace_back("/usr/local/include");
        inc_dirs.emplace_back("/usr/include");
    }

    std::vector<std::string> found_inc_dirs;
    std::error_code ec;
    for (const auto& hdr : spec.headers) {
        bool found = false;
        for (const auto& dir : inc_dirs) {
            if (std::filesystem::is_regular_file(dir / hdr, ec)) {
                const std::string s = dir.generic_string();
                if (std::ranges::find(found_inc_dirs, s) == found_inc_dirs.end())
                    found_inc_dirs.push_back(s);
                found = true;
                break;
            }
        }
        if (!found) {
            return std::unexpected(Error {
                Error::Kind::Config,
                "system_lib('" + spec.name + "'): header '" + hdr + "' not found",
            });
        }
    }

    std::vector<std::string> want_libs = spec.libs;
    if (want_libs.empty() && spec.headers.empty())
        want_libs.push_back(spec.name);

    std::vector<std::string> found_lib_dirs;
    for (const auto& lib : want_libs) {
        std::optional<std::filesystem::path> hit;
        if (!lib_dirs.empty())
            hit = fs::find_library(lib, lib_dirs);
        if (!hit && spec.paths.empty())
            hit = fs::find_library(lib);
        if (!hit) {
            return std::unexpected(Error {
                Error::Kind::Config,
                "system_lib('" + spec.name + "'): library '" + lib + "' not found",
            });
        }
        const std::string dir = hit->parent_path().generic_string();
        if (!dir.empty() && std::ranges::find(found_lib_dirs, dir) == found_lib_dirs.end())
            found_lib_dirs.push_back(dir);
    }

    Interface& iface = add_interface_library(target_name);
    for (const auto& d : found_inc_dirs)
        iface.system_include_dirs(d);
    for (const auto& d : found_lib_dirs)
        iface.link_paths(d);
    for (const auto& l : want_libs)
        iface.link_libs(l);
    return TargetRef {iface};
}

} // namespace truss
