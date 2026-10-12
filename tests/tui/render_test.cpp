#include "tui/canvas.hpp"
#include "tui/color.hpp"
#include "tui/painter.hpp"
#include "tui/renderer.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace lv::tui;

namespace {

std::string row_text(const Canvas& c, int y) {
    std::string out;
    for (int x = 0; x < c.width(); ++x) out += c.at(x, y).text;
    return out;
}

} // namespace

TEST_CASE("color parsing and conversion") {
    CHECK(Color::parse("#ff0000") == Color::rgb(255, 0, 0));
    CHECK(Color::parse("#cc") == Color::rgb(0xcc, 0xcc, 0xcc));
    CHECK(Color::parse("#fff") == Color::rgb(255, 255, 255));
    CHECK(Color::parse("42") == Color::indexed(42));
    CHECK(Color::parse("default")->is_default());
    CHECK_FALSE(Color::parse("#12345").has_value());
    CHECK_FALSE(Color::parse("300").has_value());
    CHECK(to_xterm256(Color::rgb(255, 0, 0)) == 196);
    CHECK(to_xterm256(Color::rgb(128, 128, 128)) == 244);
    CHECK(gradient(Color::rgb(0, 0, 0), Color::rgb(100, 100, 100), Color::rgb(200, 200, 200), 1.0)
          == Color::rgb(200, 200, 200));
}

TEST_CASE("canvas repairs overwritten wide characters") {
    Canvas c(6, 1);
    c.put(1, 0, "中", 2, {});
    CHECK(c.at(1, 0).width == 2);
    CHECK(c.at(2, 0).width == 0);
    c.put(2, 0, "x", 1, {}); // clobber the right half
    CHECK(c.at(1, 0).text == " ");
    CHECK(c.at(2, 0).text == "x");
    c.put(5, 0, "中", 2, {}); // does not fit at the edge
    CHECK(c.at(5, 0).text == " ");
}

TEST_CASE("painter clips and truncates") {
    Canvas c(10, 2);
    const Painter p(c, Rect{2, 0, 5, 1});
    const int end = p.text(0, 0, "abcdefghij", {});
    CHECK(end == 10);
    CHECK(row_text(c, 0) == "  cdefg   ");
    const Painter q(c, c.bounds());
    q.text(0, 1, "hello world", {}, 6);
    CHECK(row_text(c, 1) == "hello…    ");
}

TEST_CASE("boxes and labels") {
    Canvas c(12, 3);
    const Painter p(c, c.bounds());
    draw_box(p, c.bounds(), {});
    top_label(p, 1, 0, {{.text = "ab", .style = {}}}, {});
    CHECK(row_text(c, 0) == "╭┐ab┌──────╮");
    CHECK(row_text(c, 1) == "│          │");
    CHECK(row_text(c, 2) == "╰──────────╯");
}

TEST_CASE("renderer emits only changes") {
    Renderer r(true);
    r.resize(4, 2);
    r.canvas().clear();
    const std::string first = r.present(std::nullopt);
    CHECK(first.find("\x1b[2J") != std::string::npos);

    r.canvas().put(1, 1, "x", 1, Style{.fg = Color::rgb(1, 2, 3)});
    const std::string second = r.present(Cursor{.x = 0, .y = 0, .shape = CursorShape::Bar});
    CHECK(second.find("\x1b[2;2H") != std::string::npos);
    CHECK(second.find("38;2;1;2;3") != std::string::npos);
    CHECK(second.find('x') != std::string::npos);
    CHECK(second.find("\x1b[6 q") != std::string::npos);
    CHECK(second.find("\x1b[2J") == std::string::npos);

    const std::string third = r.present(std::nullopt);
    CHECK(third.find('x') == std::string::npos);
}

TEST_CASE("renderer falls back to 256 colors") {
    Renderer r(false);
    r.resize(1, 1);
    r.canvas().put(0, 0, "x", 1, Style{.fg = Color::rgb(255, 0, 0)});
    CHECK(r.present(std::nullopt).find("38;5;196") != std::string::npos);
}

TEST_CASE("braille graph draws bottom-up") {
    Canvas c(2, 2);
    const Painter p(c, c.bounds());
    const std::vector<double> values{0.0, 0.0, 1.0, 0.5};
    braille_graph(p, c.bounds(), values, Color::rgb(0, 255, 0), Color::rgb(255, 255, 0),
                  Color::rgb(255, 0, 0));
    // Rightmost cell: left column full (8 dots), right column half (bottom 4 dots).
    CHECK(c.at(1, 0).text == "⡇");
    CHECK(c.at(1, 1).text == "⣿");
    CHECK(c.at(0, 1).text == " ");
}

TEST_CASE("meter fills proportionally") {
    Canvas c(10, 1);
    const Painter p(c, c.bounds());
    const Color empty = Color::rgb(9, 9, 9);
    meter(p, 0, 0, 10, 0.3, Color::rgb(0, 255, 0), Color::rgb(255, 255, 0), Color::rgb(255, 0, 0),
          empty);
    CHECK(c.at(2, 0).style.fg != empty);
    CHECK(c.at(3, 0).style.fg == empty);
}
