#include "tui/input.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace lv::tui;

namespace {

std::vector<Event> parse_all(std::string_view bytes, bool flush = false) {
    InputParser parser;
    parser.feed(bytes);
    if (flush) parser.flush();
    std::vector<Event> out;
    while (auto ev = parser.next()) out.push_back(*ev);
    return out;
}

KeyEvent key_at(const std::vector<Event>& events, std::size_t i) {
    REQUIRE(i < events.size());
    REQUIRE(std::holds_alternative<KeyEvent>(events[i]));
    return std::get<KeyEvent>(events[i]);
}

} // namespace

TEST_CASE("plain characters and utf8") {
    const auto events = parse_all("jé中");
    REQUIRE(events.size() == 3);
    CHECK(key_at(events, 0).is('j'));
    CHECK(key_at(events, 1).is(U'é'));
    CHECK(key_at(events, 2).is(U'中'));
    CHECK(key_at(events, 2).raw == "中");
}

TEST_CASE("control bytes map to keys") {
    const auto events = parse_all("\r\t\x7f\x04\x07\x1c\x08\n");
    REQUIRE(events.size() == 8);
    CHECK(key_at(events, 0).is(Key::Enter));
    CHECK(key_at(events, 1).is(Key::Tab));
    CHECK(key_at(events, 2).is(Key::Backspace));
    CHECK(key_at(events, 3).is('d', ModCtrl));
    CHECK(key_at(events, 4).is('g', ModCtrl));
    CHECK(key_at(events, 5).is('\\', ModCtrl));
    CHECK(key_at(events, 6).is('h', ModCtrl));
    CHECK(key_at(events, 7).is('j', ModCtrl));
}

TEST_CASE("lone escape waits for flush") {
    InputParser parser;
    parser.feed("\x1b");
    CHECK_FALSE(parser.next().has_value());
    CHECK(parser.pending());
    parser.flush();
    const auto ev = parser.next();
    REQUIRE(ev.has_value());
    CHECK(std::get<KeyEvent>(*ev).is(Key::Escape));
    CHECK_FALSE(parser.pending());
}

TEST_CASE("alt chords") {
    const auto events = parse_all("\x1bx\x1b\x04");
    REQUIRE(events.size() == 2);
    CHECK(key_at(events, 0).is('x', ModAlt));
    CHECK(key_at(events, 1).is('d', ModCtrl | ModAlt));
}

TEST_CASE("csi and ss3 keys with modifiers") {
    const auto events =
        parse_all("\x1b[A\x1bOB\x1b[1;5C\x1b[3~\x1b[5;2~\x1b[15~\x1b[Z\x1b[H\x1bOF\x1b[1;3P");
    REQUIRE(events.size() == 10);
    CHECK(key_at(events, 0).is(Key::Up));
    CHECK(key_at(events, 1).is(Key::Down));
    CHECK(key_at(events, 2).is(Key::Right, ModCtrl));
    CHECK(key_at(events, 3).is(Key::Delete));
    CHECK(key_at(events, 4).is(Key::PageUp, ModShift));
    CHECK(key_at(events, 5).is(Key::F5));
    CHECK(key_at(events, 6).is(Key::Tab, ModShift));
    CHECK(key_at(events, 7).is(Key::Home));
    CHECK(key_at(events, 8).is(Key::End));
    CHECK(key_at(events, 9).is(Key::F1, ModAlt));
    CHECK(key_at(events, 2).raw == "\x1b[1;5C");
}

TEST_CASE("csi u keys") {
    const auto events = parse_all("\x1b[106;5u\x1b[13;2u");
    REQUIRE(events.size() == 2);
    CHECK(key_at(events, 0).is('j', ModCtrl));
    CHECK(key_at(events, 1).is(Key::Enter, ModShift));
}

TEST_CASE("sgr mouse") {
    const auto events = parse_all("\x1b[<0;10;5M\x1b[<0;10;5m\x1b[<65;3;4M\x1b[<32;7;8M");
    REQUIRE(events.size() == 4);
    const auto press = std::get<MouseEvent>(events[0]);
    CHECK(press.kind == MouseEvent::Kind::Press);
    CHECK(press.x == 9);
    CHECK(press.y == 4);
    CHECK(std::get<MouseEvent>(events[1]).kind == MouseEvent::Kind::Release);
    CHECK(std::get<MouseEvent>(events[2]).kind == MouseEvent::Kind::WheelDown);
    CHECK(std::get<MouseEvent>(events[3]).kind == MouseEvent::Kind::Drag);
}

TEST_CASE("bracketed paste across reads") {
    InputParser parser;
    parser.feed("\x1b[200~hello\x1b");
    parser.flush(); // a flush must not split an unfinished paste
    CHECK_FALSE(parser.next().has_value());
    parser.feed("[201~x");
    const auto paste = parser.next();
    REQUIRE(paste.has_value());
    CHECK(std::get<PasteEvent>(*paste).text == "hello");
    const auto after = parser.next();
    REQUIRE(after.has_value());
    CHECK(std::get<KeyEvent>(*after).is('x'));
}

TEST_CASE("focus events") {
    const auto events = parse_all("\x1b[I\x1b[O");
    REQUIRE(events.size() == 2);
    CHECK(std::get<FocusEvent>(events[0]).gained);
    CHECK_FALSE(std::get<FocusEvent>(events[1]).gained);
}

TEST_CASE("incomplete utf8 waits") {
    InputParser parser;
    parser.feed("\xe4\xb8");
    CHECK_FALSE(parser.next().has_value());
    parser.feed("\xad");
    const auto ev = parser.next();
    REQUIRE(ev.has_value());
    CHECK(std::get<KeyEvent>(*ev).is(U'中'));
}

TEST_CASE("key specs") {
    const auto ctrl_g = KeySpec::parse("ctrl-g");
    REQUIRE(ctrl_g.has_value());
    KeyEvent ev{.key = Key::Char, .ch = 'g', .mods = ModCtrl, .raw = "\x07"};
    CHECK(ctrl_g->matches(ev));
    CHECK(ctrl_g->to_string() == "C-g");

    const auto backslash = KeySpec::parse("C-\\");
    REQUIRE(backslash.has_value());
    CHECK(backslash->matches(KeyEvent{.key = Key::Char, .ch = '\\', .mods = ModCtrl, .raw = {}}));

    const auto f5 = KeySpec::parse("F5");
    REQUIRE(f5.has_value());
    CHECK(f5->key == Key::F5);

    const auto upper = KeySpec::parse("shift-g");
    REQUIRE(upper.has_value());
    CHECK(upper->matches(KeyEvent{.key = Key::Char, .ch = 'G', .mods = ModNone, .raw = "G"}));

    CHECK_FALSE(KeySpec::parse("ctrl-").has_value());
    CHECK_FALSE(KeySpec::parse("nonsense").has_value());
}

TEST_CASE("key specs read back") {
    for (const auto* text : {"ctrl-g", "t", "T", "?", "f5", "space", "alt-x", "shift-tab",
                             "pagedown", "ctrl--", "ctrl-\\", "é", "shift-1"}) {
        CAPTURE(text);
        const auto spec = KeySpec::parse(text);
        REQUIRE(spec.has_value());
        CHECK(spec->to_spec() == text);
        CHECK(KeySpec::parse(spec->to_spec()) == spec);
    }
    // What a captured key press turns into.
    const KeyEvent ctrl_a{.key = Key::Char, .ch = 'a', .mods = ModCtrl, .raw = "\x01"};
    CHECK(KeySpec::of(ctrl_a).to_spec() == "ctrl-a");
    CHECK(KeySpec::of(KeyEvent{.key = Key::F12, .ch = 0, .mods = ModNone, .raw = {}}).to_spec()
          == "f12");
}
