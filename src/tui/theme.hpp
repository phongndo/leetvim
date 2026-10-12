#pragma once

#include "tui/color.hpp"

#include <array>
#include <span>
#include <string_view>

namespace lv::tui {

/// Semantic colors, in the spirit of btop themes.
struct Theme {
    std::string_view name;
    Color main_bg; // only painted when the theme background is enabled
    Color main_fg;
    Color title;
    Color hi_fg; // hotkey letters in labels
    Color selected_bg;
    Color selected_fg;
    Color inactive_fg;
    Color graph_text;
    Color meter_bg;
    Color div_line;
    Color stats_box;
    Color list_box;
    Color preview_box;
    Color editor_box;
    Color result_box;
    Color easy;
    Color medium;
    Color hard;
    Color solved;
    Color attempted;
    Color accent;  // inline code and highlights
    Color code_fg; // examples and code blocks
    Color link;
    Color success;
    Color error;
    Color warning;
    Color grad_start; // activity graph / meters
    Color grad_mid;
    Color grad_end;
    std::array<Color, 5> heat; // calendar intensity, none -> most
};

std::span<const Theme> builtin_themes();
/// Theme by name; falls back to the default theme.
const Theme& theme_named(std::string_view name);

} // namespace lv::tui
