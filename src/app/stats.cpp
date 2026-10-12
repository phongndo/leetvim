#include "app/stats.hpp"

#include "app/ui.hpp"
#include "util/strings.hpp"
#include "util/unicode.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>

namespace lv::app {

using leetcode::Difficulty;
using tui::Key;
using tui::Rect;

namespace {

ui::BoxSpec spec(const App& app, Rect r, tui::Color color, std::string_view name, int number = 0) {
    return {.rect = r,
            .color = color,
            .focused = true,
            .rounded = app.config().ui.rounded_corners,
            .titles = {ui::title(app.theme(), name, number)},
            .right_titles = {},
            .hints = {}};
}

} // namespace

bool StatsView::on_key(const tui::KeyEvent& ev) {
    if (ev.is('q') || ev.is(Key::Escape) || ev.is('s') || ev.is('h')) {
        app_.go_home();
    } else if (ev.is('R')) {
        app_.refresh_user();
    } else {
        return false;
    }
    return true;
}

void StatsView::draw(const tui::Painter& p, Rect area) {
    const auto& t = app_.theme();
    const auto& data = app_.data();
    if (!data.stats) {
        auto box = spec(app_, area, t.stats_box, "stats");
        box.hints = {ui::hint(t, "R", "refresh"), ui::hint(t, "q", "back")};
        const Rect in = ui::box(p, t, box);
        const std::string msg =
            data.stats_loading ? std::format("{} loading stats…", tui::spinner(app_.tick()))
            : !data.stats_error.empty()
                ? data.stats_error
                : "Sign in with :login, or set `username` in the config, to see stats.";
        ui::draw_wrapped(p, {in.x + 2, in.y + (in.h / 2), in.w - 4, 3}, msg, ui::fg(t.graph_text));
        return;
    }
    const auto& s = *data.stats;

    const int profile_h = 5;
    const int row2_h = std::clamp((area.h - profile_h) / 2, 8, 11);
    draw_profile(p, {area.x, area.y, area.w, profile_h}, s);

    const int solved_w = std::clamp(area.w / 3, 36, 52);
    draw_solved(p, {area.x, area.y + profile_h, solved_w, row2_h}, s);
    draw_calendar(p, {area.x + solved_w, area.y + profile_h, area.w - solved_w, row2_h}, s);

    const int y3 = area.y + profile_h + row2_h;
    const Rect row3{area.x, y3, area.w, area.bottom() - y3};
    if (row3.h < 4) return;
    const int lang_w = row3.w / 4;
    const int recent_w = row3.w / 3;
    draw_languages(p, {row3.x, row3.y, lang_w, row3.h}, s);
    draw_skills(p, {row3.x + lang_w, row3.y, row3.w - lang_w - recent_w, row3.h}, s);
    draw_recent(p, {row3.right() - recent_w, row3.y, recent_w, row3.h}, s);
}

void StatsView::draw_profile(const tui::Painter& p, Rect r, const leetcode::UserStats& s) {
    const auto& t = app_.theme();
    auto box = spec(app_, r, t.stats_box, "profile", 1);
    box.right_titles = {ui::hint(t, "R", "refresh"), ui::hint(t, "q", "back")};
    const Rect in = ui::box(p, t, box);
    if (in.empty()) return;
    int x = p.text(in.x + 1, in.y, s.username, ui::fg(t.title, tui::AttrBold));
    if (!s.real_name.empty()) x = p.text(x + 2, in.y, s.real_name, ui::fg(t.main_fg));
    if (const auto& user = app_.data().user; user.has_value() && user->premium) {
        p.text(x + 2, in.y, "★ premium", ui::fg(t.warning));
    }

    const auto stat = [&](int& cx, int y, std::string_view name, const std::string& value,
                          tui::Color color) {
        cx = p.text(cx, y, std::string(name) + " ", ui::fg(t.graph_text));
        cx = p.text(cx, y, value, ui::fg(color, tui::AttrBold)) + 3;
    };
    int cx = in.x + 1;
    stat(cx, in.y + 1, "rank", ui::thousands(s.ranking), t.title);
    stat(cx, in.y + 1, "reputation", ui::thousands(s.reputation), t.main_fg);
    const auto& streak = app_.data().streak;
    stat(cx, in.y + 1, "streak", std::format("{}d", streak.has_value() ? streak->count : s.streak),
         t.warning);
    stat(cx, in.y + 1, "active days", std::to_string(s.total_active_days), t.main_fg);
    if (s.total_submissions.all > 0) {
        stat(cx, in.y + 1, "acceptance",
             str::percent(100.0 * s.ac_submissions.all / s.total_submissions.all),
             ui::acceptance_color(t, 100.0 * s.ac_submissions.all / s.total_submissions.all));
    }
    cx = in.x + 1;
    if (s.contest) {
        stat(cx, in.y + 2, "contest rating", std::format("{:.0f}", s.contest->rating), t.accent);
        stat(cx, in.y + 2, "global", ui::thousands(s.contest->global_ranking), t.main_fg);
        stat(cx, in.y + 2, "top", std::format("{:.1f}%", s.contest->top_percentage), t.success);
        stat(cx, in.y + 2, "contests", std::to_string(s.contest->attended), t.main_fg);
    } else {
        p.text(cx, in.y + 2, "no contest history", ui::fg(t.inactive_fg));
        cx += 22;
    }
    if (!s.badges.empty()) stat(cx, in.y + 2, "badges", std::to_string(s.badges.size()), t.warning);
}

void StatsView::draw_solved(const tui::Painter& p, Rect r, const leetcode::UserStats& s) {
    const auto& t = app_.theme();
    const Rect in = ui::box(p, t, spec(app_, r, t.list_box, "solved", 2));
    if (in.empty()) return;
    int y = in.y;
    const auto row = [&](std::string_view name, Difficulty d, tui::Color c) {
        if (y + 1 >= in.bottom()) return;
        const int got = s.solved.get(d);
        const int of = s.questions.get(d);
        p.text(in.x + 1, y, name, ui::fg(c, tui::AttrBold));
        const std::string count = std::format("{}/{}", got, of);
        p.text(in.right() - static_cast<int>(count.size()) - 1, y, count, ui::fg(t.main_fg));
        tui::meter(p, in.x + 1, y + 1, in.w - 2, of > 0 ? static_cast<double>(got) / of : 0.0,
                   tui::shade(c, 0.4), tui::shade(c, 0.7), c, t.meter_bg);
        y += 2;
    };
    row("Easy", Difficulty::Easy, t.easy);
    row("Medium", Difficulty::Medium, t.medium);
    row("Hard", Difficulty::Hard, t.hard);
    if (y < in.bottom()) {
        int x = p.text(in.x + 1, y, "total ", ui::fg(t.graph_text));
        x = p.text(x, y, std::format("{}/{}", s.solved.all, s.questions.all),
                   ui::fg(t.title, tui::AttrBold));
        if (s.questions.all > 0) {
            p.text(x + 2, y, str::percent(100.0 * s.solved.all / s.questions.all),
                   ui::fg(t.graph_text));
        }
    }
}

void StatsView::draw_calendar(const tui::Painter& p, Rect r, const leetcode::UserStats& s) {
    const auto& t = app_.theme();
    auto box = spec(app_, r, t.preview_box, "submissions", 3);
    int total = 0;
    int peak = 0;
    for (const auto& [ts, count] : s.calendar) {
        total += count;
        peak = std::max(peak, count);
    }
    box.right_titles = {ui::label(std::format("{} in the past year", total), t.graph_text)};
    const Rect in = ui::box(p, t, box);
    if (in.w < 10 || in.h < 4) return;

    using namespace std::chrono;
    const auto today = floor<days>(system_clock::now());
    const weekday today_wd{today};
    const bool squares = in.h >= 8;
    // Two columns per week (a square and a gap) when at least half a year fits.
    const int col_w = (in.w - 5) / 2 >= 26 ? 2 : 1;
    const int weeks = std::min(53, (in.w - 5) / col_w);
    // The last column holds the current week; day 0 of the grid is a Sunday.
    const auto first = today - days{static_cast<int>(today_wd.c_encoding()) + ((weeks - 1) * 7)};
    const auto level_color = [&](sys_days day) {
        if (day > today) return tui::Color::none();
        const auto it = s.calendar.find(duration_cast<seconds>(day.time_since_epoch()).count());
        const int count = it == s.calendar.end() ? 0 : it->second;
        if (count == 0 || peak == 0) return t.heat.at(0);
        const int level = 1 + std::min(3, count * 4 / (peak + 1));
        return t.heat.at(static_cast<std::size_t>(level));
    };

    const int gx = in.x + 4;
    const int gy = in.y + 1;
    // Month labels above the first week of each month.
    unsigned last_month = 0;
    int label_end = 0;
    for (int w = 0; w < weeks; ++w) {
        const year_month_day ymd{first + days{w * 7}};
        if (static_cast<unsigned>(ymd.month()) == last_month) continue;
        last_month = static_cast<unsigned>(ymd.month());
        // The leading month is usually partial: leave it unlabelled rather than crowd out the
        // next month when that starts before a label (3 letters and a gap) would fit.
        const int label_weeks = (4 + col_w - 1) / col_w;
        if (w == 0 && year_month_day{first + days{(label_weeks - 1) * 7}}.month() != ymd.month())
            continue;
        static constexpr std::array<std::string_view, 12> names{
            "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        const int x = gx + (w * col_w);
        if (x < label_end || x + 3 > in.right()) continue; // would collide
        label_end = p.text(x, in.y, names.at(last_month - 1), ui::fg(t.graph_text)) + 1;
    }
    if (squares) {
        for (int d = 0; d < 7; ++d) {
            if (d % 2 == 1)
                p.text(
                    in.x, gy + d,
                    std::array{"", "Mon", "", "Wed", "", "Fri", ""}.at(static_cast<std::size_t>(d)),
                    ui::fg(t.graph_text));
            for (int w = 0; w < weeks; ++w) {
                const auto day = first + days{(w * 7) + d};
                const tui::Color c = level_color(day);
                if (c.is_default()) continue;
                p.put(gx + (w * col_w), gy + d, "■", ui::fg(c));
            }
        }
    } else {
        // Two days per cell: upper half-block in fg, lower half in bg.
        for (int row = 0; row < 4; ++row) {
            for (int w = 0; w < weeks; ++w) {
                const auto top = first + days{(w * 7) + (row * 2)};
                const tui::Color upper = level_color(top);
                const tui::Color lower =
                    row * 2 + 1 < 7 ? level_color(top + days{1}) : tui::Color::none();
                if (upper.is_default() && lower.is_default()) continue;
                p.put(gx + (w * col_w), gy + row, "▀", tui::Style{.fg = upper, .bg = lower});
            }
        }
    }
    // Legend.
    const int ly = in.bottom() - 1;
    if (ly > gy + (squares ? 7 : 4)) {
        int x = in.right() - 18;
        x = p.text(x, ly, "less ", ui::fg(t.graph_text));
        for (const auto& c : t.heat) x = p.text(x, ly, "■", ui::fg(c));
        p.text(x + 1, ly, "more", ui::fg(t.graph_text));
        p.text(gx, ly,
               std::format("active {}d · streak {}d · peak {}/day", s.total_active_days, s.streak,
                           peak),
               ui::fg(t.graph_text));
    }
}

void StatsView::draw_languages(const tui::Painter& p, Rect r, const leetcode::UserStats& s) {
    const auto& t = app_.theme();
    const Rect in = ui::box(p, t, spec(app_, r, t.editor_box, "languages", 4));
    if (in.empty()) return;
    auto langs = s.languages;
    std::ranges::sort(langs, [](const auto& a, const auto& b) { return a.second > b.second; });
    const int peak = langs.empty() ? 1 : std::max(1, langs.front().second);
    int name_w = 4;
    for (const auto& [name, count] : langs)
        name_w = std::max(name_w, unicode::display_width(name) + 2);
    name_w = std::min(name_w, in.w / 2);
    for (int i = 0; i < in.h && static_cast<std::size_t>(i) < langs.size(); ++i) {
        const auto& [name, count] = langs[static_cast<std::size_t>(i)];
        const int y = in.y + i;
        p.text(in.x + 1, y, name, ui::fg(t.main_fg), name_w - 1);
        const std::string n = std::to_string(count);
        const int bar_w = in.w - name_w - static_cast<int>(n.size()) - 3;
        const double frac = static_cast<double>(count) / peak;
        tui::meter(p, in.x + name_w, y, bar_w, frac, t.grad_start, t.grad_mid, t.grad_end,
                   t.meter_bg);
        p.text(in.right() - static_cast<int>(n.size()) - 1, y, n, ui::fg(t.title));
    }
}

void StatsView::draw_skills(const tui::Painter& p, Rect r, const leetcode::UserStats& s) {
    const auto& t = app_.theme();
    const Rect in = ui::box(p, t, spec(app_, r, t.result_box, "skills", 5));
    if (in.empty()) return;
    auto skills = s.skills;
    std::ranges::sort(skills, [](const auto& a, const auto& b) { return a.solved > b.solved; });
    const int peak = skills.empty() ? 1 : std::max(1, skills.front().solved);
    const int columns = in.w >= 70 ? 2 : 1;
    const int col_w = in.w / columns;
    for (std::size_t i = 0; i < skills.size(); ++i) {
        const int col = static_cast<int>(i) / in.h;
        if (col >= columns) break;
        const int y = in.y + (static_cast<int>(i) % in.h);
        const int x = in.x + 1 + (col * col_w);
        const auto& sk = skills[i];
        const tui::Color level = sk.level == "advanced"       ? t.hard
                                 : sk.level == "intermediate" ? t.medium
                                                              : t.easy;
        p.text(x, y, "•", ui::fg(level));
        p.text(x + 2, y, sk.name, ui::fg(t.main_fg), col_w - 14);
        const std::string n = std::to_string(sk.solved);
        tui::meter(p, x + col_w - 12, y, 6, static_cast<double>(sk.solved) / peak,
                   tui::shade(level, 0.5), tui::shade(level, 0.8), level, t.meter_bg);
        p.text(x + col_w - 5, y, n, ui::fg(t.title));
    }
}

void StatsView::draw_recent(const tui::Painter& p, Rect r, const leetcode::UserStats& s) {
    const auto& t = app_.theme();
    const Rect in = ui::box(p, t, spec(app_, r, t.stats_box, "recent accepted", 6));
    if (in.empty()) return;
    for (int i = 0; i < in.h && static_cast<std::size_t>(i) < s.recent_accepted.size(); ++i) {
        const auto& sub = s.recent_accepted[static_cast<std::size_t>(i)];
        const int y = in.y + i;
        const std::string when = ui::time_ago(sub.timestamp);
        const int when_w = unicode::display_width(when);
        const tui::Color color = [&] {
            if (const auto* prob = app_.problem(sub.slug))
                return ui::difficulty_color(t, prob->difficulty);
            return t.main_fg;
        }();
        p.text(in.x + 1, y, "✓", ui::fg(t.solved));
        p.text(in.x + 3, y, sub.title, ui::fg(color), in.w - when_w - 6);
        p.text(in.right() - when_w - 1, y, when, ui::fg(t.graph_text));
    }
}

} // namespace lv::app
