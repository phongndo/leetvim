#pragma once

#include "app/app.hpp"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace lv::app {

/// Single-line text editing with readline-style keys.
class LineEdit {
public:
    /// Returns true if the key was an editing key (consumed).
    bool on_key(const tui::KeyEvent& ev);
    void insert(std::string_view text);
    void set(std::string text);
    [[nodiscard]] const std::string& text() const { return text_; }
    /// Draws the text in `width` columns and returns the cursor column.
    int draw(const tui::Painter& p, int x, int y, int width, const tui::Style& style,
             bool masked = false) const;

private:
    std::string text_;
    std::size_t cursor_ = 0; // byte offset
};

/// Centered box asking for one line of text.
class PromptOverlay : public Overlay {
public:
    using Submit = std::function<void(const std::string&)>;
    PromptOverlay(App& app, std::string title, std::vector<std::string> message, Submit on_submit,
                  bool masked = false, std::string initial = {});

    void draw(const tui::Painter& p, tui::Rect area) override;
    bool on_key(const tui::KeyEvent& ev) override;
    bool on_paste(const std::string& text) override;
    [[nodiscard]] std::optional<tui::Cursor> cursor() const override { return cursor_; }

private:
    App& app_;
    std::string title_;
    std::vector<std::string> message_;
    Submit on_submit_;
    bool masked_;
    LineEdit edit_;
    std::optional<tui::Cursor> cursor_;
};

/// ':' command line along the bottom of the screen.
class CommandOverlay : public Overlay {
public:
    explicit CommandOverlay(App& app) : app_(app) {}
    void draw(const tui::Painter& p, tui::Rect area) override;
    bool on_key(const tui::KeyEvent& ev) override;
    bool on_paste(const std::string& text) override;
    [[nodiscard]] std::optional<tui::Cursor> cursor() const override { return cursor_; }

private:
    App& app_;
    LineEdit edit_;
    std::optional<tui::Cursor> cursor_;
};

/// A choice of single-key actions ("[w] write all and quit").
class ConfirmOverlay : public Overlay {
public:
    struct Choice {
        char32_t key;
        std::string label;
        std::function<void()> action;
    };
    ConfirmOverlay(App& app, std::string title, std::vector<std::string> message,
                   std::vector<Choice> choices);
    void draw(const tui::Painter& p, tui::Rect area) override;
    bool on_key(const tui::KeyEvent& ev) override;

private:
    App& app_;
    std::string title_;
    std::vector<std::string> message_;
    std::vector<Choice> choices_;
};

/// Fuzzy-filterable list. Single-select calls on_pick; multi-select toggles with Space and
/// calls on_done with the checked values on Enter.
class PickerOverlay : public Overlay {
public:
    struct Item {
        std::string label;
        std::string detail; // right-aligned, dimmed
        std::string value;
        tui::Color color{};
    };
    struct Callbacks {
        std::function<void(const std::string&)> on_pick;           // single select
        std::function<void(const std::set<std::string>&)> on_done; // multi select
        std::function<void(const std::string&)> on_highlight;      // live preview
        std::function<void()> on_cancel;
    };
    PickerOverlay(App& app, std::string title, std::vector<Item> items, Callbacks callbacks,
                  bool multi = false, std::set<std::string> checked = {},
                  const std::string& initial = {});

    void draw(const tui::Painter& p, tui::Rect area) override;
    bool on_key(const tui::KeyEvent& ev) override;
    bool on_paste(const std::string& text) override;
    [[nodiscard]] std::optional<tui::Cursor> cursor() const override { return cursor_; }

private:
    void refilter();
    void highlight();

    App& app_;
    std::string title_;
    std::vector<Item> items_;
    Callbacks callbacks_;
    bool multi_;
    std::set<std::string> checked_;
    LineEdit filter_;
    std::vector<std::size_t> visible_;
    int selected_ = 0;
    int scroll_ = 0;
    std::optional<tui::Cursor> cursor_;
};

/// Key reference.
class HelpOverlay : public Overlay {
public:
    explicit HelpOverlay(App& app) : app_(app) {}
    void draw(const tui::Painter& p, tui::Rect area) override;
    bool on_key(const tui::KeyEvent& ev) override;

private:
    App& app_;
    int scroll_ = 0;
    int max_scroll_ = 0;
};

/// Centers a w x h box inside `area`.
tui::Rect centered(tui::Rect area, int w, int h);

} // namespace lv::app
