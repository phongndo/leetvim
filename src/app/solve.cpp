#include "app/solve.hpp"

#include "app/overlays.hpp"
#include "app/ui.hpp"
#include "core/workspace.hpp"
#include "util/strings.hpp"
#include "util/unicode.hpp"

#include <algorithm>
#include <format>

namespace lv::app {

using tui::Key;
using tui::KeyEvent;
using tui::ModCtrl;
using tui::Rect;
using tui::Segments;

namespace {

constexpr std::array<std::string_view, 3> tab_names{"result", "testcases", "submissions"};

std::string rule(int width) {
    std::string out;
    for (int i = 0; i < width; ++i) out += "─";
    return out;
}

/// "name = value" lines for one testcase.
std::vector<std::pair<std::string, std::string>> label_case(const leetcode::Question& q,
                                                            std::string_view input) {
    std::vector<std::pair<std::string, std::string>> out;
    const auto lines = str::lines(input);
    const auto& names = q.meta.param_names;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string name;
        if (!q.meta.class_name.empty()) {
            name = i == 0 ? "calls" : "args";
        } else if (i < names.size()) {
            name = names[i];
        }
        out.emplace_back(std::move(name), std::string(lines[i]));
    }
    return out;
}

/// Accumulates styled lines for scrollable panel content.
struct Lines {
    std::vector<Segments> rows;
    int width = 40;

    void blank() { rows.emplace_back(); }
    void add(Segments s) { rows.push_back(std::move(s)); }
    void text(std::string_view s, tui::Style style, int indent = 0) {
        for (auto& line : ui::wrap(s, width - indent)) {
            rows.push_back({{std::string(static_cast<std::size_t>(indent), ' '), style},
                            {std::move(line), style}});
        }
    }
    /// "Label     value" with the value wrapped under itself.
    void field(std::string_view label, std::string_view value, tui::Style label_style,
               tui::Style value_style, int label_w = 10) {
        const auto wrapped = ui::wrap(value.empty() ? "\"\"" : value, width - label_w);
        for (std::size_t i = 0; i < wrapped.size(); ++i) {
            rows.push_back({{unicode::pad_right(i == 0 ? label : "", label_w), label_style},
                            {wrapped[i], value_style}});
        }
    }
};

void draw_lines(const tui::Painter& p, Rect r, const std::vector<Segments>& rows, int scroll) {
    for (int i = 0; i < r.h; ++i) {
        const auto index = static_cast<std::size_t>(scroll) + static_cast<std::size_t>(i);
        if (index >= rows.size()) break;
        tui::draw_segments(p.clipped(r), r.x, r.y + i, rows[index]);
    }
}

std::string_view verdict_icon(const leetcode::RunResult& r) {
    if (r.is_submission) return r.accepted() ? "✓" : "✗";
    return r.accepted() && r.correct_answer ? "✓" : "✗";
}

} // namespace

Session* SolveView::session() const { return app_.active_session(); }

SolveView::Layout SolveView::layout(Rect area, const Session& s) const {
    Layout out;
    if (s.zoomed) {
        out.editor = area;
        return out;
    }
    const int left_w =
        std::clamp(static_cast<int>(area.w * app_.config().split), 30, std::max(30, area.w - 40));
    const Rect left{area.x, area.y, left_w, area.h};
    out.editor = {area.x + left_w, area.y, area.w - left_w, area.h};
    if (s.panel_open) {
        const double share = s.test_editor ? 0.6 : 0.45;
        const int panel_h =
            std::clamp(static_cast<int>(area.h * share), 8, std::max(8, area.h - 8));
        out.description = {left.x, left.y, left.w, left.h - panel_h};
        out.panel = {left.x, left.bottom() - panel_h, left.w, panel_h};
    } else {
        out.description = left;
    }
    return out;
}

void SolveView::set_focus(Focus f) {
    Session* s = session();
    if (s == nullptr) return;
    if (focus_ != f && s->editor) s->editor->set_focus(f == Focus::Editor);
    focus_ = f;
    if (f == Focus::Panel || f == Focus::TestEditor) s->panel_open = true;
    if (f != Focus::Editor && s->zoomed) s->zoomed = false;
    app_.redraw();
}

// ---------------------------------------------------------------- input

bool SolveView::on_key(const KeyEvent& ev) {
    Session* s = session();
    if (s == nullptr) {
        app_.go_home();
        return true;
    }
    if (focus_ == Focus::TestEditor && !s->test_editor) focus_ = Focus::Panel;
    if (prefix_pending_) {
        prefix_pending_ = false;
        handle_prefix(ev, *s);
        app_.redraw();
        return true;
    }
    if (app_.prefix().matches(ev)) {
        prefix_pending_ = true;
        app_.redraw();
        return true;
    }
    switch (focus_) {
    case Focus::Editor:
        if (s->editor && s->editor->running()) {
            s->editor->send_key(ev);
        } else if (ev.is(Key::Enter)) {
            app_.respawn_editor(*s);
        } else if (ev.is('q') || ev.is(Key::Escape)) {
            app_.go_home();
        } else if (ev.is('h')) {
            set_focus(Focus::Description);
        }
        return true;
    case Focus::TestEditor: s->test_editor->send_key(ev); return true;
    case Focus::Description: return handle_description(ev, *s);
    case Focus::Panel: return handle_panel(ev, *s);
    }
    return false;
}

void SolveView::handle_prefix(const KeyEvent& ev, Session& s) {
    if (app_.prefix().matches(ev)) {
        // Prefix twice sends it through, like tmux.
        if (focus_ == Focus::TestEditor && s.test_editor) {
            s.test_editor->send_key(ev);
        } else if (s.editor) {
            s.editor->send_key(ev);
        }
        return;
    }
    if (ev.is(Key::Escape)) return;
    if (ev.is('h') || ev.is(Key::Left) || ev.is('k') || ev.is(Key::Up)) {
        set_focus(Focus::Description);
    } else if (ev.is('l') || ev.is(Key::Right)) {
        set_focus(Focus::Editor);
    } else if (ev.is('j') || ev.is(Key::Down)) {
        set_focus(s.test_editor ? Focus::TestEditor : Focus::Panel);
    } else if (!handle_common(ev, s)) {
        app_.toast(std::format("{} {}: not a leetvim key ({} ? for help)",
                               app_.prefix().to_string(), tui::describe(ev),
                               app_.prefix().to_string()),
                   Level::Warning);
    }
}

bool SolveView::handle_common(const KeyEvent& ev, Session& s) {
    if (ev.is('t')) {
        app_.start_job(s, false);
    } else if (ev.is('s')) {
        app_.start_job(s, true);
    } else if (ev.is('r')) {
        s.panel_open = !s.panel_open;
        if (!s.panel_open && (focus_ == Focus::Panel || focus_ == Focus::TestEditor)) {
            set_focus(Focus::Editor);
        }
    } else if (ev.is('c')) {
        s.panel_tab = 1;
        set_focus(Focus::Panel);
    } else if (ev.is('z')) {
        s.zoomed = !s.zoomed;
        if (s.zoomed) set_focus(Focus::Editor);
    } else if (ev.is('q')) {
        app_.go_home();
    } else if (ev.is('?')) {
        app_.push_overlay(std::make_unique<HelpOverlay>(app_));
    } else if (ev.is(':')) {
        app_.push_overlay(std::make_unique<CommandOverlay>(app_));
    } else if (ev.is('H')) {
        s.show_hints = !s.show_hints;
        if (s.show_hints && s.question && s.question->hints.empty()) {
            app_.toast("this problem has no hints");
        }
    } else if (ev.is('L')) {
        std::vector<PickerOverlay::Item> items;
        for (const auto& lang : core::languages()) {
            if (s.question && !s.question->snippets.empty()
                && s.question->snippet(lang.slug) == nullptr) {
                continue;
            }
            items.push_back({.label = std::string(lang.name),
                             .detail = std::string(lang.slug),
                             .value = std::string(lang.slug),
                             .color = {}});
        }
        const std::string slug = s.slug;
        app_.push_overlay(std::make_unique<PickerOverlay>(
            app_, "solution language", std::move(items),
            PickerOverlay::Callbacks{.on_pick =
                                         [this, slug = slug](const std::string& lang) {
                                             if (Session* ss = app_.session(slug)) {
                                                 app_.switch_language(*ss, lang);
                                             }
                                         },
                                     .on_done = {},
                                     .on_highlight = {},
                                     .on_cancel = {}},
            false, std::set<std::string>{}, s.lang != nullptr ? std::string(s.lang->slug) : ""));
    } else if (ev.is('y')) {
        app_.yank(app_.client().problem_url(s.slug), "link");
    } else if (ev.is('o')) {
        app_.open_url(app_.client().problem_url(s.slug));
    } else {
        return false;
    }
    return true;
}

void SolveView::scroll_description(Session& s, int delta) const {
    s.desc_scroll = std::clamp(s.desc_scroll + delta, 0, std::max(0, desc_lines_ - desc_height_));
}

bool SolveView::handle_description(const KeyEvent& ev, Session& s) {
    const int page = std::max(1, desc_height_ - 1);
    if (pending_g_) {
        pending_g_ = false;
        if (ev.is('g')) {
            s.desc_scroll = 0;
            return true;
        }
        if (ev.is('x')) {
            app_.open_url(app_.client().problem_url(s.slug));
            return true;
        }
    }
    if (ev.is('j') || ev.is(Key::Down))
        scroll_description(s, 1);
    else if (ev.is('k') || ev.is(Key::Up))
        scroll_description(s, -1);
    else if (ev.is('d', ModCtrl))
        scroll_description(s, page / 2);
    else if (ev.is('u', ModCtrl))
        scroll_description(s, -page / 2);
    else if (ev.is('f', ModCtrl) || ev.is(Key::PageDown) || ev.is(' '))
        scroll_description(s, page);
    else if (ev.is('b', ModCtrl) || ev.is(Key::PageUp))
        scroll_description(s, -page);
    else if (ev.is('G'))
        scroll_description(s, 1'000'000);
    else if (ev.is('g'))
        pending_g_ = true;
    else if (ev.is('l') || ev.is('i') || ev.is(Key::Tab) || ev.is(Key::Enter)) {
        set_focus(Focus::Editor);
    } else if (ev.is('<')) {
        app_.set_option("split", std::format("{:.2f}", std::max(0.15, app_.config().split - 0.05)));
    } else if (ev.is('>')) {
        app_.set_option("split", std::format("{:.2f}", std::min(0.85, app_.config().split + 0.05)));
    } else if (ev.is('R')) {
        app_.data().questions.erase(s.slug);
        app_.data().question_errors.erase(s.slug);
        app_.question(s.slug);
    } else if (ev.is('q') || ev.is(Key::Escape)) {
        if (ev.is('q'))
            app_.go_home();
        else
            set_focus(Focus::Editor);
    } else if (!handle_common(ev, s)) {
        return false;
    }
    return true;
}

void SolveView::select_case(Session& s, int index) const {
    if (case_count_ <= 0) return;
    s.selected_case = std::clamp(index, 0, case_count_ - 1);
    s.panel_scroll = 0;
}

bool SolveView::handle_panel(const KeyEvent& ev, Session& s) {
    const int page = std::max(1, panel_height_ - 1);
    const auto scroll = [&](int delta) {
        s.panel_scroll =
            std::clamp(s.panel_scroll + delta, 0, std::max(0, panel_lines_ - panel_height_));
    };
    if (ev.is('j') || ev.is(Key::Down))
        scroll(1);
    else if (ev.is('k') || ev.is(Key::Up))
        scroll(-1);
    else if (ev.is('d', ModCtrl))
        scroll(page / 2);
    else if (ev.is('u', ModCtrl))
        scroll(-page / 2);
    else if (ev.is('G'))
        scroll(1'000'000);
    else if (ev.is('g'))
        s.panel_scroll = 0;
    else if (ev.is('h') || ev.is(Key::Left))
        select_case(s, s.selected_case - 1);
    else if (ev.is('l') || ev.is(Key::Right))
        select_case(s, s.selected_case + 1);
    else if (ev.key == Key::Char && ev.ch >= U'1' && ev.ch <= U'9' && ev.mods == 0) {
        select_case(s, static_cast<int>(ev.ch - U'1'));
    } else if (ev.is(Key::Tab) || ev.is(']')) {
        s.panel_tab = (s.panel_tab + 1) % 3;
        s.panel_scroll = 0;
        if (s.panel_tab == 2) app_.load_submissions(s);
    } else if (ev.is(Key::Tab, tui::ModShift) || ev.is('[')) {
        s.panel_tab = (s.panel_tab + 2) % 3;
        s.panel_scroll = 0;
        if (s.panel_tab == 2) app_.load_submissions(s);
    } else if (ev.is('e')) {
        app_.edit_testcases(s);
        if (s.test_editor) set_focus(Focus::TestEditor);
    } else if (ev.is('x')) {
        app_.reset_testcases(s);
    } else if (ev.is('R') && s.panel_tab == 2) {
        app_.load_submissions(s, true);
    } else if (ev.is(Key::Escape) || ev.is('q')) {
        set_focus(Focus::Editor);
    } else if (!handle_common(ev, s)) {
        return false;
    }
    return true;
}

bool SolveView::on_paste(const std::string& text) {
    Session* s = session();
    if (s == nullptr) return false;
    if (focus_ == Focus::Editor && s->editor) s->editor->send_paste(text);
    if (focus_ == Focus::TestEditor && s->test_editor) s->test_editor->send_paste(text);
    return true;
}

bool SolveView::on_mouse(const tui::MouseEvent& ev) {
    using Kind = tui::MouseEvent::Kind;
    Session* s = session();
    if (s == nullptr) return false;
    const bool wheel = ev.kind == Kind::WheelUp || ev.kind == Kind::WheelDown;
    const int delta = ev.kind == Kind::WheelDown ? 3 : -3;

    if (editor_inner_.contains(ev.x, ev.y) && s->editor) {
        if (ev.kind == Kind::Press && focus_ != Focus::Editor) set_focus(Focus::Editor);
        s->editor->send_mouse(ev, ev.x - editor_inner_.x, ev.y - editor_inner_.y);
        return true;
    }
    if (panel_inner_.contains(ev.x, ev.y) && s->panel_open) {
        if (s->test_editor) {
            if (ev.kind == Kind::Press) set_focus(Focus::TestEditor);
            s->test_editor->send_mouse(ev, ev.x - panel_inner_.x, ev.y - panel_inner_.y);
        } else if (wheel) {
            s->panel_scroll =
                std::clamp(s->panel_scroll + delta, 0, std::max(0, panel_lines_ - panel_height_));
        } else if (ev.kind == Kind::Press) {
            set_focus(Focus::Panel);
        }
        return true;
    }
    if (last_.description.contains(ev.x, ev.y)) {
        if (wheel) {
            scroll_description(*s, delta);
        } else if (ev.kind == Kind::Press) {
            set_focus(Focus::Description);
        }
        return true;
    }
    if (last_.panel.contains(ev.x, ev.y) && ev.kind == Kind::Press && ev.y == last_.panel.y) {
        // Click on a tab label in the border.
        int x = last_.panel.x + 1;
        for (int i = 0; i < 3; ++i) {
            const int w = unicode::display_width(tab_names.at(static_cast<std::size_t>(i))) + 2;
            if (ev.x >= x && ev.x < x + w) {
                s->panel_tab = i;
                if (i == 2) app_.load_submissions(*s);
            }
            x += w;
        }
        set_focus(Focus::Panel);
        return true;
    }
    return false;
}

std::optional<tui::Cursor> SolveView::cursor() const {
    Session* s = session();
    if (s == nullptr) return std::nullopt;
    if (focus_ == Focus::Editor && s->editor && s->editor->running()) {
        return s->editor->cursor(editor_inner_);
    }
    if (focus_ == Focus::TestEditor && s->test_editor && s->test_editor->running()) {
        return s->test_editor->cursor(panel_inner_);
    }
    return std::nullopt;
}

// ---------------------------------------------------------------- drawing

void SolveView::draw(const tui::Painter& p, Rect area) {
    Session* s = session();
    if (s == nullptr) return;
    if (focus_ == Focus::TestEditor && !s->test_editor) focus_ = Focus::Panel;
    if ((focus_ == Focus::Panel || focus_ == Focus::TestEditor) && !s->panel_open) {
        focus_ = Focus::Editor;
    }
    last_ = layout(area, *s);
    if (!last_.description.empty()) draw_description(p, last_.description, *s);
    if (!last_.panel.empty())
        draw_panel(p, last_.panel, *s);
    else
        panel_inner_ = {};
    draw_editor(p, last_.editor, *s);
}

void SolveView::draw_description(const tui::Painter& p, Rect r, Session& s) {
    const auto& t = app_.theme();
    const auto* q = s.question.get();
    std::vector<Segments> right;
    if (q != nullptr) {
        right.push_back(
            ui::label(std::format("{}. {}", q->frontend_id, q->title), t.title, tui::AttrBold));
    }
    const Rect in = ui::box(p, t,
                            {.rect = r,
                             .color = t.preview_box,
                             .focused = focus_ == Focus::Description,
                             .rounded = app_.config().rounded_corners,
                             .titles = {ui::title(t, "description")},
                             .right_titles = std::move(right),
                             .hints = {ui::hint(t, "H", s.show_hints ? "hide hints" : "hints"),
                                       ui::hint(t, "<>", "resize"), ui::hint(t, "L", "lang"),
                                       ui::hint(t, "gx", "browser")}});
    if (q == nullptr || in.w < 6 || in.h < 3) return;
    const Rect body{in.x + 1, in.y, in.w - 2, in.h};

    int y = body.y;
    int x = p.text(body.x, y, leetcode::to_string(q->difficulty),
                   ui::fg(ui::difficulty_color(t, q->difficulty), tui::AttrBold));
    if (!q->ac_rate.empty()) x = p.text(x, y, "  " + q->ac_rate, ui::fg(t.main_fg));
    x = p.text(
        x, y,
        std::format("  ▲ {}  ▼ {}", str::human_count(q->likes), str::human_count(q->dislikes)),
        ui::fg(t.graph_text));
    if (const auto* prob = app_.problem(s.slug);
        prob != nullptr && prob->status != leetcode::Status::Todo) {
        p.text(x + 2, y,
               std::format("{} {}", ui::status_icon(prob->status),
                           prob->status == leetcode::Status::Solved ? "solved" : "attempted"),
               ui::fg(ui::status_color(t, prob->status), tui::AttrBold));
    }
    ++y;
    p.text(body.x, y++, rule(body.w), ui::fg(t.div_line));

    const Rect doc{body.x, y, body.w - 1, body.bottom() - y}; // a column for the scrollbar
    desc_height_ = doc.h;
    if (q->content_html.empty()) {
        ui::draw_wrapped(p, doc,
                         q->paid_only ? "This is a premium question. Sign in with a premium "
                                        "account (:login) to read it."
                                      : "No description available.",
                         ui::fg(t.warning));
        desc_lines_ = 0;
        return;
    }
    const auto& lines = app_.description(*q, doc.w, s.show_hints);
    desc_lines_ = static_cast<int>(lines.size());
    s.desc_scroll = std::clamp(s.desc_scroll, 0, std::max(0, desc_lines_ - doc.h));
    ui::draw_document(p, doc, lines, s.desc_scroll, t);
    tui::scrollbar(p, in.right() - 1, doc.y, doc.h, s.desc_scroll, doc.h, desc_lines_,
                   ui::fg(t.div_line), ui::fg(t.preview_box));
}

void SolveView::draw_editor(const tui::Painter& p, Rect r, Session& s) {
    const auto& t = app_.theme();
    const std::string px = app_.prefix().to_string();
    std::vector<Segments> titles{ui::title(t, "solution")};
    if (!s.file.empty()) titles.push_back(ui::label(s.file.filename().string(), t.main_fg));
    std::vector<Segments> right;
    if (!s.editor_argv.empty()) {
        right.push_back(ui::label(std::filesystem::path(s.editor_argv.front()).filename().string(),
                                  t.graph_text));
    }
    if (s.lang != nullptr) right.push_back(ui::label(s.lang->name, t.accent));

    std::vector<Segments> hints;
    if (prefix_pending_) {
        hints.push_back(ui::label(px + " …  t run · s submit · h/j/l focus · z zoom · q back",
                                  t.hi_fg, tui::AttrBold));
    } else {
        // The prefix once, then btop-style words: "C-g + test submit zoom hjkl focus …".
        hints = {{{px, ui::fg(t.hi_fg, tui::AttrBold)}, {" +", ui::fg(t.graph_text)}},
                 ui::hint(t, "t", "test"),
                 ui::hint(t, "s", "submit"),
                 ui::hint(t, "z", "zoom"),
                 ui::hint(t, "hjkl", "focus"),
                 ui::hint(t, "q", "back"),
                 ui::hint(t, "?", "help")};
    }
    const Rect in = ui::box(p, t,
                            {.rect = r,
                             .color = t.editor_box,
                             .focused = focus_ == Focus::Editor,
                             .rounded = app_.config().rounded_corners,
                             .titles = std::move(titles),
                             .right_titles = std::move(right),
                             .hints = std::move(hints)});
    editor_inner_ = in;
    if (in.empty()) return;
    if (s.editor && s.editor->running()) {
        s.editor->resize(in.h, in.w);
        const tui::Color bg = app_.config().theme_background ? t.main_bg : tui::Color::none();
        s.editor->draw(p, in, tui::Color::none(), bg);
        return;
    }
    const int y = in.y + (in.h / 2) - 1;
    const std::string msg = s.editor ? "the editor exited" : "no editor running";
    p.text(in.x + std::max(0, (in.w - unicode::display_width(msg)) / 2), y, msg,
           ui::fg(t.graph_text));
    const std::string help = "Enter restart · q back · h description";
    p.text(in.x + std::max(0, (in.w - unicode::display_width(help)) / 2), y + 1, help,
           ui::fg(t.inactive_fg));
}

void SolveView::draw_panel(const tui::Painter& p, Rect r, Session& s) {
    const auto& t = app_.theme();
    std::vector<Segments> tabs;
    for (int i = 0; i < 3; ++i) {
        const bool active = s.panel_tab == i;
        tabs.push_back(ui::label(tab_names.at(static_cast<std::size_t>(i)),
                                 active ? t.title : t.inactive_fg,
                                 active ? tui::AttrBold : tui::AttrNone));
    }
    std::vector<Segments> right;
    if (s.job == JobState::Saving || s.job == JobState::Running) {
        right.push_back(ui::label(std::format("{} {}", tui::spinner(app_.tick()),
                                              s.job == JobState::Saving ? "saving"
                                              : s.job_status.empty() ? "sending"
                                                                     : str::to_lower(s.job_status)),
                                  t.warning));
    } else if (s.job == JobState::Done && s.result) {
        const bool ok =
            s.result->accepted() && (s.result->is_submission || s.result->correct_answer);
        right.push_back(ui::label(
            std::format("{} {}", verdict_icon(*s.result), ok ? "Accepted" : s.result->status_msg),
            ok ? t.success : t.error, tui::AttrBold));
    } else if (s.job == JobState::Failed) {
        right.push_back(ui::label("✗ error", t.error, tui::AttrBold));
    }
    std::vector<Segments> hints{ui::hint(t, "⇥", "tabs")};
    if (s.panel_tab == 0) hints.push_back(ui::hint(t, "h/l", "case"));
    if (s.panel_tab == 1) {
        hints.push_back(ui::hint(t, "e", "edit"));
        hints.push_back(ui::hint(t, "x", "reset"));
    }
    if (s.panel_tab == 2) hints.push_back(ui::hint(t, "R", "reload"));
    const Rect in = ui::box(p, t,
                            {.rect = r,
                             .color = t.result_box,
                             .focused = focus_ == Focus::Panel || focus_ == Focus::TestEditor,
                             .rounded = app_.config().rounded_corners,
                             .titles = std::move(tabs),
                             .right_titles = std::move(right),
                             .hints = std::move(hints)});
    panel_inner_ = in;
    if (in.empty()) return;
    if (s.test_editor) {
        if (s.test_editor->running()) {
            s.test_editor->resize(in.h, in.w);
            s.test_editor->draw(p, in, tui::Color::none(), tui::Color::none());
        }
        return;
    }
    const Rect body{in.x + 1, in.y, in.w - 2, in.h};
    panel_height_ = body.h;
    switch (s.panel_tab) {
    case 0: draw_result(p, body, s); break;
    case 1: draw_testcases(p, body, s); break;
    default: draw_submissions(p, body, s); break;
    }
}

void SolveView::draw_result(const tui::Painter& p, Rect r, Session& s) {
    const auto& t = app_.theme();
    Lines out{.rows = {}, .width = r.w};
    const tui::Style label = ui::fg(t.graph_text);
    const tui::Style value = ui::fg(t.main_fg);
    case_count_ = 0;

    if (s.job == JobState::Idle) {
        const std::string px = app_.prefix().to_string();
        out.text(std::format("{} t  runs your code on the testcases (examples, or your own via "
                             "the testcases tab).",
                             px),
                 value);
        out.blank();
        out.text(std::format("{} s  submits it to LeetCode's full test suite.", px), value);
    } else if (s.job == JobState::Saving || s.job == JobState::Running) {
        out.add({{std::string(tui::spinner(app_.tick())) + " ", ui::fg(t.warning)},
                 {s.job == JobState::Saving
                      ? "saving the solution…"
                      : std::format("{} on LeetCode… {}", s.job_is_submit ? "judging" : "running",
                                    str::to_lower(s.job_status)),
                  value}});
    } else if (s.job == JobState::Failed) {
        out.text("✗ " + s.error, ui::fg(t.error, tui::AttrBold));
        if (s.error.contains("sign") || s.error.contains("login") || s.error.contains("auth")) {
            out.blank();
            out.text("Run :login and paste your LeetCode cookie.", label);
        }
    } else if (s.result) {
        const auto& res = *s.result;
        const bool ok = res.accepted() && (res.is_submission || res.correct_answer);
        Segments head{
            {std::format("{} ", verdict_icon(res)),
             ui::fg(ok ? t.success : t.error, tui::AttrBold)},
            {ok ? "Accepted" : res.status_msg, ui::fg(ok ? t.success : t.error, tui::AttrBold)}};
        if (res.total_testcases > 0) {
            head.push_back(
                {std::format("   {}/{} testcases", res.total_correct, res.total_testcases), label});
        }
        out.add(std::move(head));

        if (!res.compile_error.empty() || !res.runtime_error.empty()) {
            out.blank();
            out.text(!res.compile_error.empty() ? res.compile_error : res.runtime_error,
                     ui::fg(t.error));
        }

        if (res.is_submission) {
            if (res.accepted()) {
                const auto meter_row = [&](std::string_view name, const std::string& amount,
                                           std::optional<double> pct) {
                    out.add({{unicode::pad_right(name, 9), label},
                             {unicode::pad_right(amount, 10), ui::fg(t.title, tui::AttrBold)},
                             {pct ? std::format("beats {:5.1f}%", *pct) : "", value}});
                };
                const auto& d = s.detail;
                meter_row("Runtime", d ? d->runtime_display : res.runtime,
                          d ? d->runtime_percentile : res.runtime_percentile);
                meter_row("Memory", d ? d->memory_display : res.memory,
                          d ? d->memory_percentile : res.memory_percentile);
            } else if (!res.last_testcase.empty() || !res.expected_output.empty()) {
                out.blank();
                if (s.question) {
                    out.add({{"Input", label}});
                    for (const auto& [name, v] : label_case(*s.question, res.last_testcase)) {
                        out.text(name.empty() ? v : std::format("{} = {}", name, v), value, 2);
                    }
                }
                out.field("Output", res.code_output, label, ui::fg(t.error));
                out.field("Expected", res.expected_output, label, ui::fg(t.success));
                if (!res.std_output.empty()) out.field("Stdout", res.std_output, label, value);
            }
        } else {
            // Run: one tab per testcase.
            case_count_ = static_cast<int>(s.cases_run.size());
            s.selected_case = std::clamp(s.selected_case, 0, std::max(0, case_count_ - 1));
            Segments tabs;
            for (int i = 0; i < case_count_; ++i) {
                const auto idx = static_cast<std::size_t>(i);
                const bool pass = idx < res.compare_result.size() ? res.compare_result[idx] == '1'
                                                                  : res.correct_answer;
                const bool sel = i == s.selected_case;
                tabs.push_back({sel ? "[" : " ", ui::fg(t.hi_fg)});
                tabs.push_back(
                    {std::format("{} {}", pass ? "✓" : "✗", i + 1),
                     ui::fg(pass ? t.success : t.error, sel ? tui::AttrBold : tui::AttrNone)});
                tabs.push_back({sel ? "]" : " ", ui::fg(t.hi_fg)});
            }
            if (!res.runtime.empty()) tabs.push_back({"   " + res.runtime, label});
            out.add(std::move(tabs));
            out.blank();
            if (case_count_ > 0 && s.question) {
                const auto idx = static_cast<std::size_t>(s.selected_case);
                out.add({{"Input", label}});
                for (const auto& [name, v] : label_case(*s.question, s.cases_run[idx])) {
                    out.text(name.empty() ? v : std::format("{} = {}", name, v), value, 2);
                }
                const auto at = [](const std::vector<std::string>& v, std::size_t i) {
                    return i < v.size() ? v[i] : std::string();
                };
                const std::string got = at(res.answers, idx);
                const std::string want = at(res.expected_answers, idx);
                out.field("Output", got, label, ui::fg(got == want ? t.success : t.error));
                out.field("Expected", want, label, value);
                if (const auto so = at(res.stdout_per_case, idx); !so.empty()) {
                    out.field("Stdout", so, label, value);
                }
            }
        }
    }

    panel_lines_ = static_cast<int>(out.rows.size());
    s.panel_scroll = std::clamp(s.panel_scroll, 0, std::max(0, panel_lines_ - r.h));
    draw_lines(p, r, out.rows, s.panel_scroll);

    // Runtime distribution under an accepted submission, highlighting the user's bucket.
    if (s.result && s.result->is_submission && s.result->accepted() && s.detail
        && !s.detail->runtime_distribution.buckets.empty()) {
        const int top = r.y + panel_lines_ + 1 - s.panel_scroll;
        const Rect chart{r.x, top + 1, r.w, r.bottom() - top - 2};
        if (chart.h >= 2 && chart.w >= 10) {
            const auto& buckets = s.detail->runtime_distribution.buckets;
            std::vector<double> values;
            int mine = -1;
            const std::string runtime = s.detail->runtime_display;
            for (std::size_t i = 0; i < buckets.size(); ++i) {
                values.push_back(buckets[i].second);
                if (runtime.starts_with(buckets[i].first + " ")) mine = static_cast<int>(i);
            }
            p.text(r.x, top, "runtime distribution", label);
            tui::bar_chart(p, chart, values, mine, t.graph_text, t.success);
            p.text(r.x, chart.bottom(), buckets.front().first + " ms", label);
            const std::string last = buckets.back().first + " ms";
            p.text(r.right() - unicode::display_width(last), chart.bottom(), last, label);
        }
    }
}

void SolveView::draw_testcases(const tui::Painter& p, Rect r, Session& s) {
    const auto& t = app_.theme();
    Lines out{.rows = {}, .width = r.w};
    case_count_ = 0;
    if (!s.question) return;
    const auto cases = app_.workspace().testcases(*s.question);
    const auto custom = std::filesystem::exists(app_.workspace().testcases_path(s.slug));
    out.add({{custom ? "custom testcases" : "example testcases", ui::fg(t.graph_text)},
             {custom ? "  (x resets to the examples)" : "  (e to edit)", ui::fg(t.inactive_fg)}});
    for (std::size_t i = 0; i < cases.size(); ++i) {
        out.blank();
        out.add({{std::format("Case {}", i + 1), ui::fg(t.title, tui::AttrBold)}});
        for (const auto& [name, v] : label_case(*s.question, cases[i])) {
            out.text(name.empty() ? v : std::format("{} = {}", name, v), ui::fg(t.main_fg), 2);
        }
    }
    panel_lines_ = static_cast<int>(out.rows.size());
    s.panel_scroll = std::clamp(s.panel_scroll, 0, std::max(0, panel_lines_ - r.h));
    draw_lines(p, r, out.rows, s.panel_scroll);
}

void SolveView::draw_submissions(const tui::Painter& p, Rect r, Session& s) {
    const auto& t = app_.theme();
    case_count_ = 0;
    if (!app_.client().has_credentials()) {
        ui::draw_wrapped(p, r, "Sign in (:login) to see your submissions.", ui::fg(t.graph_text));
        return;
    }
    if (s.submissions_loading) {
        p.text(r.x, r.y, std::format("{} loading submissions…", tui::spinner(app_.tick())),
               ui::fg(t.graph_text));
        return;
    }
    if (s.submissions.empty()) {
        p.text(r.x, r.y, s.submissions_loaded ? "No submissions yet." : "", ui::fg(t.graph_text));
        return;
    }
    Lines out{.rows = {}, .width = r.w};
    for (const auto& sub : s.submissions) {
        out.add({{unicode::pad_right(sub.accepted ? "✓ " + sub.status : "✗ " + sub.status, 24),
                  ui::fg(sub.accepted ? t.success : t.error)},
                 {unicode::pad_right(sub.lang, 11), ui::fg(t.accent)},
                 {unicode::pad_right(sub.runtime, 9), ui::fg(t.main_fg)},
                 {unicode::pad_right(sub.memory, 10), ui::fg(t.main_fg)},
                 {ui::time_ago(sub.timestamp), ui::fg(t.graph_text)}});
    }
    panel_lines_ = static_cast<int>(out.rows.size());
    s.panel_scroll = std::clamp(s.panel_scroll, 0, std::max(0, panel_lines_ - r.h));
    draw_lines(p, r, out.rows, s.panel_scroll);
}

} // namespace lv::app
