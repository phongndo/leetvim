#pragma once

#include "tui/color.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lv::tui {

enum Attr : std::uint8_t {
    AttrNone = 0,
    AttrBold = 1U << 0U,
    AttrDim = 1U << 1U,
    AttrItalic = 1U << 2U,
    AttrUnderline = 1U << 3U,
    AttrBlink = 1U << 4U,
    AttrReverse = 1U << 5U,
    AttrStrike = 1U << 6U,
};

struct Style {
    Color fg;
    Color bg;
    std::uint8_t attrs = AttrNone;

    [[nodiscard]] Style with_fg(Color c) const {
        Style s = *this;
        s.fg = c;
        return s;
    }
    [[nodiscard]] Style with_bg(Color c) const {
        Style s = *this;
        s.bg = c;
        return s;
    }
    [[nodiscard]] Style with(std::uint8_t extra) const {
        Style s = *this;
        s.attrs |= extra;
        return s;
    }
    bool operator==(const Style&) const = default;
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    [[nodiscard]] int right() const { return x + w; }
    [[nodiscard]] int bottom() const { return y + h; }
    [[nodiscard]] bool empty() const { return w <= 0 || h <= 0; }
    [[nodiscard]] bool contains(int px, int py) const {
        return px >= x && px < right() && py >= y && py < bottom();
    }
    [[nodiscard]] Rect inset(int d) const { return {x + d, y + d, w - (2 * d), h - (2 * d)}; }
    [[nodiscard]] Rect intersect(const Rect& o) const {
        const int nx = std::max(x, o.x);
        const int ny = std::max(y, o.y);
        return {nx, ny, std::max(0, std::min(right(), o.right()) - nx),
                std::max(0, std::min(bottom(), o.bottom()) - ny)};
    }
    bool operator==(const Rect&) const = default;
};

/// One terminal cell. A wide grapheme occupies its cell (width 2) plus a continuation cell
/// (width 0, empty text) to the right.
struct Cell {
    std::string text = " ";
    Style style;
    std::uint8_t width = 1;

    bool operator==(const Cell&) const = default;
};

/// A grid of cells that frames are drawn into.
class Canvas {
public:
    Canvas() = default;
    Canvas(int width, int height) { resize(width, height); }

    void resize(int width, int height);
    void clear(const Style& style = {});

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] Rect bounds() const { return {0, 0, width_, height_}; }
    [[nodiscard]] bool in_bounds(int x, int y) const {
        return x >= 0 && y >= 0 && x < width_ && y < height_;
    }

    [[nodiscard]] const Cell& at(int x, int y) const { return cells_[index(x, y)]; }
    Cell& at(int x, int y) { return cells_[index(x, y)]; }

    /// Writes one grapheme of the given width (1 or 2), repairing any wide character it
    /// partially overwrites. A wide grapheme that doesn't fit becomes a space. A default
    /// background in `style` keeps the cell's current background.
    void put(int x, int y, std::string_view grapheme, int width, const Style& style);
    /// Changes only the style of the cell at (x, y).
    void restyle(int x, int y, const Style& style);

private:
    [[nodiscard]] std::size_t index(int x, int y) const {
        return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_))
               + static_cast<std::size_t>(x);
    }

    int width_ = 0;
    int height_ = 0;
    std::vector<Cell> cells_;
};

} // namespace lv::tui
