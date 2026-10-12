#pragma once

#include "app/app.hpp"

namespace lv::app {

/// Full-screen profile dashboard: solved meters, submission heatmap, languages, skills.
class StatsView : public View {
public:
    explicit StatsView(App& app) : app_(app) {}

    void draw(const tui::Painter& p, tui::Rect area) override;
    bool on_key(const tui::KeyEvent& ev) override;

private:
    void draw_profile(const tui::Painter& p, tui::Rect r, const leetcode::UserStats& s);
    void draw_solved(const tui::Painter& p, tui::Rect r, const leetcode::UserStats& s);
    void draw_calendar(const tui::Painter& p, tui::Rect r, const leetcode::UserStats& s);
    void draw_languages(const tui::Painter& p, tui::Rect r, const leetcode::UserStats& s);
    void draw_skills(const tui::Painter& p, tui::Rect r, const leetcode::UserStats& s);
    void draw_recent(const tui::Painter& p, tui::Rect r, const leetcode::UserStats& s);

    App& app_;
};

} // namespace lv::app
