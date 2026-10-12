#pragma once

#include "tui/canvas.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace lv::tui {

/// DECSCUSR cursor shapes.
enum class CursorShape : std::uint8_t {
    Default = 0,
    BlinkingBlock = 1,
    Block = 2,
    BlinkingUnderline = 3,
    Underline = 4,
    BlinkingBar = 5,
    Bar = 6,
};

struct Cursor {
    int x = 0;
    int y = 0;
    CursorShape shape = CursorShape::Default;
};

/// Double-buffered renderer: draw a frame into canvas(), then present() returns the
/// minimal escape sequence stream that turns the previous frame into the new one.
class Renderer {
public:
    explicit Renderer(bool truecolor = true) : truecolor_(truecolor) {}

    void resize(int width, int height);
    /// Forces the next present() to repaint every cell (e.g. after the terminal was
    /// suspended or garbled by another program).
    void invalidate() { full_redraw_ = true; }
    void set_truecolor(bool on) {
        truecolor_ = on;
        full_redraw_ = true;
    }

    Canvas& canvas() { return back_; }

    /// Diffs the back buffer against what is on screen. `cursor` shows the hardware
    /// cursor at that cell; nullopt hides it.
    std::string present(const std::optional<Cursor>& cursor);

private:
    void append_style(std::string& out, const Style& style) const;
    void append_color(std::string& out, const Color& c, bool foreground) const;

    Canvas front_;
    Canvas back_;
    bool truecolor_;
    bool full_redraw_ = true;
    CursorShape last_shape_ = CursorShape::Default;
};

} // namespace lv::tui
