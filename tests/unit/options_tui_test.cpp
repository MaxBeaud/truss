// Unit tests for the interactive `truss configure` TUI state machine.
module;
#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

module truss;
import :api;
import :impl;

using namespace truss;

namespace {

bool tui_contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

std::vector<detail::TuiOptionItem> sample_items() {
    return {
        {"app", {OptionSpec::Kind::Bool, "with_tests", "Build tests", "true", {}}, "true"},
        {"audio", {OptionSpec::Kind::Choice, "backend", "Audio driver", "pipewire", {"pipewire", "alsa", "none"}}, "pipewire"},
        {"app", {OptionSpec::Kind::Int, "threads", "Worker threads", "4", {}}, "4"},
        {"audio", {OptionSpec::Kind::String, "tag", "Build tag", "default", {}}, "default"},
    };
}

} // namespace

TEST_CASE("options_tui: key decoding") {
    CHECK(detail::decode_tui_key("\x1b[A").key == detail::TuiKey::Up);
    CHECK(detail::decode_tui_key("\x1b[B").key == detail::TuiKey::Down);
    CHECK(detail::decode_tui_key("\x1b").key == detail::TuiKey::Escape);
    CHECK(detail::decode_tui_key(" ").key == detail::TuiKey::Space);
    CHECK(detail::decode_tui_key("\r").key == detail::TuiKey::Enter);
    CHECK(detail::decode_tui_key("\n").key == detail::TuiKey::Enter);
    CHECK(detail::decode_tui_key("\x7f").key == detail::TuiKey::Backspace);
    auto j = detail::decode_tui_key("j");
    CHECK(j.key == detail::TuiKey::Char);
    CHECK(j.ch == 'j');
}

TEST_CASE("options_tui: scope grouping, navigation, bool toggle, choice cycle") {
    detail::OptionsTuiState state(sample_items());
    // Grouped by first-seen scope: [app] (with_tests, threads), [audio] (backend, tag)
    REQUIRE(state.items().size() == 4);
    CHECK(state.items()[0].scope == "app");
    CHECK(state.items()[0].spec.name == "with_tests");
    CHECK(state.items()[1].scope == "app");
    CHECK(state.items()[1].spec.name == "threads");
    CHECK(state.items()[2].scope == "audio");
    CHECK(state.items()[2].spec.name == "backend");
    CHECK(state.items()[3].scope == "audio");
    CHECK(state.items()[3].spec.name == "tag");

    CHECK(state.selected() == 0);
    // Up at top stays at 0
    CHECK(state.handle_event({detail::TuiKey::Up, 0}));
    CHECK(state.selected() == 0);

    // Toggle bool with Space
    CHECK(state.handle_event({detail::TuiKey::Space, ' '}));
    CHECK(state.items()[0].value == "false");
    const std::string frame = state.render("debug");
    CHECK(tui_contains(frame, "[app]"));
    CHECK(tui_contains(frame, "[audio]"));
    CHECK(tui_contains(frame, "modified"));

    // Move down with j / Down to choice (index 2)
    CHECK(state.handle_event({detail::TuiKey::Char, 'j'}));
    CHECK(state.selected() == 1);
    CHECK(state.handle_event({detail::TuiKey::Down, 0}));
    CHECK(state.selected() == 2);

    // Cycle choice with Space and Enter
    CHECK(state.handle_event({detail::TuiKey::Space, ' '}));
    CHECK(state.items()[2].value == "alsa");
    CHECK(state.handle_event({detail::TuiKey::Enter, 0}));
    CHECK(state.items()[2].value == "none");
    CHECK(state.handle_event({detail::TuiKey::Space, ' '}));
    CHECK(state.items()[2].value == "pipewire");

    // Move back up with k
    CHECK(state.handle_event({detail::TuiKey::Char, 'k'}));
    CHECK(state.selected() == 1);
}

TEST_CASE("options_tui: inline editing for int and string with validation") {
    detail::OptionsTuiState state(sample_items());
    // Move to threads (int, index 1)
    CHECK(state.handle_event({detail::TuiKey::Down, 0}));
    REQUIRE(state.selected() == 1);

    // Enter starts editing
    CHECK(state.handle_event({detail::TuiKey::Enter, 0}));
    CHECK(state.editing());
    CHECK(state.edit_buffer() == "4");

    // Backspace, type invalid "abc", press Enter -> stays editing with error status
    CHECK(state.handle_event({detail::TuiKey::Backspace, 0}));
    CHECK(state.handle_event({detail::TuiKey::Char, 'a'}));
    CHECK(state.handle_event({detail::TuiKey::Char, 'b'}));
    CHECK(state.handle_event({detail::TuiKey::Enter, 0}));
    CHECK(state.editing());
    CHECK(tui_contains(state.status_message(), "Invalid integer"));
    CHECK(state.items()[1].value == "4");

    // Escape cancels edit
    CHECK(state.handle_event({detail::TuiKey::Escape, 0}));
    CHECK_FALSE(state.editing());
    CHECK(state.items()[1].value == "4");

    // Edit again and commit valid integer "16"
    CHECK(state.handle_event({detail::TuiKey::Enter, 0}));
    CHECK(state.handle_event({detail::TuiKey::Backspace, 0}));
    CHECK(state.handle_event({detail::TuiKey::Char, '1'}));
    CHECK(state.handle_event({detail::TuiKey::Char, '6'}));
    CHECK(state.handle_event({detail::TuiKey::Enter, 0}));
    CHECK_FALSE(state.editing());
    CHECK(state.items()[1].value == "16");

    // Move to tag (string, index 3) and edit with space
    CHECK(state.handle_event({detail::TuiKey::Down, 0}));
    CHECK(state.handle_event({detail::TuiKey::Down, 0}));
    REQUIRE(state.selected() == 3);
    CHECK(state.handle_event({detail::TuiKey::Enter, 0}));
    CHECK(state.handle_event({detail::TuiKey::Space, ' '}));
    CHECK(state.handle_event({detail::TuiKey::Char, '2'}));
    CHECK(state.handle_event({detail::TuiKey::Enter, 0}));
    CHECK_FALSE(state.editing());
    CHECK(state.items()[3].value == "default 2");
}

TEST_CASE("options_tui: reset, save, and quit") {
    SUBCASE("reset and save") {
        detail::OptionsTuiState state(sample_items());
        CHECK(state.handle_event({detail::TuiKey::Space, ' '}));
        CHECK(state.items()[0].value == "false");
        CHECK(state.handle_event({detail::TuiKey::Char, 'r'}));
        CHECK(state.items()[0].value == "true");
        CHECK_FALSE(state.handle_event({detail::TuiKey::Char, 's'}));
        CHECK(state.saved());
        CHECK_FALSE(state.quit());
    }

    SUBCASE("quit with q") {
        detail::OptionsTuiState state(sample_items());
        CHECK_FALSE(state.handle_event({detail::TuiKey::Char, 'q'}));
        CHECK(state.quit());
        CHECK_FALSE(state.saved());
    }

    SUBCASE("quit with Esc") {
        detail::OptionsTuiState state(sample_items());
        CHECK_FALSE(state.handle_event({detail::TuiKey::Escape, 0}));
        CHECK(state.quit());
        CHECK_FALSE(state.saved());
    }
}
