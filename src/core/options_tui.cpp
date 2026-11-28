module;
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <poll.h>
#include <termios.h>
#include <unistd.h>

module truss;
import :api;
import :impl;

namespace truss {
namespace {

void tui_write_all(std::string_view s) {
    std::size_t off = 0;
    while (off < s.size()) {
        const ssize_t n = ::write(STDOUT_FILENO, s.data() + off, s.size() - off);
        if (n <= 0)
            break;
        off += static_cast<std::size_t>(n);
    }
}

class RawTerminalGuard {
public:
    RawTerminalGuard() {
        if (::tcgetattr(STDIN_FILENO, &orig_) == 0) {
            termios raw = orig_;
            /* Clear ISIG so Ctrl-C arrives as 0x03 (mapped to Escape) and
             * unwinds RawTerminalGuard cleanly instead of dying mid-alt-screen. */
            raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | ISIG);
            raw.c_iflag &= ~static_cast<tcflag_t>(IXON | ICRNL);
            raw.c_cc[VMIN] = 1;
            raw.c_cc[VTIME] = 0;
            if (::tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
                active_ = true;
                tui_write_all("\x1b[?1049h\x1b[?25l");
            }
        }
    }
    RawTerminalGuard(const RawTerminalGuard&) = delete;
    RawTerminalGuard& operator=(const RawTerminalGuard&) = delete;
    ~RawTerminalGuard() {
        if (active_) {
            tui_write_all("\x1b[?25h\x1b[?1049l");
            ::tcsetattr(STDIN_FILENO, TCSANOW, &orig_);
        }
    }
    bool active() const { return active_; }

private:
    termios orig_ {};
    bool active_ {false};
};

std::string tui_read_key_seq() {
    char c = 0;
    if (::read(STDIN_FILENO, &c, 1) != 1)
        return {};
    std::string seq(1, c);
    if (c == '\x1b') {
        /* Distinguish bare Esc from an ANSI escape sequence (e.g. \x1b[A). */
        pollfd pfd {};
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        if (::poll(&pfd, 1, 25) > 0 && (pfd.revents & POLLIN) != 0) {
            char extra[7] {};
            const ssize_t n = ::read(STDIN_FILENO, extra, sizeof(extra));
            if (n > 0)
                seq.append(extra, static_cast<std::size_t>(n));
        }
    }
    return seq;
}

} // namespace

namespace detail {

TuiEvent decode_tui_key(std::string_view seq) {
    if (seq.empty())
        return {TuiKey::Unknown, 0};
    if (seq == "\x1b[A" || seq == "\x1bOA")
        return {TuiKey::Up, 0};
    if (seq == "\x1b[B" || seq == "\x1bOB")
        return {TuiKey::Down, 0};
    if (seq == "\x1b")
        return {TuiKey::Escape, 0};
    if (seq.size() == 1) {
        const unsigned char c = static_cast<unsigned char>(seq[0]);
        if (c == '\r' || c == '\n')
            return {TuiKey::Enter, 0};
        if (c == ' ')
            return {TuiKey::Space, ' '};
        if (c == 0x7f || c == 0x08)
            return {TuiKey::Backspace, 0};
        if (c == 0x03) /* Ctrl-C */
            return {TuiKey::Escape, 0};
        if (c >= 0x20 && c < 0x7f)
            return {TuiKey::Char, static_cast<char>(c)};
    }
    return {TuiKey::Unknown, 0};
}

OptionsTuiState::OptionsTuiState(std::vector<TuiOptionItem> items) {
    /* Group by scope in first-seen order so selected_ matches visual order. */
    std::vector<std::string> scopes;
    for (const auto& it : items) {
        if (std::ranges::find(scopes, it.scope) == scopes.end())
            scopes.push_back(it.scope);
    }
    items_.reserve(items.size());
    for (const auto& sc : scopes) {
        for (auto& it : items) {
            if (it.scope == sc)
                items_.push_back(std::move(it));
        }
    }
}

bool OptionsTuiState::handle_event(TuiEvent ev) {
    if (editing_) {
        if (ev.key == TuiKey::Escape) {
            editing_ = false;
            edit_buf_.clear();
            status_ = "Edit cancelled";
            return true;
        }
        if (ev.key == TuiKey::Backspace) {
            if (!edit_buf_.empty())
                edit_buf_.pop_back();
            return true;
        }
        if (ev.key == TuiKey::Space) {
            edit_buf_.push_back(' ');
            return true;
        }
        if (ev.key == TuiKey::Char) {
            if (static_cast<unsigned char>(ev.ch) >= 0x20 && static_cast<unsigned char>(ev.ch) < 0x7f)
                edit_buf_.push_back(ev.ch);
            return true;
        }
        if (ev.key == TuiKey::Enter) {
            auto& item = items_[selected_];
            if (item.spec.kind == OptionSpec::Kind::Int) {
                std::int64_t parsed {};
                const auto [end, ec] = std::from_chars(edit_buf_.data(), edit_buf_.data() + edit_buf_.size(), parsed);
                if (edit_buf_.empty() || ec != std::errc {} || end != edit_buf_.data() + edit_buf_.size()) {
                    status_ = "Invalid integer: '" + edit_buf_ + "'";
                    return true;
                }
                item.value = std::to_string(parsed);
            } else {
                if (!valid_option_string(edit_buf_)) {
                    status_ = "Invalid string value";
                    return true;
                }
                item.value = edit_buf_;
            }
            editing_ = false;
            edit_buf_.clear();
            status_.clear();
            return true;
        }
        return true;
    }

    if (ev.key == TuiKey::Escape || (ev.key == TuiKey::Char && (ev.ch == 'q' || ev.ch == 'Q'))) {
        quit_ = true;
        return false;
    }
    if (ev.key == TuiKey::Char && (ev.ch == 's' || ev.ch == 'S')) {
        saved_ = true;
        return false;
    }
    if (ev.key == TuiKey::Char && (ev.ch == 'r' || ev.ch == 'R')) {
        for (auto& it : items_)
            it.value = it.spec.default_str;
        status_ = "Reset all options to defaults (press s to save)";
        return true;
    }
    if (items_.empty())
        return true;

    if (ev.key == TuiKey::Up || (ev.key == TuiKey::Char && ev.ch == 'k')) {
        if (selected_ > 0)
            --selected_;
        status_.clear();
        return true;
    }
    if (ev.key == TuiKey::Down || (ev.key == TuiKey::Char && ev.ch == 'j')) {
        if (selected_ + 1 < items_.size())
            ++selected_;
        status_.clear();
        return true;
    }
    if (ev.key == TuiKey::Space || ev.key == TuiKey::Enter) {
        auto& item = items_[selected_];
        if (item.spec.kind == OptionSpec::Kind::Bool) {
            item.value = (item.value == "true") ? "false" : "true";
            status_.clear();
        } else if (item.spec.kind == OptionSpec::Kind::Choice) {
            if (!item.spec.choices.empty()) {
                const auto it = std::ranges::find(item.spec.choices, item.value);
                std::size_t idx = (it == item.spec.choices.end())
                                      ? 0
                                      : static_cast<std::size_t>(it - item.spec.choices.begin());
                idx = (idx + 1) % item.spec.choices.size();
                item.value = item.spec.choices[idx];
            }
            status_.clear();
        } else if (ev.key == TuiKey::Enter) {
            editing_ = true;
            edit_buf_ = item.value;
            status_ = "Editing " + item.spec.name + " (Enter to commit, Esc to cancel)";
        }
        return true;
    }
    return true;
}

std::string OptionsTuiState::render(std::string_view mode_label) const {
    std::string out;
    out.reserve(1024);
    out += "\x1b[2J\x1b[H";
    out += "\x1b[1mtruss configure\x1b[0m (";
    out += mode_label;
    out += ")\r\n\r\n";

    std::string cur_scope;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const auto& item = items_[i];
        if (item.scope != cur_scope) {
            if (!cur_scope.empty())
                out += "\r\n";
            cur_scope = item.scope;
            out += "\x1b[1;36m  [";
            out += cur_scope;
            out += "]\x1b[0m\r\n";
        }
        const bool sel = (i == selected_);
        const bool mod = (item.value != item.spec.default_str);
        if (sel)
            out += "\x1b[7m";
        out += sel ? "  > " : "    ";
        out += item.spec.name;
        out += " = ";
        if (sel && editing_) {
            out += edit_buf_;
            out += "_";
        } else {
            out += item.value;
        }
        out += "  (";
        out += option_kind_label(item.spec.kind);
        out += ", default: ";
        out += item.spec.default_str;
        if (mod)
            out += ", modified";
        out += ")";
        if (!item.spec.description.empty()) {
            out += "  ";
            out += item.spec.description;
        }
        if (item.spec.kind == OptionSpec::Kind::Choice && !item.spec.choices.empty()) {
            out += "  [";
            for (std::size_t c = 0; c < item.spec.choices.size(); ++c) {
                if (c > 0)
                    out += '|';
                out += item.spec.choices[c];
            }
            out += ']';
        }
        if (sel)
            out += "\x1b[0m";
        out += "\r\n";
    }

    out += "\r\n";
    if (!status_.empty()) {
        out += "\x1b[33m  ";
        out += status_;
        out += "\x1b[0m\r\n";
    }
    out += "\x1b[2m  Up/Down or j/k: move | Space/Enter: toggle/cycle/edit | s: save | r: reset | q/Esc: quit\x1b[0m\r\n";
    return out;
}

} // namespace detail

Result<void> Project::run_options_tui() {
    std::vector<detail::TuiOptionItem> items;
    items.reserve(registered_options_.size());
    for (const auto& r : registered_options_)
        items.push_back({r.scope, r.spec, r.value});

    detail::OptionsTuiState state(std::move(items));
    const std::string_view mode_label = (mode_ == BuildMode::Release) ? "release" : "debug";

    {
        RawTerminalGuard guard;
        if (!guard.active()) {
            if (auto r = save_options_cache(); !r)
                return r;
            print_options();
            return {};
        }
        for (;;) {
            tui_write_all(state.render(mode_label));
            tui_write_all(state.editing() ? "\x1b[?25h" : "\x1b[?25l");
            const std::string seq = tui_read_key_seq();
            if (seq.empty())
                break;
            if (!state.handle_event(detail::decode_tui_key(seq)))
                break;
        }
    }

    if (state.saved()) {
        for (const auto& it : state.items()) {
            for (auto& r : registered_options_) {
                if (r.scope == it.scope && r.spec.name == it.spec.name) {
                    r.value = it.value;
                    r.from_preset = false;
                    break;
                }
            }
        }
        if (auto r = save_options_cache(); !r)
            return r;
        log::success("saved options to " + (build_dir() / "options.cache").generic_string());
    }
    return {};
}

} // namespace truss
