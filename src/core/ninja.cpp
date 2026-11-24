module;
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module truss;
import :api;
import :impl;

namespace truss::detail {
namespace {

/* escaping appends straight into the manifest buffer: the emitter is the
 * per-edge hot path, so no temporary string per token. CR/LF are stripped as
 * defense-in-depth so no caller can break out of a ninja binding/statement. */
void append_value(std::string& out, std::string_view v) {
    for (const char c : v) {
        if (c == '\r' || c == '\n') continue;
        if (c == '$') out += "$$";
        else out += c;
    }
}

void append_path(std::string& out, std::string_view p) {
    for (const char c : p) {
        switch (c) {
        case '\r': case '\n': break;
        case '$': out += "$$"; break;
        case ':': out += "$:"; break;
        case ' ': out += "$ "; break;
        default: out += c; break;
        }
    }
}

void append_paths(std::string& out, const std::vector<std::string>& paths) {
    for (const auto& p : paths) {
        out += ' ';
        append_path(out, p);
    }
}

void append_binding(std::string& out, std::string_view key, std::string_view value, bool escape) {
    out += "  ";
    out += key;
    out += " = ";
    if (escape) append_value(out, value);
    else out += value;
    out += '\n';
}

} // namespace

std::string esc_value(std::string_view v) {
    std::string out;
    out.reserve(v.size());
    append_value(out, v);
    return out;
}

std::string esc_path(std::string_view p) {
    std::string out;
    out.reserve(p.size());
    append_path(out, p);
    return out;
}

void NinjaWriter::comment(std::string_view text) {
    buf_ += "# ";
    buf_ += text;
    buf_ += '\n';
}

void NinjaWriter::var(std::string_view name, std::string_view value) {
    buf_ += name;
    buf_ += " = ";
    append_value(buf_, value);
    buf_ += '\n';
}

void NinjaWriter::rule(std::string_view name, const std::vector<std::pair<std::string, std::string>>& body) {
    buf_ += "rule ";
    buf_ += name;
    buf_ += '\n';
    // NOTE: rule bodies carry ninja $placeholders verbatim — do not escape.
    for (const auto& [k, v] : body)
        append_binding(buf_, k, v, false);
    buf_ += '\n';
}

void NinjaWriter::build(const Edge& e) {
    buf_ += "build";
    append_paths(buf_, e.outputs);
    if (!e.implicit_outs.empty()) {
        buf_ += " |";
        append_paths(buf_, e.implicit_outs);
    }
    buf_ += ": ";
    buf_ += e.rule;
    append_paths(buf_, e.inputs);
    if (!e.implicit_ins.empty()) {
        buf_ += " |";
        append_paths(buf_, e.implicit_ins);
    }
    buf_ += '\n';
    for (const auto& [k, v] : e.vars)
        append_binding(buf_, k, v, true);
    buf_ += '\n';
}

void NinjaWriter::phony(std::string_view name, std::vector<std::string> ins) {
    Edge e;
    e.outputs = {std::string(name)};
    e.rule = "phony";
    e.inputs = std::move(ins);
    build(e);
}

void NinjaWriter::default_targets(const std::vector<std::string>& names) {
    buf_ += "default";
    append_paths(buf_, names);
    buf_ += '\n';
}

} // namespace truss::detail
