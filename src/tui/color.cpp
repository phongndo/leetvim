#include "tui/color.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>

namespace lv::tui {

namespace {

std::optional<std::uint32_t> parse_hex(std::string_view digits) {
    std::uint32_t value = 0;
    const char* first = digits.data();
    const char* end = first + digits.size();
    const auto [ptr, ec] = std::from_chars(first, end, value, 16);
    if (ec != std::errc{} || ptr != end) return std::nullopt;
    return value;
}

std::uint8_t mix(std::uint8_t a, std::uint8_t b, double t) {
    return static_cast<std::uint8_t>(std::lround(a + ((b - a) * t)));
}

constexpr std::array<std::uint8_t, 6> cube_levels{0, 95, 135, 175, 215, 255};

int nearest_cube_index(std::uint8_t v) {
    int best = 0;
    for (int i = 1; i < 6; ++i) {
        if (std::abs(cube_levels.at(i) - v) < std::abs(cube_levels.at(best) - v)) best = i;
    }
    return best;
}

int distance(int r1, int g1, int b1, int r2, int g2, int b2) {
    return ((r1 - r2) * (r1 - r2)) + ((g1 - g2) * (g1 - g2)) + ((b1 - b2) * (b1 - b2));
}

} // namespace

std::optional<Color> Color::parse(std::string_view text) {
    if (text.empty() || text == "default" || text == "none") return Color::none();
    if (text.front() == '#') {
        const auto digits = text.substr(1);
        const auto value = parse_hex(digits);
        if (!value) return std::nullopt;
        switch (digits.size()) {
        case 2: {
            const auto v = static_cast<std::uint8_t>(*value);
            return rgb(v, v, v);
        }
        case 3:
            return rgb(static_cast<std::uint8_t>(((*value >> 8U) & 0xFU) * 17),
                       static_cast<std::uint8_t>(((*value >> 4U) & 0xFU) * 17),
                       static_cast<std::uint8_t>((*value & 0xFU) * 17));
        case 6: return hex(*value);
        default: return std::nullopt;
        }
    }
    int index = 0;
    const char* first = text.data();
    const char* end = first + text.size();
    const auto [ptr, ec] = std::from_chars(first, end, index);
    if (ec != std::errc{} || ptr != end || index < 0 || index > 255) return std::nullopt;
    return indexed(static_cast<std::uint8_t>(index));
}

Color lerp(Color a, Color b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    if (a.kind != Color::Kind::Rgb || b.kind != Color::Kind::Rgb) return t < 0.5 ? a : b;
    return Color::rgb(mix(a.r, b.r, t), mix(a.g, b.g, t), mix(a.b, b.b, t));
}

Color gradient(Color start, Color mid, Color end, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t < 0.5 ? lerp(start, mid, t * 2) : lerp(mid, end, (t - 0.5) * 2);
}

Color shade(Color c, double factor) {
    if (c.kind != Color::Kind::Rgb) return c;
    if (factor <= 1.0) {
        return Color::rgb(mix(0, c.r, factor), mix(0, c.g, factor), mix(0, c.b, factor));
    }
    const double t = std::min(factor - 1.0, 1.0);
    return Color::rgb(mix(c.r, 255, t), mix(c.g, 255, t), mix(c.b, 255, t));
}

std::uint8_t to_xterm256(Color c) {
    if (c.kind == Color::Kind::Indexed) return c.index;
    if (c.kind == Color::Kind::Default) return 7;
    const int ri = nearest_cube_index(c.r);
    const int gi = nearest_cube_index(c.g);
    const int bi = nearest_cube_index(c.b);
    const int cube = 16 + (36 * ri) + (6 * gi) + bi;
    const int cube_dist =
        distance(c.r, c.g, c.b, cube_levels.at(ri), cube_levels.at(gi), cube_levels.at(bi));
    const int avg = (c.r + c.g + c.b) / 3;
    const int gray_index = std::clamp((avg - 8) / 10, 0, 23);
    const int gray = 8 + (gray_index * 10);
    const int gray_dist = distance(c.r, c.g, c.b, gray, gray, gray);
    return static_cast<std::uint8_t>(gray_dist < cube_dist ? 232 + gray_index : cube);
}

} // namespace lv::tui
