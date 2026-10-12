#pragma once

#include "tui/canvas.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lv::tui {

/// Draws into a Canvas, clipped to a rectangle. Coordinates are absolute screen cells.
class Painter {
public:
    Painter(Canvas& canvas, Rect clip) : canvas_(&canvas), clip_(clip.intersect(canvas.bounds())) {}

    [[nodiscard]] Painter clipped(Rect r) const { return {*canvas_, clip_.intersect(r)}; }
    [[nodiscard]] Rect clip() const { return clip_; }
    [[nodiscard]] Canvas& canvas() const { return *canvas_; }

    /// Draws text starting at (x, y); returns the column after the last grapheme drawn
    /// (or that would have been drawn, when clipped). Control characters are skipped.
    int text(int x, int y, std::string_view s, const Style& style) const;
    /// Like text() but limited to `max_width` columns, ending in "…" when cut.
    int text(int x, int y, std::string_view s, const Style& style, int max_width) const;
    /// Writes a single grapheme.
    void put(int x, int y, std::string_view grapheme, const Style& style) const;
    /// Fills the rectangle with spaces in `style` (a default background stays transparent).
    void fill(Rect r, const Style& style) const;
    /// Resets the rectangle to blank cells with background `bg`, hiding what was beneath.
    void clear(Rect r, Color bg = {}) const;
    /// Changes the background of every cell in `r`, keeping the text.
    void tint(Rect r, Color bg) const;

private:
    Canvas* canvas_;
    Rect clip_;
};

/// A run of styled text, used for labels composed of several colors.
struct Segment {
    std::string text;
    Style style;
};
using Segments = std::vector<Segment>;

/// Draws a box outline (rounded corners like btop by default).
void draw_box(const Painter& p, Rect r, const Style& border, bool rounded = true);
/// Draws "┐label┌" on a top border starting at column x; returns the next free column.
int top_label(const Painter& p, int x, int y, const Segments& label, const Style& border);
/// Draws "┐label┌" on a top border ending just before column `right`; returns its start.
int top_label_right(const Painter& p, int right, int y, const Segments& label, const Style& border);
/// Draws "┘label└" on a bottom border starting at column x; returns the next free column.
int bottom_label(const Painter& p, int x, int y, const Segments& label, const Style& border);
int segments_width(const Segments& label);
int draw_segments(const Painter& p, int x, int y, const Segments& label);

/// Horizontal meter of '■' cells; the filled part follows the gradient.
void meter(const Painter& p, int x, int y, int width, double fraction, Color start, Color mid,
           Color end, Color empty);

/// Braille area graph (2x4 dots per cell). `values` are 0..1, oldest first; the newest
/// values are right-aligned. Rows are colored bottom-to-top along the gradient.
void braille_graph(const Painter& p, Rect r, std::span<const double> values, Color start, Color mid,
                   Color end);

/// Vertical bar chart using eighth blocks. Bars are scaled to the largest value. The bar
/// for `highlight` (index into values, or -1) uses `hi`.
void bar_chart(const Painter& p, Rect r, std::span<const double> values, int highlight,
               Color normal, Color hi);

/// A one-column scrollbar for a view showing [offset, offset+visible) of `total` rows.
void scrollbar(const Painter& p, int x, int y, int height, int offset, int visible, int total,
               const Style& track, const Style& thumb);

/// Braille spinner frame for an animation tick.
std::string_view spinner(int tick);

} // namespace lv::tui
