#include "tui/renderer.hpp"

#include <format>
#include <iterator>

namespace lv::tui {

void Renderer::resize(int width, int height) {
    front_.resize(width, height);
    back_.resize(width, height);
    full_redraw_ = true;
}

void Renderer::append_color(std::string& out, const Color& c, bool foreground) const {
    switch (c.kind) {
    case Color::Kind::Default: out += foreground ? ";39" : ";49"; break;
    case Color::Kind::Indexed:
        if (c.index < 8) {
            std::format_to(std::back_inserter(out), ";{}", (foreground ? 30 : 40) + c.index);
        } else if (c.index < 16) {
            std::format_to(std::back_inserter(out), ";{}", (foreground ? 90 : 100) + c.index - 8);
        } else {
            std::format_to(std::back_inserter(out), ";{};5;{}", foreground ? 38 : 48, c.index);
        }
        break;
    case Color::Kind::Rgb:
        if (truecolor_) {
            std::format_to(std::back_inserter(out), ";{};2;{};{};{}", foreground ? 38 : 48, c.r,
                           c.g, c.b);
        } else {
            std::format_to(std::back_inserter(out), ";{};5;{}", foreground ? 38 : 48,
                           to_xterm256(c));
        }
        break;
    }
}

void Renderer::append_style(std::string& out, const Style& style) const {
    out += "\x1b[0";
    if ((style.attrs & AttrBold) != 0) out += ";1";
    if ((style.attrs & AttrDim) != 0) out += ";2";
    if ((style.attrs & AttrItalic) != 0) out += ";3";
    if ((style.attrs & AttrUnderline) != 0) out += ";4";
    if ((style.attrs & AttrBlink) != 0) out += ";5";
    if ((style.attrs & AttrReverse) != 0) out += ";7";
    if ((style.attrs & AttrStrike) != 0) out += ";9";
    if (!style.fg.is_default()) append_color(out, style.fg, true);
    if (!style.bg.is_default()) append_color(out, style.bg, false);
    out += 'm';
}

std::string Renderer::present(const std::optional<Cursor>& cursor) {
    std::string out;
    out.reserve(4096);
    out += "\x1b[?2026h"; // synchronized update: terminals that support it avoid tearing
    out += "\x1b[?25l";
    if (full_redraw_) out += "\x1b[0m\x1b[2J";

    int cx = -1;
    int cy = -1;
    std::optional<Style> current;
    for (int y = 0; y < back_.height(); ++y) {
        for (int x = 0; x < back_.width(); ++x) {
            const Cell& cell = back_.at(x, y);
            if (cell.width == 0) continue; // drawn with its wide lead
            const bool changed = full_redraw_ || cell != front_.at(x, y)
                                 || (cell.width == 2 && x + 1 < back_.width()
                                     && back_.at(x + 1, y) != front_.at(x + 1, y));
            if (!changed) continue;
            if (cx != x || cy != y)
                std::format_to(std::back_inserter(out), "\x1b[{};{}H", y + 1, x + 1);
            if (!current || *current != cell.style) {
                append_style(out, cell.style);
                current = cell.style;
            }
            out += cell.text.empty() ? " " : cell.text;
            cx = x + cell.width;
            cy = y;
        }
    }
    out += "\x1b[0m";

    if (cursor) {
        std::format_to(std::back_inserter(out), "\x1b[{};{}H", cursor->y + 1, cursor->x + 1);
        if (cursor->shape != last_shape_) {
            std::format_to(std::back_inserter(out), "\x1b[{} q", static_cast<int>(cursor->shape));
            last_shape_ = cursor->shape;
        }
        out += "\x1b[?25h";
    }
    out += "\x1b[?2026l";

    front_ = back_;
    full_redraw_ = false;
    return out;
}

} // namespace lv::tui
