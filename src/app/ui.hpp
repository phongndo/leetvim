#pragma once

#include "content/layout.hpp"
#include "leetcode/models.hpp"
#include "tui/painter.hpp"
#include "tui/theme.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Shared drawing vocabulary for the views: btop-style boxes, labels and hints.
namespace lv::app::ui {

inline tui::Style fg(tui::Color c, std::uint8_t attrs = tui::AttrNone) {
    return tui::Style{.fg = c, .bg = {}, .attrs = attrs};
}

tui::Color difficulty_color(const tui::Theme& t, leetcode::Difficulty d);
tui::Color status_color(const tui::Theme& t, leetcode::Status s);
std::string_view status_icon(leetcode::Status s);
/// Acceptance rate colored from red (hard to pass) to green.
tui::Color acceptance_color(const tui::Theme& t, double percent);

/// "¹" "²"... for box numbers like btop's "¹cpu".
std::string superscript(int n);

/// Box title: optional superscript hotkey number + name; dimmed when inactive.
tui::Segments title(const tui::Theme& t, std::string_view name, int number = 0, bool active = true);
/// Bottom-border hint. Like btop, a letter key found in the word is highlighted in place
/// ("filter"); other keys are drawn before the word ("/ search").
tui::Segments hint(const tui::Theme& t, std::string_view key, std::string_view action);
/// Plain colored label.
tui::Segments label(std::string_view text, tui::Color color, std::uint8_t attrs = tui::AttrNone);

/// A titled box. Returns the inner rectangle.
struct BoxSpec {
    tui::Rect rect;
    tui::Color color;
    bool focused = true;
    bool rounded = true;
    std::vector<tui::Segments> titles;       // left-aligned top labels
    std::vector<tui::Segments> right_titles; // right-aligned top labels
    std::vector<tui::Segments> hints;        // bottom labels
};
tui::Rect box(const tui::Painter& p, const tui::Theme& t, const BoxSpec& spec);

/// Draws laid-out document lines from `scroll`, filling `r`.
void draw_document(const tui::Painter& p, tui::Rect r, const std::vector<content::Line>& lines,
                   int scroll, const tui::Theme& t);

/// Draws text wrapped to the width of `r`, starting at row `scroll`; returns rows used.
int draw_wrapped(const tui::Painter& p, tui::Rect r, std::string_view text, tui::Style style,
                 int scroll = 0);
/// Word-wraps text to `width` columns, keeping explicit newlines; overlong words are split.
std::vector<std::string> wrap(std::string_view text, int width);

/// 103905 -> "103,905".
std::string thousands(std::int64_t n);

/// "3m ago", "2h ago", "5d ago", or a date.
std::string time_ago(std::int64_t unix_seconds);

} // namespace lv::app::ui
