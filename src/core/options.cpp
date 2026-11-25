module;
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module truss;
import :api;
import :impl;

namespace truss {
namespace {

std::string opt_ascii_lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s)
        out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    return out;
}

std::optional<bool> opt_parse_bool(std::string_view s) {
    const std::string l = opt_ascii_lower(s);
    if (l == "1" || l == "true" || l == "on" || l == "yes" || l == "y")
        return true;
    if (l == "0" || l == "false" || l == "off" || l == "no" || l == "n")
        return false;
    return std::nullopt;
}

bool opt_valid_name(std::string_view s) {
    if (s.empty())
        return false;
    for (const char c : s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7f || c == '=' || c == '.' || c == ' ' || c == '\t')
            return false;
    }
    return true;
}

bool opt_valid_c_ident(std::string_view s) {
    if (s.empty())
        return false;
    auto is_alpha = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
    auto is_alnum = [&](char c) { return is_alpha(c) || (c >= '0' && c <= '9'); };
    if (!is_alpha(s.front()))
        return false;
    for (const char c : s.substr(1))
        if (!is_alnum(c))
            return false;
    return true;
}

bool opt_valid_value(std::string_view s) {
    for (const char c : s) {
        if (c == '\n' || c == '\r' || c == '\0')
            return false;
    }
    return true;
}

/* Format a string option value as a POSIX-shell-safe C string literal so
 * `/bin/sh -c` delivers `-DNAME="val"` intact to the compiler (single-quoted
 * for the shell, C-escaped inside the double quotes). */
std::string opt_shell_safe_c_string(std::string_view s) {
    std::string out = "'\"";
    out.reserve(s.size() + 6);
    for (const char c : s) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out += "\"'";
    return out;
}

std::string_view opt_kind_label(OptionSpec::Kind k) {
    switch (k) {
    case OptionSpec::Kind::Bool: return "bool";
    case OptionSpec::Kind::Int: return "int";
    case OptionSpec::Kind::String: return "string";
    case OptionSpec::Kind::Choice: return "choice";
    }
    return "string";
}

bool opt_kind_matches(OptionSpec::Kind actual, OptionSpec::Kind expected) {
    if (expected == OptionSpec::Kind::String)
        return actual == OptionSpec::Kind::String || actual == OptionSpec::Kind::Choice;
    return actual == expected;
}

} // namespace

void Target::apply_passthrough_options(std::span<const OptionSpec> specs, bool is_public, std::string_view fn_name) {
    if (kind_ == TargetKind::Interface || kind_ == TargetKind::ImportedLibrary) {
        defer_error(Error {
            Error::Kind::Config,
            std::string(fn_name) + " is only supported on compiled targets (not Interface/ImportedLibrary '" + name_ + "')",
        });
        return;
    }
    if (!owner_) {
        defer_error(Error {
            Error::Kind::Config,
            "target '" + name_ + "' has no owning Project for " + std::string(fn_name),
        });
        return;
    }
    for (const auto& s : specs) {
        if (!opt_valid_c_ident(s.name)) {
            defer_error(Error {
                Error::Kind::Config,
                std::string(fn_name) + ": option name '" + s.name + "' is not a valid C preprocessor identifier",
            });
            continue;
        }
        const std::string val = owner_->register_option_value(name_, s);
        std::string def;
        if (s.kind == OptionSpec::Kind::Bool) {
            if (val != "true")
                continue;
            def = s.name + "=1";
        } else if (s.kind == OptionSpec::Kind::Int) {
            def = s.name + "=" + val;
        } else {
            def = s.name + "=" + opt_shell_safe_c_string(val);
        }
        if (is_public)
            public_defines(def);
        else
            defines(def);
    }
}

void Project::load_options_cache() {
    cache_loaded_ = true;
    cached_options_.clear();
    const std::filesystem::path p = build_dir() / "options.cache";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec))
        return;
    auto text = detail::read_file(p);
    if (!text)
        return;
    std::size_t pos = 0;
    while (pos < text->size()) {
        std::size_t nl = text->find('\n', pos);
        if (nl == std::string::npos)
            nl = text->size();
        std::string_view line(text->data() + pos, nl - pos);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        pos = nl + 1;
        if (line.empty() || line.front() == '#')
            continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0)
            continue;
        cached_options_.emplace_back(std::string(line.substr(0, eq)), std::string(line.substr(eq + 1)));
    }
}

std::string Project::register_option_value(std::string_view scope, const OptionSpec& spec) {
    if (!cache_loaded_)
        load_options_cache();

    const std::string sc = scope.empty() ? name_ : std::string(scope);
    const std::string full_key = sc + "." + spec.name;

    auto set_err = [&](std::string msg) {
        if (!options_error_)
            options_error_ = Error {Error::Kind::Config, std::move(msg)};
    };

    if (!opt_valid_name(spec.name))
        set_err("invalid option name '" + spec.name + "' (must be non-empty without spaces, '=', or '.')");
    if (spec.kind == OptionSpec::Kind::Choice) {
        if (spec.choices.empty())
            set_err("choice option '" + full_key + "' must have at least one allowed value");
        else if (std::ranges::find(spec.choices, spec.default_str) == spec.choices.end())
            set_err("choice option '" + full_key + "' default '" + spec.default_str + "' is not in allowed choices");
    }
    for (const auto& r : registered_options_) {
        if (r.scope == sc && r.spec.name == spec.name) {
            set_err("option '" + full_key + "' is registered twice");
            break;
        }
    }

    /* Precedence: CLI scoped > CLI unscoped > options.cache > default */
    const CliOption* matched_cli = nullptr;
    for (const auto& c : cli_options_) {
        if (c.key == full_key)
            matched_cli = &c;
    }
    if (!matched_cli) {
        for (const auto& c : cli_options_) {
            if (c.key == spec.name)
                matched_cli = &c;
        }
    }

    std::string raw = spec.default_str;
    bool from_cli = false;
    bool from_cache = false;

    if (matched_cli) {
        if (matched_cli->bare && spec.kind != OptionSpec::Kind::Bool) {
            set_err("option '-D" + matched_cli->key + "' without '=VALUE' is only valid for bool options");
        } else {
            raw = matched_cli->value;
            from_cli = true;
        }
    } else {
        for (const auto& [k, v] : cached_options_) {
            if (k == full_key) {
                raw = v;
                from_cache = true;
                break;
            }
        }
    }

    std::string normalized = raw;
    if (spec.kind == OptionSpec::Kind::Bool) {
        if (auto b = opt_parse_bool(raw)) {
            normalized = *b ? "true" : "false";
        } else {
            set_err("option '" + full_key + "': invalid bool value '" + raw + "' (expected true/false/1/0/on/off)");
            normalized = spec.default_str;
        }
    } else if (spec.kind == OptionSpec::Kind::Int) {
        std::int64_t parsed {};
        const auto [end, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), parsed);
        if (ec != std::errc {} || end != raw.data() + raw.size()) {
            set_err("option '" + full_key + "': invalid integer value '" + raw + "'");
            normalized = spec.default_str;
        } else {
            normalized = std::to_string(parsed);
        }
    } else if (spec.kind == OptionSpec::Kind::Choice) {
        if (std::ranges::find(spec.choices, raw) == spec.choices.end()) {
            std::string allowed;
            for (std::size_t i = 0; i < spec.choices.size(); ++i) {
                if (i > 0) allowed += ", ";
                allowed += spec.choices[i];
            }
            set_err("option '" + full_key + "': invalid choice '" + raw + "' (allowed: " + allowed + ")");
            normalized = spec.default_str;
        }
    } else {
        if (!opt_valid_value(raw)) {
            set_err("option '" + full_key + "': value cannot contain newlines");
            normalized = spec.default_str;
        }
    }

    registered_options_.push_back(RegisteredOption {
        sc, spec, normalized, from_cli, from_cache,
    });
    return normalized;
}

std::optional<std::string> Project::lookup_option_value(std::string_view scope, std::string_view name,
                                                        OptionSpec::Kind expected) const {
    auto set_err = [&](std::string msg) {
        if (!options_error_)
            options_error_ = Error {Error::Kind::Config, std::move(msg)};
    };

    const RegisteredOption* match = nullptr;
    if (!scope.empty()) {
        for (const auto& r : registered_options_) {
            if (r.scope == scope && r.spec.name == name) {
                match = &r;
                break;
            }
        }
        if (!match) {
            set_err("get(): unknown option '" + std::string(scope) + "." + std::string(name) + "'");
            return std::nullopt;
        }
    } else if (const std::size_t dot = name.rfind('.'); dot != std::string_view::npos) {
        const std::string_view sc = name.substr(0, dot);
        const std::string_view key = name.substr(dot + 1);
        for (const auto& r : registered_options_) {
            if (r.scope == sc && r.spec.name == key) {
                match = &r;
                break;
            }
        }
        if (!match) {
            set_err("get(): unknown option '" + std::string(name) + "'");
            return std::nullopt;
        }
    } else {
        for (const auto& r : registered_options_) {
            if (r.spec.name == name) {
                if (match) {
                    set_err("get(): option '" + std::string(name) + "' is ambiguous across scopes; specify scope");
                    return std::nullopt;
                }
                match = &r;
            }
        }
        if (!match) {
            set_err("get(): unknown option '" + std::string(name) + "'");
            return std::nullopt;
        }
    }

    if (!opt_kind_matches(match->spec.kind, expected)) {
        set_err("get(): option '" + match->scope + "." + match->spec.name + "' has type "
                + std::string(opt_kind_label(match->spec.kind)) + ", not "
                + std::string(opt_kind_label(expected)));
        return std::nullopt;
    }
    return match->value;
}

Result<void> Project::validate_options() const {
    if (options_error_)
        return std::unexpected(*options_error_);

    for (const auto& c : cli_options_) {
        if (c.key.contains('.')) {
            bool matched = false;
            for (const auto& r : registered_options_) {
                if (r.scope + "." + r.spec.name == c.key) {
                    matched = true;
                    break;
                }
            }
            if (!matched)
                return std::unexpected(Error {
                    Error::Kind::Config,
                    "unknown option '-D" + c.key + "' (run ./truss options to list available options)",
                });
        } else {
            std::vector<std::string> scopes;
            for (const auto& r : registered_options_)
                if (r.spec.name == c.key)
                    scopes.push_back(r.scope);
            if (scopes.empty())
                return std::unexpected(Error {
                    Error::Kind::Config,
                    "unknown option '-D" + c.key + "' (run ./truss options to list available options)",
                });
            if (scopes.size() > 1) {
                std::string list;
                for (std::size_t i = 0; i < scopes.size(); ++i) {
                    if (i > 0) list += ", ";
                    list += "-D" + scopes[i] + "." + c.key;
                }
                return std::unexpected(Error {
                    Error::Kind::Config,
                    "option '-D" + c.key + "' is ambiguous across scopes; specify one of: " + list,
                });
            }
        }
    }
    return {};
}

Result<void> Project::save_options_cache() const {
    if (registered_options_.empty() && cached_options_.empty())
        return {};

    std::map<std::string, std::string> merged;
    for (const auto& [k, v] : cached_options_)
        merged[k] = v;
    for (const auto& r : registered_options_) {
        const std::string key = r.scope + "." + r.spec.name;
        if (r.value != r.spec.default_str)
            merged[key] = r.value;
        else
            merged.erase(key);
    }

    const std::filesystem::path p = build_dir() / "options.cache";
    if (merged.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(p, ec))
            std::filesystem::remove(p, ec);
        return {};
    }

    std::string out = "# truss options cache (auto-generated)\n";
    for (const auto& [k, v] : merged)
        out += k + "=" + v + "\n";
    return detail::write_file_if_changed(p, out);
}

Result<void> Project::reset_options_cache() const {
    const std::filesystem::path p = build_dir() / "options.cache";
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) {
        if (!std::filesystem::remove(p, ec))
            return std::unexpected(Error {Error::Kind::Io, "failed to remove options cache", p});
    }
    return {};
}

void Project::print_options() const {
    if (registered_options_.empty()) {
        std::puts("no build options defined (register with proj.options(...) or target.options(...))");
        return;
    }

    std::vector<std::string> scopes;
    for (const auto& r : registered_options_)
        if (std::ranges::find(scopes, r.scope) == scopes.end())
            scopes.push_back(r.scope);

    std::printf("options (%s):\n", mode_ == BuildMode::Debug ? "debug" : "release");
    for (const auto& sc : scopes) {
        std::printf("\n  [%s]\n", sc.c_str());
        std::size_t width = 0;
        for (const auto& r : registered_options_)
            if (r.scope == sc)
                width = std::max(width, r.spec.name.size());

        for (const auto& r : registered_options_) {
            if (r.scope != sc)
                continue;
            const bool modified = (r.value != r.spec.default_str);
            const std::string_view kind = opt_kind_label(r.spec.kind);
            std::printf("    %-*s = %-12s  (%.*s, default: %s%s)",
                        static_cast<int>(width), r.spec.name.c_str(), r.value.c_str(),
                        static_cast<int>(kind.size()), kind.data(), r.spec.default_str.c_str(),
                        modified ? ", modified" : "");
            if (!r.spec.description.empty())
                std::printf("  %s", r.spec.description.c_str());
            if (r.spec.kind == OptionSpec::Kind::Choice && !r.spec.choices.empty()) {
                std::printf("  [");
                for (std::size_t i = 0; i < r.spec.choices.size(); ++i) {
                    if (i > 0) std::printf("|");
                    std::printf("%s", r.spec.choices[i].c_str());
                }
                std::printf("]");
            }
            std::putchar('\n');
        }
    }
}

} // namespace truss
