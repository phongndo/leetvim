#include "tui/canvas.hpp"

namespace lv::tui {

void Canvas::resize(int width, int height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    cells_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), Cell{});
}

void Canvas::clear(const Style& style) {
    for (auto& cell : cells_) {
        cell.text = " ";
        cell.width = 1;
        cell.style = style;
    }
}

void Canvas::put(int x, int y, std::string_view grapheme, int width, const Style& style) {
    if (!in_bounds(x, y)) return;
    if (width == 2 && x + 1 >= width_) {
        grapheme = " ";
        width = 1;
    }
    // Overwriting half of an existing wide character leaves the other half orphaned.
    Cell& cell = at(x, y);
    if (cell.width == 0 && x > 0) {
        Cell& lead = at(x - 1, y);
        lead.text = " ";
        lead.width = 1;
    }
    const int end = x + std::max(width, 1);
    if (end < width_ && at(end - 1, y).width == 2) {
        Cell& tail = at(end, y);
        tail.text = " ";
        tail.width = 1;
    }

    // A default background is transparent: it keeps whatever background is underneath,
    // so a themed canvas background survives text drawn on top of it.
    const auto apply = [&](Cell& c) {
        const Color under = c.style.bg;
        c.style = style;
        if (style.bg.is_default()) c.style.bg = under;
    };
    cell.text.assign(grapheme);
    cell.width = static_cast<std::uint8_t>(width);
    apply(cell);
    if (width == 2) {
        Cell& cont = at(x + 1, y);
        cont.text.clear();
        cont.width = 0;
        apply(cont);
    }
}

void Canvas::restyle(int x, int y, const Style& style) {
    if (in_bounds(x, y)) at(x, y).style = style;
}

} // namespace lv::tui
