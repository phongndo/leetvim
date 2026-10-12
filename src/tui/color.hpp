#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace lv::tui {

struct Color {
    enum class Kind : std::uint8_t { Default, Indexed, Rgb };

    Kind kind = Kind::Default;
    std::uint8_t index = 0;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    static constexpr Color none() { return {}; }
    static constexpr Color indexed(std::uint8_t i) {
        return {.kind = Kind::Indexed, .index = i, .r = 0, .g = 0, .b = 0};
    }
    static constexpr Color rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
        return {.kind = Kind::Rgb, .index = 0, .r = red, .g = green, .b = blue};
    }
    static constexpr Color hex(std::uint32_t value) {
        return rgb(static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
                   static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
                   static_cast<std::uint8_t>(value & 0xFFU));
    }
    /// Parses "#rrggbb", "#rgb", "#gg" (grayscale, as in btop themes), "default", or a
    /// 0-255 palette index.
    static std::optional<Color> parse(std::string_view text);

    [[nodiscard]] constexpr bool is_default() const { return kind == Kind::Default; }
    bool operator==(const Color&) const = default;
};

/// Linear interpolation in RGB. Non-RGB colors snap to whichever end is closer.
Color lerp(Color a, Color b, double t);
/// Three-stop gradient, t in [0, 1].
Color gradient(Color start, Color mid, Color end, double t);
/// Scales an RGB color toward black (factor < 1) or white (factor > 1).
Color shade(Color c, double factor);
/// Nearest xterm-256 palette entry for an RGB color.
std::uint8_t to_xterm256(Color c);

} // namespace lv::tui
