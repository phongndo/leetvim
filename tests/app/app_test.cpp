#include "app/home.hpp"
#include "app/overlays.hpp"
#include "app/ui.hpp"

#include <doctest/doctest.h>

using namespace lv;
using namespace lv::app;
using leetcode::Difficulty;
using leetcode::ProblemSummary;
using leetcode::Status;
using tui::Key;
using tui::KeyEvent;

namespace {

ProblemSummary problem(Difficulty d, Status s, bool paid, const std::vector<std::string>& tags) {
    ProblemSummary p;
    p.difficulty = d;
    p.status = s;
    p.paid_only = paid;
    for (const auto& t : tags) p.tags.push_back({.name = t, .slug = t});
    return p;
}

KeyEvent ch(char32_t c) { return {.key = Key::Char, .ch = c, .mods = 0, .raw = {}}; }
KeyEvent ctrl(char32_t c) { return {.key = Key::Char, .ch = c, .mods = tui::ModCtrl, .raw = {}}; }

} // namespace

TEST_CASE("an empty filter matches everything") {
    const Filter f;
    CHECK_FALSE(f.active());
    CHECK(f.matches(problem(Difficulty::Hard, Status::Solved, true, {})));
    CHECK(f.describe().empty());
}

TEST_CASE("filters combine difficulty, status, premium and topics") {
    Filter f;
    f.difficulty = {true, false, true}; // easy or hard
    f.status = {true, true, false};     // todo or attempted
    f.hide_paid = true;
    f.tags = {"array", "dp"};
    CHECK(f.active());
    CHECK(f.matches(problem(Difficulty::Easy, Status::Todo, false, {"array", "dp", "math"})));
    CHECK_FALSE(f.matches(problem(Difficulty::Medium, Status::Todo, false, {"array", "dp"})));
    CHECK_FALSE(f.matches(problem(Difficulty::Hard, Status::Solved, false, {"array", "dp"})));
    CHECK_FALSE(f.matches(problem(Difficulty::Hard, Status::Attempted, true, {"array", "dp"})));
    CHECK_FALSE(f.matches(problem(Difficulty::Hard, Status::Attempted, false, {"array"})));
    CHECK(f.describe() == "easy hard · todo attempted · free · #array · #dp");
}

TEST_CASE("line editing keys") {
    LineEdit e;
    for (const char32_t c : U"two sum") e.on_key(ch(c));
    CHECK(e.text() == "two sum");
    e.on_key(ctrl('w'));
    CHECK(e.text() == "two ");
    e.on_key({.key = Key::Backspace, .ch = 0, .mods = 0, .raw = {}});
    CHECK(e.text() == "two");
    e.on_key(ctrl('a'));
    e.on_key(ch('#'));
    CHECK(e.text() == "#two");
    e.on_key(ctrl('k'));
    CHECK(e.text() == "#");
    e.insert("a\nb\x01");
    CHECK(e.text() == "#a b");
    e.on_key(ctrl('u'));
    CHECK(e.text().empty());
    CHECK_FALSE(e.on_key({.key = Key::F5, .ch = 0, .mods = 0, .raw = {}}));
}

TEST_CASE("line editing is grapheme aware") {
    LineEdit e;
    e.insert("中文");
    e.on_key({.key = Key::Backspace, .ch = 0, .mods = 0, .raw = {}});
    CHECK(e.text() == "中");
    e.on_key({.key = Key::Left, .ch = 0, .mods = 0, .raw = {}});
    e.on_key(ch('a'));
    CHECK(e.text() == "a中");
}

TEST_CASE("centered boxes stay inside the area") {
    const auto r = centered({0, 0, 80, 24}, 40, 10);
    CHECK(r == tui::Rect{20, 7, 40, 10});
    const auto clamped = centered({0, 0, 30, 5}, 40, 10);
    CHECK(clamped == tui::Rect{0, 0, 30, 5});
}

TEST_CASE("wrapping prefers word boundaries") {
    using V = std::vector<std::string>;
    CHECK(ui::wrap("stored with 0600 permissions in here", 16)
          == V{"stored with 0600", "permissions in", "here"});
    CHECK(ui::wrap("[1,2,3,4,5,6,7,8]", 6) == V{"[1,2,3", ",4,5,6", ",7,8]"});
    CHECK(ui::wrap("a\n\nb", 10) == V{"a", "", "b"});
    CHECK(ui::wrap("中文中文", 3) == V{"中", "文", "中", "文"});
    CHECK(ui::wrap("", 5).empty());
}

TEST_CASE("hints highlight the key inside the word like btop") {
    const auto& t = tui::theme_named("default");
    const auto text = [](const tui::Segments& s) {
        std::vector<std::string> out;
        for (const auto& seg : s) out.push_back(seg.text);
        return out;
    };
    using V = std::vector<std::string>;
    CHECK(text(ui::hint(t, "f", "filter")) == V{"", "f", "ilter"});
    CHECK(text(ui::hint(t, "o", "sort")) == V{"s", "o", "rt"});
    CHECK(text(ui::hint(t, "H", "hints")) == V{"", "H", "ints"}); // shifted key keeps its case
    CHECK(text(ui::hint(t, "x", "reset")) == V{"x", " reset"});   // not in the word
    CHECK(text(ui::hint(t, "/", "search")) == V{"/", " search"});
    const auto hl = ui::hint(t, "o", "sort");
    CHECK(hl[1].style.fg == t.hi_fg);
    CHECK(hl[0].style.fg == t.main_fg);
}
