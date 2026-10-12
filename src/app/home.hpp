#pragma once

#include "app/app.hpp"
#include "app/overlays.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lv::app {

struct Filter {
    std::array<bool, 3> difficulty{}; // easy, medium, hard; none set = all
    std::array<bool, 3> status{};     // todo, attempted, solved; none set = all
    bool hide_paid = false;
    std::vector<std::string> tags; // topic slugs; a problem must have all of them

    [[nodiscard]] bool active() const;
    [[nodiscard]] bool matches(const leetcode::ProblemSummary& p) const;
    /// Short summary for the box title, e.g. "easy medium · todo · #dp".
    [[nodiscard]] std::string describe() const;
};

enum class SortKey : std::uint8_t { Id, Difficulty, Acceptance, Title, Status };

/// The dashboard: stats box, problem list and preview.
class HomeView : public View {
public:
    explicit HomeView(App& app);

    void draw(const tui::Painter& p, tui::Rect area) override;
    bool on_key(const tui::KeyEvent& ev) override;
    bool on_mouse(const tui::MouseEvent& ev) override;
    bool on_paste(const std::string& text) override;
    bool on_tick() override;
    [[nodiscard]] std::optional<tui::Cursor> cursor() const override { return cursor_; }

    Filter& filter() { return filter_; }
    void refilter() { built_version_ = UINT64_MAX; }
    void pick_random();
    void select_slug(const std::string& slug);
    /// Applies a changed [list] option, e.g. "sort".
    void apply_option(std::string_view name);

private:
    void rebuild();
    void move(int delta);
    void select(int row);
    [[nodiscard]] const leetcode::ProblemSummary* selected() const;
    void open_selected();
    void show_filter();
    void show_topics();

    void draw_stats(const tui::Painter& p, tui::Rect r);
    void draw_list(const tui::Painter& p, tui::Rect r);
    void draw_preview(const tui::Painter& p, tui::Rect r);

    App& app_;
    Filter filter_;
    SortKey sort_ = SortKey::Id;
    bool reverse_ = false;
    std::vector<std::size_t> rows_;
    std::uint64_t built_version_ = UINT64_MAX;

    int selected_ = 0;
    int scroll_ = 0;
    int list_height_ = 1;
    std::string selected_slug_;
    Clock::time_point selected_at_;

    bool searching_ = false;
    LineEdit search_;
    std::string applied_search_;

    bool focus_preview_ = false;
    int preview_scroll_ = 0;
    int preview_height_ = 1;
    int preview_lines_ = 0;
    bool show_stats_ = true;
    bool show_preview_ = true;
    bool pending_g_ = false;

    tui::Rect list_rect_;
    tui::Rect preview_rect_;
    int last_click_row_ = -1;
    Clock::time_point last_click_;
    std::optional<tui::Cursor> cursor_;
};

} // namespace lv::app
