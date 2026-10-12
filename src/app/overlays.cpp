#include "app/overlays.hpp"

#include "app/ui.hpp"
#include "util/fuzzy.hpp"
#include "util/unicode.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace lv::app {

using tui::Key;
using tui::KeyEvent;
using tui::ModCtrl;
using tui::Rect;

namespace {

/// Byte offset of the previous grapheme boundary before `pos`.
std::size_t prev_boundary(const std::string& s, std::size_t pos) {
    std::size_t last = 0;
    std::size_t i = 0;
    while (i < pos) {
        last = i;
        unicode::next_grapheme(s, i);
    }
    return last;
}

} // namespace

Rect overlay_frame(const App& app, const tui::Painter& p, Rect r, std::string_view title,
                   std::vector<tui::Segments> hints) {
    const auto& t = app.theme();
    p.clear(r, app.base_bg());
    return ui::box(p, t,
                   {.rect = r,
                    .color = t.hi_fg,
                    .focused = true,
                    .rounded = true,
                    .titles = {ui::title(t, title)},
                    .right_titles = {},
                    .hints = std::move(hints)});
}

Rect centered(Rect area, int w, int h) {
    w = std::min(w, area.w);
    h = std::min(h, area.h);
    return {area.x + ((area.w - w) / 2), area.y + ((area.h - h) / 2), w, h};
}

// ---------------------------------------------------------------- LineEdit

bool LineEdit::on_key(const KeyEvent& ev) {
    if (ev.key == Key::Char && (ev.mods & ~tui::ModShift) == 0) {
        insert(unicode::to_utf8(ev.ch));
        return true;
    }
    if (ev.is(Key::Backspace) || ev.is('h', ModCtrl)) {
        if (cursor_ > 0) {
            const auto prev = prev_boundary(text_, cursor_);
            text_.erase(prev, cursor_ - prev);
            cursor_ = prev;
        }
        return true;
    }
    if (ev.is(Key::Delete)) {
        if (cursor_ < text_.size()) {
            std::size_t next = cursor_;
            unicode::next_grapheme(text_, next);
            text_.erase(cursor_, next - cursor_);
        }
        return true;
    }
    if (ev.is(Key::Left) || ev.is('b', ModCtrl)) {
        if (cursor_ > 0) cursor_ = prev_boundary(text_, cursor_);
        return true;
    }
    if (ev.is(Key::Right) || ev.is('f', ModCtrl)) {
        if (cursor_ < text_.size()) unicode::next_grapheme(text_, cursor_);
        return true;
    }
    if (ev.is(Key::Home) || ev.is('a', ModCtrl)) {
        cursor_ = 0;
        return true;
    }
    if (ev.is(Key::End) || ev.is('e', ModCtrl)) {
        cursor_ = text_.size();
        return true;
    }
    if (ev.is('u', ModCtrl)) {
        text_.erase(0, cursor_);
        cursor_ = 0;
        return true;
    }
    if (ev.is('k', ModCtrl)) {
        text_.erase(cursor_);
        return true;
    }
    if (ev.is('w', ModCtrl)) {
        std::size_t start = cursor_;
        while (start > 0 && text_[start - 1] == ' ') --start;
        while (start > 0 && text_[start - 1] != ' ') --start;
        text_.erase(start, cursor_ - start);
        cursor_ = start;
        return true;
    }
    return false;
}

void LineEdit::insert(std::string_view text) {
    std::string clean;
    for (const char c : text) {
        if (c == '\n' || c == '\r' || c == '\t') {
            clean += ' ';
        } else if (static_cast<unsigned char>(c) >= 0x20) {
            clean += c;
        }
    }
    text_.insert(cursor_, clean);
    cursor_ += clean.size();
}

void LineEdit::set(std::string text) {
    text_ = std::move(text);
    cursor_ = text_.size();
}

int LineEdit::draw(const tui::Painter& p, int x, int y, int width, const tui::Style& style,
                   bool masked) const {
    std::string shown = text_;
    std::size_t cursor = cursor_;
    if (masked) {
        const auto n = static_cast<std::size_t>(unicode::display_width(text_));
        const auto before = static_cast<std::size_t>(
            unicode::display_width(std::string_view(text_).substr(0, cursor_)));
        shown.clear();
        for (std::size_t i = 0; i < n; ++i) shown += "•";
        cursor = before * std::string_view("•").size();
    }
    // Scroll horizontally so the cursor stays visible.
    const int cursor_col = unicode::display_width(std::string_view(shown).substr(0, cursor));
    const int offset = std::max(0, cursor_col - width + 1);
    std::size_t pos = 0;
    int skipped = 0;
    while (pos < shown.size() && skipped < offset)
        skipped += unicode::next_grapheme(shown, pos).width;
    p.clipped({x, y, width, 1}).text(x, y, std::string_view(shown).substr(pos), style);
    return x + cursor_col - skipped;
}

// ---------------------------------------------------------------- PromptOverlay

PromptOverlay::PromptOverlay(App& app, std::string title, std::vector<std::string> message,
                             Submit on_submit, bool masked, std::string initial)
    : app_(app), title_(std::move(title)), message_(std::move(message)),
      on_submit_(std::move(on_submit)), masked_(masked) {
    edit_.set(std::move(initial));
}

void PromptOverlay::draw(const tui::Painter& p, Rect area) {
    const auto& t = app_.theme();
    int lines = 0;
    const int w = std::min(area.w - 4, 76);
    for (const auto& m : message_) lines += static_cast<int>(ui::wrap(m, w - 4).size());
    const Rect r = centered(area, w, lines + 5);
    const Rect inner =
        overlay_frame(app_, p, r, title_, {ui::hint(t, "⏎", "ok"), ui::hint(t, "esc", "cancel")});
    int y = inner.y;
    for (const auto& m : message_) {
        y += ui::draw_wrapped(p, {inner.x + 1, y, inner.w - 2, inner.bottom() - y}, m,
                              ui::fg(t.main_fg));
    }
    y = inner.bottom() - 2;
    p.text(inner.x + 1, y + 1, "❯", ui::fg(t.hi_fg, tui::AttrBold));
    const int cx = edit_.draw(p, inner.x + 3, y + 1, inner.w - 4, ui::fg(t.title), masked_);
    cursor_ = tui::Cursor{.x = cx, .y = y + 1, .shape = tui::CursorShape::Bar};
}

bool PromptOverlay::on_key(const KeyEvent& ev) {
    if (ev.is(Key::Escape) || ev.is('c', ModCtrl)) {
        closed_ = true;
    } else if (ev.is(Key::Enter)) {
        closed_ = true;
        on_submit_(edit_.text());
    } else {
        edit_.on_key(ev);
    }
    return true;
}

bool PromptOverlay::on_paste(const std::string& text) {
    edit_.insert(text);
    return true;
}

// ---------------------------------------------------------------- CommandOverlay

void CommandOverlay::draw(const tui::Painter& p, Rect area) {
    const auto& t = app_.theme();
    const int y = area.bottom() - 1;
    p.clear({area.x, y, area.w, 1}, app_.base_bg());
    p.text(area.x, y, ":", ui::fg(t.hi_fg, tui::AttrBold));
    const int cx = edit_.draw(p, area.x + 1, y, area.w - 1, ui::fg(t.title));
    cursor_ = tui::Cursor{.x = cx, .y = y, .shape = tui::CursorShape::Bar};
}

bool CommandOverlay::on_key(const KeyEvent& ev) {
    if (ev.is(Key::Escape) || ev.is('c', ModCtrl)
        || (ev.is(Key::Backspace) && edit_.text().empty())) {
        closed_ = true;
    } else if (ev.is(Key::Enter)) {
        closed_ = true;
        app_.run_command(edit_.text());
    } else {
        edit_.on_key(ev);
    }
    return true;
}

bool CommandOverlay::on_paste(const std::string& text) {
    edit_.insert(text);
    return true;
}

// ---------------------------------------------------------------- ConfirmOverlay

ConfirmOverlay::ConfirmOverlay(App& app, std::string title, std::vector<std::string> message,
                               std::vector<Choice> choices)
    : app_(app), title_(std::move(title)), message_(std::move(message)),
      choices_(std::move(choices)) {}

void ConfirmOverlay::draw(const tui::Painter& p, Rect area) {
    const auto& t = app_.theme();
    const int w = std::min(area.w - 4, 64);
    int lines = 0;
    for (const auto& m : message_) lines += static_cast<int>(ui::wrap(m, w - 4).size());
    const Rect r = centered(area, w, lines + static_cast<int>(choices_.size()) + 4);
    const Rect inner = overlay_frame(app_, p, r, title_);
    int y = inner.y;
    for (const auto& m : message_) {
        y += ui::draw_wrapped(p, {inner.x + 1, y, inner.w - 2, inner.h}, m, ui::fg(t.main_fg));
    }
    ++y;
    for (const auto& c : choices_) {
        const int x = p.text(inner.x + 1, y, std::format("[{}]", unicode::to_utf8(c.key)),
                             ui::fg(t.hi_fg, tui::AttrBold));
        p.text(x + 1, y, c.label, ui::fg(t.main_fg));
        ++y;
    }
}

bool ConfirmOverlay::on_key(const KeyEvent& ev) {
    if (ev.is(Key::Escape) || ev.is('c', ModCtrl)) {
        closed_ = true;
        return true;
    }
    for (const auto& c : choices_) {
        if (ev.key == Key::Char && ev.ch == c.key) {
            closed_ = true;
            c.action();
            return true;
        }
    }
    return true;
}

// ---------------------------------------------------------------- PickerOverlay

PickerOverlay::PickerOverlay(App& app, std::string title, std::vector<Item> items,
                             Callbacks callbacks, bool multi, std::set<std::string> checked,
                             const std::string& initial)
    : app_(app), title_(std::move(title)), items_(std::move(items)),
      callbacks_(std::move(callbacks)), multi_(multi), checked_(std::move(checked)) {
    refilter();
    for (std::size_t i = 0; i < visible_.size(); ++i) {
        if (items_[visible_[i]].value == initial) selected_ = static_cast<int>(i);
    }
}

void PickerOverlay::refilter() {
    std::vector<std::pair<int, std::size_t>> scored;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (const auto score = fuzzy::match(filter_.text(), items_[i].label)) {
            scored.emplace_back(-*score, i);
        }
    }
    if (!filter_.text().empty())
        std::ranges::stable_sort(scored, {}, &std::pair<int, std::size_t>::first);
    visible_.clear();
    for (const auto& [score, i] : scored) visible_.push_back(i);
    selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(visible_.size()) - 1));
}

void PickerOverlay::highlight() {
    if (callbacks_.on_highlight && !visible_.empty()) {
        callbacks_.on_highlight(items_[visible_[static_cast<std::size_t>(selected_)]].value);
    }
}

void PickerOverlay::draw(const tui::Painter& p, Rect area) {
    const auto& t = app_.theme();
    int widest = unicode::display_width(title_) + 10;
    for (const auto& item : items_) {
        widest = std::max(widest, unicode::display_width(item.label)
                                      + unicode::display_width(item.detail) + 10);
    }
    const int w = std::clamp(widest, 30, std::min(area.w - 4, 70));
    const int h = std::min(area.h - 2, static_cast<int>(items_.size()) + 4);
    const Rect r = centered(area, w, std::max(h, 6));
    std::vector<tui::Segments> hints{ui::hint(t, "⏎", multi_ ? "apply" : "select"),
                                     ui::hint(t, "esc", "cancel")};
    if (multi_) hints.insert(hints.begin(), ui::hint(t, "␣", "toggle"));
    const Rect inner = overlay_frame(app_, p, r, title_, std::move(hints));

    p.text(inner.x + 1, inner.y, "/", ui::fg(t.hi_fg, tui::AttrBold));
    const int cx = filter_.draw(p, inner.x + 3, inner.y, inner.w - 4, ui::fg(t.title));
    cursor_ = tui::Cursor{.x = cx, .y = inner.y, .shape = tui::CursorShape::Bar};

    const int rows = inner.h - 1;
    scroll_ = std::min(scroll_, selected_);
    if (selected_ >= scroll_ + rows) scroll_ = selected_ - rows + 1;
    for (int row = 0; row < rows; ++row) {
        const auto vi = static_cast<std::size_t>(scroll_) + static_cast<std::size_t>(row);
        if (vi >= visible_.size()) break;
        const auto& item = items_[visible_[vi]];
        const int y = inner.y + 1 + row;
        const bool sel = scroll_ + row == selected_;
        const tui::Style base =
            sel ? tui::Style{.fg = t.selected_fg, .bg = t.selected_bg, .attrs = tui::AttrBold}
                : ui::fg(item.color.is_default() ? t.main_fg : item.color);
        if (sel) p.fill({inner.x, y, inner.w, 1}, base);
        int x = inner.x + 1;
        if (multi_) {
            x = p.text(x, y, checked_.contains(item.value) ? "[x] " : "[ ] ",
                       sel ? base : ui::fg(t.hi_fg));
        }
        const int detail_w = unicode::display_width(item.detail);
        p.text(x, y, item.label, base, inner.right() - x - detail_w - 2);
        if (!item.detail.empty()) {
            p.text(inner.right() - detail_w - 1, y, item.detail, sel ? base : ui::fg(t.graph_text));
        }
    }
    if (visible_.empty()) p.text(inner.x + 1, inner.y + 1, "no matches", ui::fg(t.inactive_fg));
}

bool PickerOverlay::on_key(const KeyEvent& ev) {
    const int count = static_cast<int>(visible_.size());
    const auto move = [&](int delta) {
        if (count == 0) return;
        selected_ = std::clamp(selected_ + delta, 0, count - 1);
        highlight();
    };
    if (ev.is(Key::Escape) || ev.is('c', ModCtrl)) {
        closed_ = true;
        if (callbacks_.on_cancel) callbacks_.on_cancel();
    } else if (ev.is(Key::Enter)) {
        closed_ = true;
        if (multi_) {
            if (callbacks_.on_done) callbacks_.on_done(checked_);
        } else if (count > 0 && callbacks_.on_pick) {
            callbacks_.on_pick(items_[visible_[static_cast<std::size_t>(selected_)]].value);
        } else if (callbacks_.on_cancel) {
            callbacks_.on_cancel();
        }
    } else if (ev.is(Key::Down) || ev.is('n', ModCtrl) || ev.is('j', ModCtrl) || ev.is(Key::Tab)) {
        move(1);
    } else if (ev.is(Key::Up) || ev.is('p', ModCtrl) || ev.is('k', ModCtrl)
               || ev.is(Key::Tab, tui::ModShift)) {
        move(-1);
    } else if (ev.is(Key::PageDown) || ev.is('d', ModCtrl)) {
        move(10);
    } else if (ev.is(Key::PageUp) || ev.is('u', ModCtrl)) {
        move(-10);
    } else if (multi_ && ev.is(' ')) {
        if (count > 0) {
            const auto& v = items_[visible_[static_cast<std::size_t>(selected_)]].value;
            if (!checked_.erase(v)) checked_.insert(v);
            move(1);
        }
    } else if (filter_.on_key(ev)) {
        refilter();
        highlight();
    }
    return true;
}

bool PickerOverlay::on_paste(const std::string& text) {
    filter_.insert(text);
    refilter();
    return true;
}

// ---------------------------------------------------------------- HelpOverlay

namespace {

struct HelpSection {
    std::string_view title;
    std::vector<std::pair<std::string, std::string_view>> keys;
};

std::vector<HelpSection> help_sections(const Bindings& keys) {
    const std::string prefix = keys.prefix.to_string();
    const auto after = [&](const tui::KeySpec& key) { return prefix + " " + key.to_string(); };
    return {
        {"Problems",
         {{"j k ↓ ↑", "move"},
          {"gg G", "first / last"},
          {"C-d C-u", "half page down / up"},
          {"Enter l", "solve the problem"},
          {"/", "search (fuzzy, by title or id)"},
          {"f", "filter: difficulty, status, premium"},
          {"t", "filter by topics"},
          {"o O", "cycle sort / reverse"},
          {"d", "open today's daily question"},
          {"r", "jump to a random problem"},
          {"J K", "scroll the preview"},
          {"1 2", "toggle stats / preview boxes"},
          {"s", "stats dashboard"},
          {"R", "re-sync problems and stats"},
          {"y gx", "copy link / open in browser"}}},
        {"Solving",
         {{prefix + " h j k l", "focus description / results / editor"},
          {after(keys.run), "run the testcases on LeetCode"},
          {after(keys.submit), "submit"},
          {after(keys.results), "toggle the results panel"},
          {after(keys.testcases), "show testcases"},
          {after(keys.zoom), "zoom the editor"},
          {after(keys.back), "back to the problem list"},
          {prefix + " " + prefix, "send " + prefix + " to the editor"},
          {"(description) j k C-d C-u gg G", "scroll"},
          {"(description) H", "show hints"},
          {"(description) < >", "resize the panes"},
          {"(description) L", "change solution language"},
          {"(results) h l 1-9", "pick a testcase"},
          {"(results) Tab", "results / testcases / submissions"},
          {"(results) e x", "edit / reset custom testcases"}}},
        {"Anywhere",
         {{":", "command line (:help commands below)"},
          {"?", "this help"},
          {"L T", "language / theme picker"},
          {",", "settings"},
          {"q", "back / quit"},
          {"C-c", "quit"},
          {"C-z", "suspend"}}},
        {"Commands",
         {{":daily  :random  :stats", ""},
          {":open <id|slug|title>", "open a problem"},
          {":lang <slug>  :theme <name>", "switch language / theme"},
          {":set <option>=<value>", "change and save an option"},
          {":set  :settings", "all the settings"},
          {":login", "link your account from your browser"},
          {":login paste  :logout", "paste a cookie / forget the session"},
          {":sync", "re-download the problem list"},
          {":q", "quit"}}},
    };
}

} // namespace

void HelpOverlay::draw(const tui::Painter& p, Rect area) {
    const auto& t = app_.theme();
    const auto sections = help_sections(app_.keys());
    int total = 0;
    for (const auto& s : sections) total += static_cast<int>(s.keys.size()) + 2;
    const Rect r = centered(area, std::min(area.w - 2, 84), std::min(area.h - 2, total + 2));
    const Rect inner = overlay_frame(app_, p, r, "help",
                                     {ui::hint(t, "j/k", "scroll"), ui::hint(t, "q", "close")});
    max_scroll_ = std::max(0, total - inner.h);
    scroll_ = std::min(scroll_, max_scroll_);
    const tui::Painter clip = p.clipped(inner);
    int y = inner.y - scroll_;
    const int key_w = 34;
    for (const auto& s : sections) {
        clip.text(inner.x + 1, y++, s.title, ui::fg(t.title, tui::AttrBold));
        for (const auto& [key, what] : s.keys) {
            clip.text(inner.x + 3, y, key, ui::fg(t.hi_fg), key_w);
            clip.text(inner.x + 3 + key_w, y, what, ui::fg(t.main_fg));
            ++y;
        }
        ++y;
    }
}

bool HelpOverlay::on_key(const KeyEvent& ev) {
    if (ev.is('j') || ev.is(Key::Down)) {
        scroll_ = std::min(scroll_ + 1, max_scroll_);
    } else if (ev.is('k') || ev.is(Key::Up)) {
        scroll_ = std::max(scroll_ - 1, 0);
    } else if (ev.is('d', ModCtrl) || ev.is(Key::PageDown)) {
        scroll_ = std::min(scroll_ + 10, max_scroll_);
    } else if (ev.is('u', ModCtrl) || ev.is(Key::PageUp)) {
        scroll_ = std::max(scroll_ - 10, 0);
    } else if (ev.is('q') || ev.is('?') || ev.is(Key::Escape) || ev.is(Key::Enter)) {
        closed_ = true;
    }
    return true;
}

} // namespace lv::app
