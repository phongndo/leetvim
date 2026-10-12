#include "app/home.hpp"

#include "app/ui.hpp"
#include "util/fuzzy.hpp"
#include "util/strings.hpp"
#include "util/unicode.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <format>
#include <map>
#include <random>
#include <utility>

namespace lv::app {

using leetcode::Difficulty;
using leetcode::ProblemSummary;
using leetcode::Status;
using tui::Key;
using tui::KeyEvent;
using tui::ModCtrl;
using tui::Rect;

namespace {

constexpr std::array<std::string_view, 5> sort_names{"id", "difficulty", "acceptance", "title",
                                                     "status"};

int difficulty_index(Difficulty d) {
    switch (d) {
    case Difficulty::Easy: return 0;
    case Difficulty::Medium: return 1;
    case Difficulty::Hard: return 2;
    case Difficulty::Unknown: break;
    }
    return -1;
}

std::string search_key(const ProblemSummary& p) { return p.frontend_id + " " + p.title; }

/// Filter toggles, applied live.
class FilterOverlay : public Overlay {
public:
    FilterOverlay(App& app, HomeView& home) : app_(app), home_(home) {}

    void draw(const tui::Painter& p, Rect area) override {
        const auto& t = app_.theme();
        const Rect r = centered(area, 58, 10);
        p.clear(r, app_.base_bg());
        const Rect in =
            ui::box(p, t,
                    {.rect = r,
                     .color = t.hi_fg,
                     .focused = true,
                     .rounded = true,
                     .titles = {ui::title(t, "filter")},
                     .right_titles = {},
                     .hints = {ui::hint(t, "c", "clear"), ui::hint(t, "esc", "close")}});
        const auto& f = home_.filter();
        const auto opt = [&](int x, int y, std::string_view key, std::string_view name, bool on,
                             tui::Color color) {
            x = p.text(x, y, on ? "● " : "○ ", ui::fg(on ? color : t.inactive_fg));
            return tui::draw_segments(p, x, y, ui::hint(t, key, name)) + 2;
        };
        int y = in.y + 1;
        const int col = in.x + 13;
        p.text(in.x + 1, y, "difficulty", ui::fg(t.graph_text));
        int x = opt(col, y, "e", "easy", f.difficulty[0], t.easy);
        x = opt(x, y, "m", "medium", f.difficulty[1], t.medium);
        opt(x, y, "h", "hard", f.difficulty[2], t.hard);
        y += 2;
        p.text(in.x + 1, y, "status", ui::fg(t.graph_text));
        x = opt(col, y, "t", "todo", f.status[0], t.main_fg);
        x = opt(x, y, "a", "attempted", f.status[1], t.attempted);
        opt(x, y, "s", "solved", f.status[2], t.solved);
        y += 2;
        p.text(in.x + 1, y, "premium", ui::fg(t.graph_text));
        opt(col, y, "p", "hide premium", f.hide_paid, t.warning);
        y += 2;
        p.text(in.x + 1, y, "topics", ui::fg(t.graph_text));
        x = tui::draw_segments(p, col + 2, y, ui::hint(t, "T", "topics"));
        std::string tags;
        for (const auto& tag : f.tags) tags += (tags.empty() ? "" : ", ") + tag;
        p.text(x + 2, y, tags.empty() ? "choose…" : tags, ui::fg(t.accent), in.right() - x - 3);
    }

    bool on_key(const KeyEvent& ev) override {
        auto& f = home_.filter();
        if (ev.is(Key::Escape) || ev.is('q') || ev.is(Key::Enter) || ev.is('f')) {
            closed_ = true;
            return true;
        }
        if (ev.is('e'))
            f.difficulty[0] = !f.difficulty[0];
        else if (ev.is('m'))
            f.difficulty[1] = !f.difficulty[1];
        else if (ev.is('h'))
            f.difficulty[2] = !f.difficulty[2];
        else if (ev.is('t'))
            f.status[0] = !f.status[0];
        else if (ev.is('a'))
            f.status[1] = !f.status[1];
        else if (ev.is('s'))
            f.status[2] = !f.status[2];
        else if (ev.is('p'))
            f.hide_paid = !f.hide_paid;
        else if (ev.is('c'))
            f = Filter{};
        else if (ev.is('T')) {
            closed_ = true;
            home_.on_key(KeyEvent{.key = Key::Char, .ch = 't', .mods = 0, .raw = "t"});
            return true;
        }
        home_.refilter();
        return true;
    }

private:
    App& app_;
    HomeView& home_;
};

} // namespace

// ---------------------------------------------------------------- Filter

bool Filter::active() const {
    return std::ranges::any_of(difficulty, std::identity{})
           || std::ranges::any_of(status, std::identity{}) || hide_paid || !tags.empty();
}

bool Filter::matches(const ProblemSummary& p) const {
    if (std::ranges::any_of(difficulty, std::identity{})) {
        const int d = difficulty_index(p.difficulty);
        if (d < 0 || !difficulty.at(static_cast<std::size_t>(d))) return false;
    }
    if (std::ranges::any_of(status, std::identity{})
        && !status.at(static_cast<std::size_t>(p.status))) {
        return false;
    }
    if (hide_paid && p.paid_only) return false;
    for (const auto& tag : tags) {
        if (std::ranges::none_of(p.tags, [&](const auto& t) { return t.slug == tag; })) {
            return false;
        }
    }
    return true;
}

std::string Filter::describe() const {
    std::vector<std::string> parts;
    static constexpr std::array<std::string_view, 3> diff{"easy", "medium", "hard"};
    static constexpr std::array<std::string_view, 3> stat{"todo", "attempted", "solved"};
    std::string d;
    for (std::size_t i = 0; i < 3; ++i) {
        if (difficulty.at(i)) d += (d.empty() ? "" : " ") + std::string(diff.at(i));
    }
    if (!d.empty()) parts.push_back(d);
    std::string s;
    for (std::size_t i = 0; i < 3; ++i) {
        if (status.at(i)) s += (s.empty() ? "" : " ") + std::string(stat.at(i));
    }
    if (!s.empty()) parts.push_back(s);
    if (hide_paid) parts.emplace_back("free");
    for (const auto& tag : tags) parts.push_back("#" + tag);
    return str::join(parts, " · ");
}

// ---------------------------------------------------------------- list state

void HomeView::rebuild() {
    const auto& data = app_.data();
    const std::uint64_t version = data.problems_version;
    if (built_version_ == version && applied_search_ == search_.text()) return;
    built_version_ = version;
    applied_search_ = search_.text();

    const auto& problems = data.problems;
    const bool show_paid = app_.config().show_paid;
    const std::string query(str::trim(applied_search_));
    std::vector<std::pair<int, std::size_t>> scored;
    scored.reserve(problems.size());
    for (std::size_t i = 0; i < problems.size(); ++i) {
        const auto& p = problems[i];
        if (!filter_.matches(p) || (!show_paid && p.paid_only)) continue;
        if (query.empty()) {
            scored.emplace_back(0, i);
            continue;
        }
        if (p.frontend_id == query) {
            scored.emplace_back(1'000'000, i);
        } else if (const auto score = fuzzy::match(query, search_key(p))) {
            scored.emplace_back(*score, i);
        }
    }

    const auto key_less = [&](std::size_t a, std::size_t b) {
        const auto& x = problems[a];
        const auto& y = problems[b];
        switch (sort_) {
        case SortKey::Difficulty:
            if (x.difficulty != y.difficulty) return x.difficulty < y.difficulty;
            break;
        case SortKey::Acceptance:
            if (x.ac_rate != y.ac_rate) return x.ac_rate > y.ac_rate;
            break;
        case SortKey::Title:
            if (x.title != y.title) return str::to_lower(x.title) < str::to_lower(y.title);
            break;
        case SortKey::Status:
            if (x.status != y.status) return x.status > y.status;
            break;
        case SortKey::Id: break;
        }
        return x.number < y.number;
    };
    if (!query.empty() && sort_ == SortKey::Id) {
        // Searching: best matches first.
        std::ranges::stable_sort(scored, [&](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first > b.first;
            return problems[a.second].number < problems[b.second].number;
        });
    } else {
        std::ranges::stable_sort(scored, [&](const auto& a, const auto& b) {
            return reverse_ ? key_less(b.second, a.second) : key_less(a.second, b.second);
        });
    }
    rows_.clear();
    for (const auto& [score, i] : scored) rows_.push_back(i);

    // Keep the selection on the same problem when possible.
    int found = -1;
    for (std::size_t r = 0; r < rows_.size(); ++r) {
        if (problems[rows_[r]].slug == selected_slug_) found = static_cast<int>(r);
    }
    if (found >= 0 && query.empty()) {
        selected_ = found;
    } else {
        select(query.empty() ? std::min(selected_, static_cast<int>(rows_.size()) - 1) : 0);
    }
}

const ProblemSummary* HomeView::selected() const {
    if (selected_ < 0 || static_cast<std::size_t>(selected_) >= rows_.size()) return nullptr;
    return &app_.data().problems[rows_[static_cast<std::size_t>(selected_)]];
}

void HomeView::select(int row) {
    selected_ = std::clamp(row, 0, std::max(0, static_cast<int>(rows_.size()) - 1));
    if (const auto* p = selected(); p != nullptr && p->slug != selected_slug_) {
        selected_slug_ = p->slug;
        selected_at_ = Clock::now();
        preview_scroll_ = 0;
    }
}

void HomeView::select_slug(const std::string& slug) {
    selected_slug_ = slug;
    built_version_ = UINT64_MAX;
    rebuild();
    for (std::size_t r = 0; r < rows_.size(); ++r) {
        if (app_.data().problems[rows_[r]].slug == slug) {
            selected_ = static_cast<int>(r);
            scroll_ = std::max(0, selected_ - (list_height_ / 2));
            selected_at_ = Clock::now();
            preview_scroll_ = 0;
            return;
        }
    }
}

void HomeView::move(int delta) { select(selected_ + delta); }

void HomeView::open_selected() {
    if (const auto* p = selected()) app_.open_problem(p->slug);
}

void HomeView::pick_random() {
    std::vector<std::size_t> pool;
    const auto& user = app_.data().user;
    const bool premium = user.has_value() && user->premium;
    for (std::size_t r = 0; r < rows_.size(); ++r) {
        const auto& p = app_.data().problems[rows_[r]];
        if (premium || !p.paid_only) pool.push_back(r);
    }
    if (pool.empty()) {
        app_.toast("nothing to pick from", Level::Warning);
        return;
    }
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
    select(static_cast<int>(pool[pick(rng)]));
    scroll_ = std::max(0, selected_ - (list_height_ / 2));
    if (const auto* p = selected()) {
        app_.toast(std::format("random pick: {}. {}", p->frontend_id, p->title));
    }
}

void HomeView::show_filter() { app_.push_overlay(std::make_unique<FilterOverlay>(app_, *this)); }

void HomeView::show_topics() {
    std::map<std::string, std::pair<std::string, int>> tags; // slug -> (name, count)
    for (const auto& p : app_.data().problems) {
        for (const auto& t : p.tags) {
            auto& entry = tags[t.slug];
            entry.first = t.name;
            ++entry.second;
        }
    }
    std::vector<PickerOverlay::Item> items;
    items.reserve(tags.size());
    for (const auto& [slug, info] : tags) {
        items.push_back({.label = info.first,
                         .detail = std::to_string(info.second),
                         .value = slug,
                         .color = {}});
    }
    std::ranges::sort(items, [](const auto& a, const auto& b) { return a.label < b.label; });
    const std::set<std::string> checked(filter_.tags.begin(), filter_.tags.end());
    app_.push_overlay(std::make_unique<PickerOverlay>(
        app_, "topics", std::move(items),
        PickerOverlay::Callbacks{.on_pick = {},
                                 .on_done =
                                     [this](const std::set<std::string>& chosen) {
                                         filter_.tags.assign(chosen.begin(), chosen.end());
                                         refilter();
                                     },
                                 .on_highlight = {},
                                 .on_cancel = {}},
        true, checked));
}

// ---------------------------------------------------------------- input

bool HomeView::on_key(const KeyEvent& ev) {
    rebuild();
    if (searching_) {
        if (ev.is(Key::Escape) || ev.is('c', ModCtrl)) {
            searching_ = false;
            search_.set({});
        } else if (ev.is(Key::Enter)) {
            searching_ = false;
            if (ev.is(Key::Enter) && rows_.size() == 1) open_selected();
        } else if (ev.is(Key::Down) || ev.is('n', ModCtrl) || ev.is('j', ModCtrl)) {
            move(1);
        } else if (ev.is(Key::Up) || ev.is('p', ModCtrl) || ev.is('k', ModCtrl)) {
            move(-1);
        } else if (ev.is(Key::Backspace) && search_.text().empty()) {
            searching_ = false;
        } else {
            search_.on_key(ev);
        }
        rebuild();
        return true;
    }

    if (pending_g_) {
        pending_g_ = false;
        if (ev.is('g')) {
            if (focus_preview_) {
                preview_scroll_ = 0;
            } else {
                select(0);
            }
            return true;
        }
        if (ev.is('x')) {
            if (const auto* p = selected()) app_.open_url(app_.client().problem_url(p->slug));
            return true;
        }
    }

    const int page = std::max(1, list_height_ - 1);
    const auto scroll_preview = [&](int delta) {
        preview_scroll_ =
            std::clamp(preview_scroll_ + delta, 0, std::max(0, preview_lines_ - preview_height_));
    };

    if (focus_preview_) {
        const int ppage = std::max(1, preview_height_ - 1);
        if (ev.is('j') || ev.is(Key::Down))
            scroll_preview(1);
        else if (ev.is('k') || ev.is(Key::Up))
            scroll_preview(-1);
        else if (ev.is('d', ModCtrl))
            scroll_preview(ppage / 2);
        else if (ev.is('u', ModCtrl))
            scroll_preview(-ppage / 2);
        else if (ev.is('f', ModCtrl) || ev.is(Key::PageDown))
            scroll_preview(ppage);
        else if (ev.is('b', ModCtrl) || ev.is(Key::PageUp))
            scroll_preview(-ppage);
        else if (ev.is('G'))
            scroll_preview(1'000'000);
        else if (ev.is('g'))
            pending_g_ = true;
        else if (ev.is(Key::Tab) || ev.is('h') || ev.is(Key::Escape))
            focus_preview_ = false;
        else if (ev.is(Key::Enter) || ev.is('l'))
            open_selected();
        else
            return false;
        return true;
    }

    if (ev.is('j') || ev.is(Key::Down))
        move(1);
    else if (ev.is('k') || ev.is(Key::Up))
        move(-1);
    else if (ev.is('d', ModCtrl))
        move(page / 2);
    else if (ev.is('u', ModCtrl))
        move(-page / 2);
    else if (ev.is('f', ModCtrl) || ev.is(Key::PageDown))
        move(page);
    else if (ev.is('b', ModCtrl) || ev.is(Key::PageUp))
        move(-page);
    else if (ev.is('G') || ev.is(Key::End))
        select(static_cast<int>(rows_.size()) - 1);
    else if (ev.is(Key::Home))
        select(0);
    else if (ev.is('g'))
        pending_g_ = true;
    else if (ev.is(Key::Enter) || ev.is('l'))
        open_selected();
    else if (ev.is('/')) {
        searching_ = true;
        search_.set(search_.text());
    } else if (ev.is(Key::Escape)) {
        if (!search_.text().empty()) {
            search_.set({});
        } else if (filter_.active()) {
            filter_ = Filter{};
            refilter();
        } else {
            return false;
        }
    } else if (ev.is('f'))
        show_filter();
    else if (ev.is('t'))
        show_topics();
    else if (ev.is('o')) {
        sort_ = static_cast<SortKey>((static_cast<int>(sort_) + 1) % sort_names.size());
        refilter();
    } else if (ev.is('O')) {
        reverse_ = !reverse_;
        refilter();
    } else if (ev.is('d'))
        app_.open_daily();
    else if (ev.is('r'))
        pick_random();
    else if (ev.is('J'))
        scroll_preview(1);
    else if (ev.is('K'))
        scroll_preview(-1);
    else if (ev.is(Key::Tab))
        focus_preview_ = show_preview_ && preview_rect_.w > 0;
    else if (ev.is('1'))
        show_stats_ = !show_stats_;
    else if (ev.is('2'))
        show_preview_ = !show_preview_;
    else if (ev.is('s'))
        app_.show_stats();
    else if (ev.is('R')) {
        app_.sync_problems(true);
        app_.refresh_user();
    } else if (ev.is('y')) {
        if (const auto* p = selected()) app_.yank(app_.client().problem_url(p->slug), "link");
    } else {
        return false;
    }
    rebuild();
    return true;
}

bool HomeView::on_paste(const std::string& text) {
    if (!searching_) return false;
    search_.insert(text);
    rebuild();
    return true;
}

bool HomeView::on_mouse(const tui::MouseEvent& ev) {
    using Kind = tui::MouseEvent::Kind;
    const bool in_list = list_rect_.contains(ev.x, ev.y);
    const bool in_preview = preview_rect_.contains(ev.x, ev.y);
    if (ev.kind == Kind::WheelDown || ev.kind == Kind::WheelUp) {
        const int delta = ev.kind == Kind::WheelDown ? 3 : -3;
        if (in_preview) {
            preview_scroll_ = std::clamp(preview_scroll_ + delta, 0,
                                         std::max(0, preview_lines_ - preview_height_));
        } else if (in_list) {
            scroll_ = std::clamp(scroll_ + delta, 0,
                                 std::max(0, static_cast<int>(rows_.size()) - list_height_));
            select(std::clamp(selected_, scroll_, scroll_ + list_height_ - 1));
        }
        return true;
    }
    if (ev.kind != Kind::Press || ev.button != 0) return false;
    if (in_preview) {
        focus_preview_ = true;
        return true;
    }
    if (!in_list) return false;
    focus_preview_ = false;
    const int row = scroll_ + (ev.y - list_rect_.y);
    if (row < 0 || std::cmp_greater_equal(row, rows_.size())) return true;
    const auto now = Clock::now();
    if (row == last_click_row_ && now - last_click_ < std::chrono::milliseconds(400)) {
        select(row);
        open_selected();
        last_click_row_ = -1;
        return true;
    }
    last_click_row_ = row;
    last_click_ = now;
    select(row);
    return true;
}

bool HomeView::on_tick() {
    // Fetch the preview once the selection has settled, so holding j doesn't fire
    // a request per row.
    if (!selected_slug_.empty() && show_preview_
        && Clock::now() - selected_at_ > std::chrono::milliseconds(120)
        && !app_.data().questions.contains(selected_slug_)
        && !app_.data().loading_questions.contains(selected_slug_)
        && !app_.data().question_errors.contains(selected_slug_)) {
        app_.question(selected_slug_);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- drawing

void HomeView::draw(const tui::Painter& p, Rect area) {
    rebuild();
    cursor_.reset();
    int y = area.y;
    if (show_stats_ && area.h >= 24) {
        const int h = std::clamp(area.h / 4, 8, 12);
        draw_stats(p, {area.x, y, area.w, h});
        y += h;
    }
    const Rect rest{area.x, y, area.w, area.bottom() - y};
    int preview_w = 0;
    if (show_preview_ && rest.w >= 90) preview_w = std::clamp(rest.w * 9 / 20, 40, 110);
    draw_list(p, {rest.x, rest.y, rest.w - preview_w, rest.h});
    preview_rect_ = {};
    if (preview_w > 0) draw_preview(p, {rest.right() - preview_w, rest.y, preview_w, rest.h});
    if (preview_w == 0) focus_preview_ = false;
}

void HomeView::draw_stats(const tui::Painter& p, Rect r) {
    const auto& t = app_.theme();
    const auto& data = app_.data();
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&tt, &local);
    const std::string clock =
        std::format("{:02}:{:02}:{:02}", local.tm_hour, local.tm_min, local.tm_sec);

    std::vector<tui::Segments> right;
    if (data.user && data.user->signed_in) {
        right.push_back(ui::label(data.user->username + (data.user->premium ? " ★" : ""), t.title,
                                  tui::AttrBold));
    } else if (!app_.username().empty()) {
        right.push_back(ui::label(app_.username(), t.title));
    } else {
        right.push_back(ui::hint(t, ":login", "link your account"));
    }
    const Rect in = ui::box(p, t,
                            {.rect = r,
                             .color = t.stats_box,
                             .focused = true,
                             .rounded = app_.config().rounded_corners,
                             .titles = {ui::title(t, "stats", 1)},
                             .right_titles = std::move(right),
                             .hints = {}});
    // Clock centered on the top border, like btop.
    const tui::Segments clock_label = ui::label(clock, t.title, tui::AttrBold);
    tui::top_label(p, r.x + ((r.w - static_cast<int>(clock.size()) - 2) / 2), r.y, clock_label,
                   ui::fg(t.stats_box));
    if (in.empty()) return;

    // Right: solved meters in a sub-box.
    const int side_w = std::min(46, in.w / 2);
    const Rect side{in.right() - side_w, in.y, side_w, in.h};
    const Rect side_in = ui::box(p, t,
                                 {.rect = side,
                                  .color = t.div_line,
                                  .focused = true,
                                  .rounded = app_.config().rounded_corners,
                                  .titles = {ui::title(t, "solved")},
                                  .right_titles = {},
                                  .hints = {}});

    leetcode::PerDifficulty solved;
    leetcode::PerDifficulty total;
    if (data.stats) {
        solved = data.stats->solved;
        total = data.stats->questions;
    }
    if (total.all == 0) {
        for (const auto& prob : data.problems) {
            ++total.all;
            const bool ok = prob.status == Status::Solved;
            solved.all += ok ? 1 : 0;
            switch (prob.difficulty) {
            case Difficulty::Easy:
                ++total.easy;
                solved.easy += ok ? 1 : 0;
                break;
            case Difficulty::Medium:
                ++total.medium;
                solved.medium += ok ? 1 : 0;
                break;
            case Difficulty::Hard:
                ++total.hard;
                solved.hard += ok ? 1 : 0;
                break;
            case Difficulty::Unknown: break;
            }
        }
    }
    int sy = side_in.y;
    const auto meter_row = [&](std::string_view name, Difficulty d) {
        if (sy >= side_in.bottom()) return;
        const tui::Color c = ui::difficulty_color(t, d);
        const int got = solved.get(d);
        const int of = total.get(d);
        const std::string count = std::format("{}/{}", got, of);
        p.text(side_in.x + 1, sy, name, ui::fg(c, tui::AttrBold));
        const int mw = side_in.w - 9 - static_cast<int>(count.size()) - 2;
        tui::meter(p, side_in.x + 8, sy, mw, of > 0 ? static_cast<double>(got) / of : 0.0,
                   tui::shade(c, 0.45), tui::shade(c, 0.75), c, t.meter_bg);
        p.text(side_in.right() - static_cast<int>(count.size()) - 1, sy, count, ui::fg(t.main_fg));
        ++sy;
    };
    meter_row("Easy", Difficulty::Easy);
    meter_row("Medium", Difficulty::Medium);
    meter_row("Hard", Difficulty::Hard);
    if (sy < side_in.bottom()) {
        int x = p.text(side_in.x + 1, sy, "Total ", ui::fg(t.graph_text));
        x = p.text(x, sy, std::format("{}/{}", solved.all, total.all),
                   ui::fg(t.title, tui::AttrBold));
        if (data.stats && data.stats->ranking > 0) {
            x = p.text(x, sy, "  rank ", ui::fg(t.graph_text));
            p.text(x, sy, ui::thousands(data.stats->ranking), ui::fg(t.main_fg));
        }
        ++sy;
    }
    if (sy < side_in.bottom()) {
        int x = side_in.x + 1;
        const int streak = data.streak ? data.streak->count : (data.stats ? data.stats->streak : 0);
        x = p.text(x, sy, "streak ", ui::fg(t.graph_text));
        x = p.text(x, sy, std::format("{}d", streak),
                   ui::fg(streak > 0 ? t.warning : t.main_fg, tui::AttrBold));
        if (data.stats) {
            x = p.text(x, sy, "  active ", ui::fg(t.graph_text));
            p.text(x, sy, std::format("{}d", data.stats->total_active_days), ui::fg(t.main_fg));
        }
        ++sy;
    }
    if (sy < side_in.bottom() && data.daily) {
        const auto& d = *data.daily;
        int x = p.text(side_in.x + 1, sy, "daily ", ui::fg(t.graph_text));
        x = p.text(x, sy, d.completed ? "✓ " : "✗ ",
                   ui::fg(d.completed ? t.solved : t.error, tui::AttrBold));
        x = p.text(x, sy, "d", ui::fg(t.hi_fg, tui::AttrBold));
        p.text(x + 1, sy, std::format("{}. {}", d.problem.frontend_id, d.problem.title),
               ui::fg(ui::difficulty_color(t, d.problem.difficulty)), side_in.right() - x - 2);
    }

    // Left: braille graph of daily submissions (UTC days), newest at the right.
    const Rect graph{in.x + 1, in.y + 1, in.w - side_w - 2, in.h - 1};
    if (graph.w < 8 || graph.h < 1) return;
    if (!data.stats) {
        const std::string msg = data.stats_loading
                                    ? std::string(tui::spinner(app_.tick())) + " loading stats"
                                : !data.stats_error.empty()
                                    ? data.stats_error
                                    : "sign in with :login (or set username) to see your activity";
        ui::draw_wrapped(p, {graph.x, graph.y + (graph.h / 2), graph.w, 2}, msg,
                         ui::fg(t.graph_text));
        return;
    }
    const int days = graph.w * 2;
    const auto today = std::chrono::floor<std::chrono::days>(now);
    std::vector<double> values(static_cast<std::size_t>(days), 0.0);
    int peak = 0;
    int recent = 0;
    for (const auto& [ts, count] : data.stats->calendar) {
        const auto day = std::chrono::floor<std::chrono::days>(
            std::chrono::sys_seconds{std::chrono::seconds{ts}});
        const auto ago = (today - day).count();
        if (ago < 0 || ago >= days) continue;
        values[static_cast<std::size_t>(days - 1 - ago)] = count;
        peak = std::max(peak, count);
        recent += count;
    }
    if (peak > 0) {
        for (auto& v : values) v = std::sqrt(v / peak); // sqrt keeps small days visible
    }
    tui::braille_graph(p, graph, values, t.grad_start, t.grad_mid, t.grad_end);
    p.text(in.x + 1, in.y, std::format("submissions · last {} days", days), ui::fg(t.graph_text));
    const std::string summary = std::format("{} total · peak {}", recent, peak);
    p.text(graph.right() - static_cast<int>(summary.size()), in.y, summary, ui::fg(t.graph_text));
}

void HomeView::draw_list(const tui::Painter& p, Rect r) {
    const auto& t = app_.theme();
    const auto& data = app_.data();
    std::vector<tui::Segments> titles{ui::title(t, "problems")};
    if (searching_ || !search_.text().empty()) {
        tui::Segments s = ui::label("/", t.hi_fg, tui::AttrBold);
        s.push_back({search_.text() + (searching_ ? " " : ""), ui::fg(t.title)});
        titles.push_back(std::move(s));
    }
    if (filter_.active()) titles.push_back(ui::label(filter_.describe(), t.accent));

    std::vector<tui::Segments> right;
    if (data.syncing) {
        right.push_back(ui::label(std::format("{} syncing {}/{}", tui::spinner(app_.tick()),
                                              data.sync_done, data.sync_total),
                                  t.warning));
    }
    right.push_back(ui::label(
        std::format("{}{}", sort_names.at(static_cast<std::size_t>(sort_)), reverse_ ? "↓" : "↑"),
        t.graph_text));
    right.push_back(ui::label(std::format("{}/{}", rows_.size(), data.problems.size()), t.main_fg));

    const Rect in = ui::box(p, t,
                            {.rect = r,
                             .color = t.list_box,
                             .focused = !focus_preview_,
                             .rounded = app_.config().rounded_corners,
                             .titles = std::move(titles),
                             .right_titles = std::move(right),
                             .hints = {ui::hint(t, "/", "search"), ui::hint(t, "f", "filter"),
                                       ui::hint(t, "t", "topics"), ui::hint(t, "o", "sort"),
                                       ui::hint(t, "d", "daily"), ui::hint(t, "r", "random"),
                                       ui::hint(t, "s", "stats"), ui::hint(t, "?", "help")}});
    if (in.h < 2) return;

    if (searching_) {
        // Cursor at the end of the search text in the title.
        const int x = r.x + 1 + tui::segments_width(ui::title(t, "problems")) + 2 + 2
                      + unicode::display_width(search_.text());
        cursor_ = tui::Cursor{.x = x, .y = r.y, .shape = tui::CursorShape::Bar};
    }

    // Columns.
    const int w = in.w - 1; // leave the scrollbar column
    const bool tags_col = w >= 100;
    const int tag_w = tags_col ? std::min(30, w / 4) : 0;
    const int fixed = 2 + 6 + 2 + 8 + 7 + (tags_col ? tag_w + 1 : 0);
    const int title_w = std::max(10, w - fixed);
    const int x_status = in.x;
    const int x_id = x_status + 2;
    const int x_title = x_id + 6;
    const int x_paid = x_title + title_w;
    const int x_diff = x_paid + 2;
    const int x_ac = x_diff + 8;
    const int x_tags = x_ac + 8;

    const tui::Style head = ui::fg(t.graph_text, tui::AttrBold);
    p.text(x_id, in.y, unicode::pad_left("#", 4), head);
    p.text(x_title, in.y, "Title", head);
    p.text(x_diff, in.y, "Level", head);
    p.text(x_ac, in.y, "Accept", head);
    if (tags_col) p.text(x_tags, in.y, "Topics", head);

    list_rect_ = {in.x, in.y + 1, in.w, in.h - 1};
    list_height_ = list_rect_.h;
    scroll_ = std::min(scroll_, selected_);
    if (selected_ >= scroll_ + list_height_) scroll_ = selected_ - list_height_ + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(rows_.size()) - list_height_));

    if (data.problems.empty()) {
        const std::string msg =
            data.syncing
                ? std::format("{} downloading the problem list…", tui::spinner(app_.tick()))
                : (data.sync_error.empty() ? "no problems" : data.sync_error);
        ui::draw_wrapped(p, {in.x + 2, in.y + 2, in.w - 4, in.h - 2}, msg, ui::fg(t.graph_text));
        return;
    }
    if (rows_.empty()) {
        p.text(in.x + 2, in.y + 2, "no problems match", ui::fg(t.graph_text));
        return;
    }

    const std::string daily_slug = data.daily ? data.daily->problem.slug : "";
    for (int row = 0; row < list_height_; ++row) {
        const auto index = static_cast<std::size_t>(scroll_) + static_cast<std::size_t>(row);
        if (index >= rows_.size()) break;
        const auto& prob = data.problems[rows_[index]];
        const int y = list_rect_.y + row;
        const bool sel = scroll_ + row == selected_;
        const bool dim = prob.paid_only && !(data.user && data.user->premium);
        tui::Style base = ui::fg(dim ? t.inactive_fg : t.main_fg);
        if (sel) {
            base = tui::Style{.fg = t.selected_fg, .bg = t.selected_bg, .attrs = tui::AttrBold};
            p.fill({in.x, y, w, 1}, base);
        }
        const auto colored = [&](tui::Color c) { return base.with_fg(c); };
        p.text(x_status, y, ui::status_icon(prob.status),
               colored(ui::status_color(t, prob.status)));
        p.text(x_id, y, unicode::pad_left(prob.frontend_id, 4), sel ? base : ui::fg(t.graph_text));
        // Markers follow the (possibly truncated) title: today's daily, an open editor.
        const bool is_daily = prob.slug == daily_slug;
        const bool open = app_.session(prob.slug) != nullptr;
        const int suffix_w = (is_daily ? 8 : 0) + (open ? 2 : 0);
        int tx = p.text(x_title, y, prob.title, base, title_w - suffix_w - 1);
        if (is_daily) tx = p.text(tx + 1, y, "◆ daily", colored(t.warning));
        if (open) p.text(tx + 1, y, "●", colored(t.editor_box));
        if (prob.paid_only) p.text(x_paid, y, "$", colored(t.warning));
        p.text(x_diff, y, leetcode::to_string(prob.difficulty),
               colored(ui::difficulty_color(t, prob.difficulty)));
        p.text(x_ac, y, unicode::pad_left(str::percent(prob.ac_rate), 6),
               colored(ui::acceptance_color(t, prob.ac_rate)));
        if (tags_col) {
            std::string tags;
            for (const auto& tag : prob.tags) tags += (tags.empty() ? "" : ", ") + tag.name;
            p.text(x_tags, y, tags, sel ? base : ui::fg(t.graph_text), tag_w);
        }
    }
    tui::scrollbar(p, in.right() - 1, list_rect_.y, list_height_, scroll_, list_height_,
                   static_cast<int>(rows_.size()), ui::fg(t.div_line), ui::fg(t.list_box));
}

void HomeView::draw_preview(const tui::Painter& p, Rect r) {
    const auto& t = app_.theme();
    const auto* prob = selected();
    std::vector<tui::Segments> right;
    if (prob != nullptr) right.push_back(ui::label(prob->frontend_id, t.graph_text));
    const Rect in = ui::box(p, t,
                            {.rect = r,
                             .color = t.preview_box,
                             .focused = focus_preview_,
                             .rounded = app_.config().rounded_corners,
                             .titles = {ui::title(t, "preview", 2, true)},
                             .right_titles = std::move(right),
                             .hints = {ui::hint(t, "J/K", "scroll"), ui::hint(t, "⏎", "solve"),
                                       ui::hint(t, "gx", "browser")}});
    preview_rect_ = r;
    if (prob == nullptr || in.w < 10 || in.h < 3) return;
    const Rect body{in.x + 1, in.y, in.w - 2, in.h};

    // Header: title, metadata, topics.
    int y = body.y;
    p.text(body.x, y++, std::format("{}. {}", prob->frontend_id, prob->title),
           ui::fg(t.title, tui::AttrBold), body.w);
    int x = p.text(body.x, y, leetcode::to_string(prob->difficulty),
                   ui::fg(ui::difficulty_color(t, prob->difficulty), tui::AttrBold));
    x = p.text(x, y, "  " + str::percent(prob->ac_rate),
               ui::fg(ui::acceptance_color(t, prob->ac_rate)));
    const auto q = app_.data().questions.find(prob->slug);
    if (q != app_.data().questions.end()) {
        x = p.text(x, y, std::format("  ▲ {}", str::human_count(q->second->likes)),
                   ui::fg(t.graph_text));
    }
    if (prob->status != Status::Todo) {
        p.text(x + 2, y,
               std::format("{} {}", ui::status_icon(prob->status),
                           prob->status == Status::Solved ? "solved" : "attempted"),
               ui::fg(ui::status_color(t, prob->status)));
    }
    ++y;
    if (!prob->tags.empty()) {
        std::string tags;
        for (const auto& tag : prob->tags) tags += (tags.empty() ? "" : " · ") + tag.name;
        y += ui::draw_wrapped(p, {body.x, y, body.w, 2}, tags, ui::fg(t.accent));
    }
    std::string rule;
    for (int i = 0; i < body.w; ++i) rule += "─";
    p.text(body.x, y++, rule, ui::fg(t.div_line));

    const Rect doc{body.x, y, body.w - 1, body.bottom() - y}; // a column for the scrollbar
    preview_height_ = doc.h;
    if (q == app_.data().questions.end()) {
        const auto err = app_.data().question_errors.find(prob->slug);
        const std::string msg = err != app_.data().question_errors.end()
                                    ? err->second
                                    : std::format("{} loading…", tui::spinner(app_.tick()));
        ui::draw_wrapped(p, doc, msg, ui::fg(t.graph_text));
        preview_lines_ = 0;
        return;
    }
    const auto& lines = app_.description(*q->second, doc.w, false);
    preview_lines_ = static_cast<int>(lines.size());
    preview_scroll_ = std::clamp(preview_scroll_, 0, std::max(0, preview_lines_ - doc.h));
    ui::draw_document(p, doc, lines, preview_scroll_, t);
    tui::scrollbar(p, in.right() - 1, doc.y, doc.h, preview_scroll_, doc.h, preview_lines_,
                   ui::fg(t.div_line), ui::fg(t.preview_box));
}

} // namespace lv::app
