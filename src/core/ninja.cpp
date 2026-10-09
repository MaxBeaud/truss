module;
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module truss;
import :api;
import :impl;

namespace truss::detail {

std::string esc_value(std::string_view v) {
    std::string out;
    out.reserve(v.size());
    for (const char c : v) {
        if (c == '$') out += "$$";
        else out += c;
    }
    return out;
}

std::string esc_path(std::string_view p) {
    std::string out;
    out.reserve(p.size());
    for (const char c : p) {
        switch (c) {
        case '$': out += "$$"; break;
        case ':': out += "$:"; break;
        case ' ': out += "$ "; break;
        default: out += c; break;
        }
    }
    return out;
}

void NinjaWriter::comment(std::string_view text) {
    buf_ += "# ";
    buf_.append(text);
    buf_ += '\n';
}

void NinjaWriter::var(std::string_view name, std::string_view value) {
    buf_ += std::string(name) + " = " + esc_value(value) + '\n';
}

void NinjaWriter::rule(std::string_view name, std::vector<std::pair<std::string, std::string>> body) {
    buf_ += "rule " + std::string(name) + '\n';
    // NOTE: rule bodies carry ninja $placeholders verbatim — do not escape.
    for (const auto& [k, v] : body)
        buf_ += "  " + k + " = " + v + '\n';
    buf_ += '\n';
}

void NinjaWriter::build(Edge e) {
    buf_ += "build";
    for (const auto& out : e.outputs)
        buf_ += " " + esc_path(out);
    buf_ += ": " + e.rule;
    for (const auto& in : e.inputs)
        buf_ += " " + esc_path(in);
    if (!e.implicit_ins.empty()) {
        buf_ += " |";
        for (const auto& in : e.implicit_ins)
            buf_ += " " + esc_path(in);
    }
    buf_ += '\n';
    for (const auto& [k, v] : e.vars)
        buf_ += "  " + k + " = " + esc_value(v) + '\n';
    buf_ += '\n';
}

void NinjaWriter::phony(std::string_view name, std::vector<std::string> ins) {
    Edge e;
    e.outputs = {std::string(name)};
    e.rule = "phony";
    e.inputs = std::move(ins);
    build(std::move(e));
}

void NinjaWriter::default_targets(std::vector<std::string> names) {
    buf_ += "default";
    for (const auto& n : names)
        buf_ += " " + esc_path(n);
    buf_ += '\n';
}

std::string NinjaWriter::str() const { return buf_; }

} // namespace truss::detail
