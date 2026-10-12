#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace lv::unicode {

inline constexpr char32_t replacement_char = 0xFFFD;

/// Decodes the code point at `pos` and advances `pos` past it.
/// Malformed sequences decode to U+FFFD and advance by one byte.
char32_t decode(std::string_view s, std::size_t& pos);

void append_utf8(std::string& out, char32_t cp);
std::string to_utf8(char32_t cp);

/// Terminal columns used by a single code point: 0 (combining / zero width / control), 1, or 2.
int codepoint_width(char32_t cp);

/// A user-perceived character: a base code point plus any combining marks, variation
/// selectors, skin-tone modifiers or ZWJ-joined code points that follow it.
struct Grapheme {
    std::string_view bytes;
    int width = 0;
};

/// Returns the grapheme cluster starting at `pos` and advances `pos` past it.
Grapheme next_grapheme(std::string_view s, std::size_t& pos);

/// Number of terminal columns `s` occupies.
int display_width(std::string_view s);

/// Cuts `s` to at most `max_width` columns. When cut, the result ends with `ellipsis`
/// (which counts toward the width).
std::string truncate(std::string_view s, int max_width, std::string_view ellipsis = "…");

/// Truncates or right-pads with spaces to exactly `width` columns.
std::string pad_right(std::string_view s, int width);

/// Truncates or left-pads with spaces to exactly `width` columns.
std::string pad_left(std::string_view s, int width);

} // namespace lv::unicode
