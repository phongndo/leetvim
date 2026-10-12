#include "tui/painter.hpp"

#include "util/unicode.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace lv::tui {

int Painter::text(int x, int y, std::string_view s, const Style& style) const {
    const bool row_visible = y >= clip_.y && y < clip_.bottom();
    std::size_t pos = 0;
    while (pos < s.size()) {
        const auto g = unicode::next_grapheme(s, pos);
        std::string_view bytes = g.bytes;
        int width = g.width;
        if (width == 0) {
            if (static_cast<unsigned char>(bytes.front()) >= 0x20) continue; // stray combiner
            bytes = " "; // control characters (tabs included) render as one space
            width = 1;
        }
        if (row_visible) {
            for (int i = 0; i < width; ++i) {
                if (!clip_.contains(x + i, y)) continue;
                if (x >= clip_.x && x + width <= clip_.right()) {
                    canvas_->put(x, y, bytes, width, style);
                    break;
                }
                canvas_->put(x + i, y, " ", 1, style); // wide char cut by the clip edge
            }
        }
        x += width;
    }
    return x;
}

int Painter::text(int x, int y, std::string_view s, const Style& style, int max_width) const {
    if (max_width <= 0) return x;
    const std::string cut = unicode::truncate(s, max_width);
    return text(x, y, cut, style);
}

void Painter::put(int x, int y, std::string_view grapheme, const Style& style) const {
    std::size_t pos = 0;
    const auto g = unicode::next_grapheme(grapheme, pos);
    if (!clip_.contains(x, y) || x + std::max(g.width, 1) > clip_.right()) return;
    canvas_->put(x, y, g.bytes, std::max(g.width, 1), style);
}

void Painter::fill(Rect r, const Style& style) const {
    const Rect area = r.intersect(clip_);
    for (int y = area.y; y < area.bottom(); ++y) {
        for (int x = area.x; x < area.right(); ++x) canvas_->put(x, y, " ", 1, style);
    }
}

void Painter::clear(Rect r, Color bg) const {
    const Rect area = r.intersect(clip_);
    for (int y = area.y; y < area.bottom(); ++y) {
        for (int x = area.x; x < area.right(); ++x) {
            canvas_->at(x, y) = Cell{.text = " ", .style = Style{.fg = {}, .bg = bg}, .width = 1};
        }
    }
}

void Painter::tint(Rect r, Color bg) const {
    const Rect area = r.intersect(clip_);
    for (int y = area.y; y < area.bottom(); ++y) {
        for (int x = area.x; x < area.right(); ++x) canvas_->at(x, y).style.bg = bg;
    }
}

void draw_box(const Painter& p, Rect r, const Style& border, bool rounded) {
    if (r.w < 2 || r.h < 2) return;
    const std::string_view tl = rounded ? "╭" : "┌";
    const std::string_view tr = rounded ? "╮" : "┐";
    const std::string_view bl = rounded ? "╰" : "└";
    const std::string_view br = rounded ? "╯" : "┘";
    for (int x = r.x + 1; x < r.right() - 1; ++x) {
        p.put(x, r.y, "─", border);
        p.put(x, r.bottom() - 1, "─", border);
    }
    for (int y = r.y + 1; y < r.bottom() - 1; ++y) {
        p.put(r.x, y, "│", border);
        p.put(r.right() - 1, y, "│", border);
    }
    p.put(r.x, r.y, tl, border);
    p.put(r.right() - 1, r.y, tr, border);
    p.put(r.x, r.bottom() - 1, bl, border);
    p.put(r.right() - 1, r.bottom() - 1, br, border);
}

int segments_width(const Segments& label) {
    int w = 0;
    for (const auto& seg : label) w += unicode::display_width(seg.text);
    return w;
}

int draw_segments(const Painter& p, int x, int y, const Segments& label) {
    for (const auto& seg : label) x = p.text(x, y, seg.text, seg.style);
    return x;
}

int top_label(const Painter& p, int x, int y, const Segments& label, const Style& border) {
    p.put(x, y, "┐", border);
    x = draw_segments(p, x + 1, y, label);
    p.put(x, y, "┌", border);
    return x + 1;
}

int top_label_right(const Painter& p, int right, int y, const Segments& label,
                    const Style& border) {
    const int start = right - segments_width(label) - 2;
    top_label(p, start, y, label, border);
    return start;
}

int bottom_label(const Painter& p, int x, int y, const Segments& label, const Style& border) {
    p.put(x, y, "┘", border);
    x = draw_segments(p, x + 1, y, label);
    p.put(x, y, "└", border);
    return x + 1;
}

void meter(const Painter& p, int x, int y, int width, double fraction, Color start, Color mid,
           Color end, Color empty) {
    if (width <= 0) return;
    fraction = std::clamp(fraction, 0.0, 1.0);
    const int filled = static_cast<int>(std::lround(fraction * width));
    for (int i = 0; i < width; ++i) {
        const double t = width == 1 ? 1.0 : static_cast<double>(i) / (width - 1);
        const Color c = i < filled ? gradient(start, mid, end, t) : empty;
        p.put(x + i, y, "■", Style{.fg = c});
    }
}

void braille_graph(const Painter& p, Rect r, std::span<const double> values, Color start, Color mid,
                   Color end) {
    if (r.empty()) return;
    const int dot_rows = r.h * 4;
    const int columns = r.w * 2;
    const auto count = static_cast<int>(values.size());
    const auto dots_for = [&](int column) {
        // Column `columns - 1` shows the newest value.
        const int index = count - (columns - column);
        if (index < 0) return 0;
        const double v = std::clamp(values[static_cast<std::size_t>(index)], 0.0, 1.0);
        const int n = static_cast<int>(std::lround(v * dot_rows));
        return v > 0.0 ? std::max(n, 1) : 0;
    };
    static constexpr std::array<std::array<unsigned, 4>, 2> bits{{
        {0x01, 0x02, 0x04, 0x40},
        {0x08, 0x10, 0x20, 0x80},
    }};
    for (int row = 0; row < r.h; ++row) {
        const int from_bottom = (r.h - 1 - row) * 4;
        const double t = r.h == 1 ? 0.5 : static_cast<double>(r.h - 1 - row) / (r.h - 1);
        const Style style{.fg = gradient(start, mid, end, t)};
        for (int cx = 0; cx < r.w; ++cx) {
            unsigned pattern = 0;
            for (int half = 0; half < 2; ++half) {
                const int n = dots_for((cx * 2) + half);
                for (int d = 0; d < 4; ++d) {
                    if (from_bottom + (3 - d) < n) {
                        pattern |=
                            bits.at(static_cast<std::size_t>(half)).at(static_cast<std::size_t>(d));
                    }
                }
            }
            if (pattern != 0) p.put(r.x + cx, r.y + row, unicode::to_utf8(0x2800 + pattern), style);
        }
    }
}

void bar_chart(const Painter& p, Rect r, std::span<const double> values, int highlight,
               Color normal, Color hi) {
    if (r.empty() || values.empty()) return;
    // Merge neighbouring values when there are more values than columns.
    const auto n = static_cast<int>(values.size());
    const int bars = std::min(n, r.w);
    std::vector<double> merged(static_cast<std::size_t>(bars), 0.0);
    int hi_bar = -1;
    for (int i = 0; i < n; ++i) {
        const int b = static_cast<int>(static_cast<long long>(i) * bars / n);
        merged[static_cast<std::size_t>(b)] += values[static_cast<std::size_t>(i)];
        if (i == highlight) hi_bar = b;
    }
    const double peak = *std::ranges::max_element(merged);
    if (peak <= 0) return;
    const int bar_width = std::max(1, r.w / bars);
    static constexpr std::array<std::string_view, 9> eighths{" ", "▁", "▂", "▃", "▄",
                                                             "▅", "▆", "▇", "█"};
    for (int b = 0; b < bars; ++b) {
        const double v = merged[static_cast<std::size_t>(b)];
        int units = static_cast<int>(std::lround(v / peak * r.h * 8));
        if (v > 0 && units == 0) units = 1;
        const Style style{.fg = b == hi_bar ? hi : normal};
        for (int row = r.h - 1; row >= 0 && units > 0; --row) {
            const int take = std::min(units, 8);
            for (int k = 0; k < bar_width; ++k) {
                const int x = r.x + (b * bar_width) + k;
                if (x < r.right())
                    p.put(x, r.y + row, eighths.at(static_cast<std::size_t>(take)), style);
            }
            units -= take;
        }
    }
}

void scrollbar(const Painter& p, int x, int y, int height, int offset, int visible, int total,
               const Style& track, const Style& thumb) {
    if (height <= 0 || total <= visible) return;
    const int thumb_h = std::max(1, height * visible / total);
    const int max_offset = std::max(1, total - visible);
    const int thumb_y = (height - thumb_h) * std::clamp(offset, 0, max_offset) / max_offset;
    for (int i = 0; i < height; ++i) {
        const bool on = i >= thumb_y && i < thumb_y + thumb_h;
        p.put(x, y + i, on ? "┃" : "│", on ? thumb : track);
    }
}

std::string_view spinner(int tick) {
    static constexpr std::array<std::string_view, 10> frames{"⠋", "⠙", "⠹", "⠸", "⠼",
                                                             "⠴", "⠦", "⠧", "⠇", "⠏"};
    return frames.at(static_cast<std::size_t>(tick) % frames.size());
}

} // namespace lv::tui
