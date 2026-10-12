#pragma once

#include "app/app.hpp"

#include <vector>

namespace lv::app {

/// The two-pane solving screen: description (+ results panel) beside the editor.
class SolveView : public View {
public:
    enum class Focus : std::uint8_t { Description, Editor, Panel, TestEditor };

    explicit SolveView(App& app) : app_(app) {}

    void draw(const tui::Painter& p, tui::Rect area) override;
    bool on_key(const tui::KeyEvent& ev) override;
    bool on_mouse(const tui::MouseEvent& ev) override;
    bool on_paste(const std::string& text) override;
    [[nodiscard]] std::optional<tui::Cursor> cursor() const override;

    void set_focus(Focus f);
    [[nodiscard]] Focus focus() const { return focus_; }

    struct Layout {
        tui::Rect description;
        tui::Rect panel;
        tui::Rect editor;
    };
    /// Box rectangles for a session on a screen of `area`.
    [[nodiscard]] Layout layout(tui::Rect area, const Session& s) const;

private:
    Session* session() const;
    void handle_prefix(const tui::KeyEvent& ev, Session& s);
    bool handle_description(const tui::KeyEvent& ev, Session& s);
    bool handle_panel(const tui::KeyEvent& ev, Session& s);
    bool handle_common(const tui::KeyEvent& ev, Session& s);
    void scroll_description(Session& s, int delta) const;
    void select_case(Session& s, int index) const;

    void draw_description(const tui::Painter& p, tui::Rect r, Session& s);
    void draw_editor(const tui::Painter& p, tui::Rect r, Session& s);
    void draw_panel(const tui::Painter& p, tui::Rect r, Session& s);
    void draw_result(const tui::Painter& p, tui::Rect r, Session& s);
    void draw_testcases(const tui::Painter& p, tui::Rect r, Session& s);
    void draw_submissions(const tui::Painter& p, tui::Rect r, Session& s);

    App& app_;
    Focus focus_ = Focus::Editor;
    bool prefix_pending_ = false;
    bool pending_g_ = false;
    Layout last_;
    tui::Rect editor_inner_;
    tui::Rect panel_inner_;
    int desc_height_ = 1;
    int desc_lines_ = 0;
    int panel_height_ = 1;
    int panel_lines_ = 0;
    int case_count_ = 0;
};

} // namespace lv::app
