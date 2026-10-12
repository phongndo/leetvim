#include "util/unicode.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace lv::unicode {

namespace {

struct Range {
    char32_t first;
    char32_t last;
};

// Zero-width code points: combining marks, format characters, variation selectors.
constexpr std::array zero_width = std::to_array<Range>({
    {0x0300, 0x036F},   {0x0483, 0x0489},   {0x0591, 0x05BD},   {0x05BF, 0x05BF},
    {0x05C1, 0x05C2},   {0x05C4, 0x05C5},   {0x05C7, 0x05C7},   {0x0610, 0x061A},
    {0x061C, 0x061C},   {0x064B, 0x065F},   {0x0670, 0x0670},   {0x06D6, 0x06DC},
    {0x06DF, 0x06E4},   {0x06E7, 0x06E8},   {0x06EA, 0x06ED},   {0x0711, 0x0711},
    {0x0730, 0x074A},   {0x07A6, 0x07B0},   {0x07EB, 0x07F3},   {0x0816, 0x082D},
    {0x0859, 0x085B},   {0x08D3, 0x0902},   {0x093A, 0x093A},   {0x093C, 0x093C},
    {0x0941, 0x0948},   {0x094D, 0x094D},   {0x0951, 0x0957},   {0x0962, 0x0963},
    {0x0981, 0x0981},   {0x09BC, 0x09BC},   {0x09C1, 0x09C4},   {0x09CD, 0x09CD},
    {0x0A01, 0x0A02},   {0x0A3C, 0x0A3C},   {0x0A41, 0x0A51},   {0x0A70, 0x0A71},
    {0x0A81, 0x0A82},   {0x0ABC, 0x0ABC},   {0x0AC1, 0x0AC8},   {0x0ACD, 0x0ACD},
    {0x0B01, 0x0B01},   {0x0B3C, 0x0B3C},   {0x0B3F, 0x0B3F},   {0x0B41, 0x0B44},
    {0x0B4D, 0x0B4D},   {0x0BC0, 0x0BC0},   {0x0BCD, 0x0BCD},   {0x0C3E, 0x0C40},
    {0x0C46, 0x0C56},   {0x0CBC, 0x0CBC},   {0x0CCC, 0x0CCD},   {0x0D41, 0x0D44},
    {0x0D4D, 0x0D4D},   {0x0DCA, 0x0DCA},   {0x0DD2, 0x0DD6},   {0x0E31, 0x0E31},
    {0x0E34, 0x0E3A},   {0x0E47, 0x0E4E},   {0x0EB1, 0x0EB1},   {0x0EB4, 0x0EBC},
    {0x0EC8, 0x0ECD},   {0x0F18, 0x0F19},   {0x0F35, 0x0F39},   {0x0F71, 0x0F84},
    {0x1000, 0x1000},   {0x102D, 0x1030},   {0x1032, 0x1037},   {0x1039, 0x103A},
    {0x1160, 0x11FF},   {0x135D, 0x135F},   {0x1712, 0x1714},   {0x17B4, 0x17B5},
    {0x17B7, 0x17BD},   {0x17C6, 0x17C6},   {0x17C9, 0x17D3},   {0x180B, 0x180F},
    {0x1AB0, 0x1AFF},   {0x1DC0, 0x1DFF},   {0x200B, 0x200F},   {0x2028, 0x202E},
    {0x2060, 0x206F},   {0x20D0, 0x20FF},   {0x2CEF, 0x2CF1},   {0x2DE0, 0x2DFF},
    {0x302A, 0x302D},   {0x3099, 0x309A},   {0xA66F, 0xA672},   {0xA674, 0xA67D},
    {0xA69E, 0xA69F},   {0xA6F0, 0xA6F1},   {0xA8E0, 0xA8F1},   {0xFE00, 0xFE0F},
    {0xFE20, 0xFE2F},   {0xFEFF, 0xFEFF},   {0xFFF9, 0xFFFB},   {0x1D167, 0x1D169},
    {0x1D173, 0x1D182}, {0x1D185, 0x1D18B}, {0x1D1AA, 0x1D1AD}, {0x1F3FB, 0x1F3FF},
    {0xE0000, 0xE0FFF},
});

// East Asian Wide/Fullwidth ranges plus emoji with default emoji presentation.
constexpr std::array wide = std::to_array<Range>({
    {0x1100, 0x115F},   {0x231A, 0x231B},   {0x2329, 0x232A},   {0x23E9, 0x23EC},
    {0x23F0, 0x23F0},   {0x23F3, 0x23F3},   {0x25FD, 0x25FE},   {0x2614, 0x2615},
    {0x2648, 0x2653},   {0x267F, 0x267F},   {0x2693, 0x2693},   {0x26A1, 0x26A1},
    {0x26AA, 0x26AB},   {0x26BD, 0x26BE},   {0x26C4, 0x26C5},   {0x26CE, 0x26CE},
    {0x26D4, 0x26D4},   {0x26EA, 0x26EA},   {0x26F2, 0x26F3},   {0x26F5, 0x26F5},
    {0x26FA, 0x26FA},   {0x26FD, 0x26FD},   {0x2705, 0x2705},   {0x270A, 0x270B},
    {0x2728, 0x2728},   {0x274C, 0x274C},   {0x274E, 0x274E},   {0x2753, 0x2755},
    {0x2757, 0x2757},   {0x2795, 0x2797},   {0x27B0, 0x27B0},   {0x27BF, 0x27BF},
    {0x2B1B, 0x2B1C},   {0x2B50, 0x2B50},   {0x2B55, 0x2B55},   {0x2E80, 0x303E},
    {0x3041, 0x33FF},   {0x3400, 0x4DBF},   {0x4E00, 0x9FFF},   {0xA000, 0xA4CF},
    {0xA960, 0xA97F},   {0xAC00, 0xD7A3},   {0xF900, 0xFAFF},   {0xFE10, 0xFE19},
    {0xFE30, 0xFE6F},   {0xFF00, 0xFF60},   {0xFFE0, 0xFFE6},   {0x16FE0, 0x16FE4},
    {0x17000, 0x187F7}, {0x18800, 0x18CD5}, {0x1B000, 0x1B2FB}, {0x1F004, 0x1F004},
    {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A}, {0x1F200, 0x1F202},
    {0x1F210, 0x1F23B}, {0x1F240, 0x1F248}, {0x1F250, 0x1F251}, {0x1F260, 0x1F265},
    {0x1F300, 0x1F320}, {0x1F32D, 0x1F335}, {0x1F337, 0x1F37C}, {0x1F37E, 0x1F393},
    {0x1F3A0, 0x1F3CA}, {0x1F3CF, 0x1F3D3}, {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4},
    {0x1F3F8, 0x1F43E}, {0x1F440, 0x1F440}, {0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D},
    {0x1F54B, 0x1F54E}, {0x1F550, 0x1F567}, {0x1F57A, 0x1F57A}, {0x1F595, 0x1F596},
    {0x1F5A4, 0x1F5A4}, {0x1F5FB, 0x1F64F}, {0x1F680, 0x1F6C5}, {0x1F6CC, 0x1F6CC},
    {0x1F6D0, 0x1F6D2}, {0x1F6D5, 0x1F6D7}, {0x1F6DC, 0x1F6DF}, {0x1F6EB, 0x1F6EC},
    {0x1F6F4, 0x1F6FC}, {0x1F7E0, 0x1F7EB}, {0x1F7F0, 0x1F7F0}, {0x1F90C, 0x1F93A},
    {0x1F93C, 0x1F945}, {0x1F947, 0x1F9FF}, {0x1FA70, 0x1FA7C}, {0x1FA80, 0x1FA89},
    {0x1FA8F, 0x1FAC6}, {0x1FACE, 0x1FADC}, {0x1FADF, 0x1FAE9}, {0x1FAF0, 0x1FAF8},
    {0x20000, 0x2FFFD}, {0x30000, 0x3FFFD},
});

template <std::size_t N>
bool in_table(const std::array<Range, N>& table, char32_t cp) {
    if (cp < table.front().first || cp > table.back().last) return false;
    const auto* it = std::upper_bound(table.begin(), table.end(), cp,
                                      [](char32_t c, const Range& r) { return c < r.first; });
    if (it == table.begin()) return false;
    return cp <= std::prev(it)->last;
}

constexpr char32_t zwj = 0x200D;
constexpr char32_t emoji_presentation = 0xFE0F;

bool is_regional_indicator(char32_t cp) { return cp >= 0x1F1E6 && cp <= 0x1F1FF; }

} // namespace

char32_t decode(std::string_view s, std::size_t& pos) {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(s[i]); };
    const unsigned char lead = byte(pos);
    if (lead < 0x80) {
        ++pos;
        return lead;
    }
    int extra = 0;
    char32_t cp = 0;
    if ((lead & 0xE0) == 0xC0) {
        extra = 1;
        cp = lead & 0x1Fu;
    } else if ((lead & 0xF0) == 0xE0) {
        extra = 2;
        cp = lead & 0x0Fu;
    } else if ((lead & 0xF8) == 0xF0) {
        extra = 3;
        cp = lead & 0x07u;
    } else {
        ++pos;
        return replacement_char;
    }
    if (pos + static_cast<std::size_t>(extra) >= s.size()) {
        ++pos;
        return replacement_char;
    }
    for (int i = 1; i <= extra; ++i) {
        const unsigned char c = byte(pos + static_cast<std::size_t>(i));
        if ((c & 0xC0) != 0x80) {
            ++pos;
            return replacement_char;
        }
        cp = (cp << 6U) | (c & 0x3Fu);
    }
    static constexpr std::array<char32_t, 4> min_value{0, 0x80, 0x800, 0x10000};
    pos += static_cast<std::size_t>(extra) + 1;
    if (cp < min_value[static_cast<std::size_t>(extra)] || cp > 0x10FFFF
        || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return replacement_char;
    }
    return cp;
}

void append_utf8(std::string& out, char32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = replacement_char;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6U)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3FU)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12U)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18U)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3FU)));
    }
}

std::string to_utf8(char32_t cp) {
    std::string out;
    append_utf8(out, cp);
    return out;
}

int codepoint_width(char32_t cp) {
    if (cp == 0) return 0;
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 0;
    if (cp < 0x300) return 1;
    if (in_table(zero_width, cp)) return 0;
    if (in_table(wide, cp)) return 2;
    return 1;
}

Grapheme next_grapheme(std::string_view s, std::size_t& pos) {
    const std::size_t start = pos;
    const char32_t base = decode(s, pos);
    int width = codepoint_width(base);
    const bool regional = is_regional_indicator(base);
    bool joined = false;
    while (pos < s.size()) {
        std::size_t peek = pos;
        const char32_t cp = decode(s, peek);
        if (cp == zwj) {
            // Absorb the joiner and the code point it joins.
            pos = peek;
            if (pos < s.size()) decode(s, pos);
            joined = true;
            continue;
        }
        if (cp == emoji_presentation) {
            width = 2;
            pos = peek;
            continue;
        }
        if (regional && is_regional_indicator(cp) && pos - start == 4) {
            // A pair of regional indicators renders as one flag.
            width = 2;
            pos = peek;
            continue;
        }
        if (codepoint_width(cp) == 0 && cp >= 0x300) {
            pos = peek;
            continue;
        }
        break;
    }
    if (joined && width < 2) width = 2;
    return {.bytes = s.substr(start, pos - start), .width = width};
}

int display_width(std::string_view s) {
    int width = 0;
    std::size_t pos = 0;
    while (pos < s.size()) width += next_grapheme(s, pos).width;
    return width;
}

std::string truncate(std::string_view s, int max_width, std::string_view ellipsis) {
    if (max_width <= 0) return {};
    if (display_width(s) <= max_width) return std::string(s);
    const int ellipsis_width = display_width(ellipsis);
    if (ellipsis_width > max_width) ellipsis = {};
    const int budget = max_width - (ellipsis.empty() ? 0 : ellipsis_width);
    std::string out;
    int used = 0;
    std::size_t pos = 0;
    while (pos < s.size()) {
        const Grapheme g = next_grapheme(s, pos);
        if (used + g.width > budget) break;
        out.append(g.bytes);
        used += g.width;
    }
    out.append(ellipsis);
    return out;
}

std::string pad_right(std::string_view s, int width) {
    std::string out = truncate(s, width);
    const int w = display_width(out);
    if (w < width) out.append(static_cast<std::size_t>(width - w), ' ');
    return out;
}

std::string pad_left(std::string_view s, int width) {
    std::string cut = truncate(s, width);
    const int w = display_width(cut);
    if (w >= width) return cut;
    return std::string(static_cast<std::size_t>(width - w), ' ') + cut;
}

} // namespace lv::unicode
