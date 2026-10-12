#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace lv::tui {

enum class Key : std::uint8_t {
    None,
    Char, // printable character (or a Ctrl/Alt chord on one), see KeyEvent::ch
    Enter,
    Tab,
    Backspace,
    Escape,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    Insert,
    Delete,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
};

enum Mod : std::uint8_t {
    ModNone = 0,
    ModShift = 1U << 0U,
    ModAlt = 1U << 1U,
    ModCtrl = 1U << 2U,
};

struct KeyEvent {
    Key key = Key::None;
    char32_t ch = 0; // for Key::Char; Ctrl chords report the lowercase letter
    std::uint8_t mods = ModNone;
    std::string raw; // the exact bytes received, for forwarding to child programs

    /// A plain character press: `ev.is('j')`, `ev.is('d', ModCtrl)`.
    [[nodiscard]] bool is(char32_t c, std::uint8_t m = ModNone) const {
        return key == Key::Char && ch == c && mods == m;
    }
    [[nodiscard]] bool is(Key k, std::uint8_t m = ModNone) const { return key == k && mods == m; }
};

struct MouseEvent {
    enum class Kind : std::uint8_t { Press, Release, Drag, Move, WheelUp, WheelDown };
    Kind kind = Kind::Press;
    int button = 0; // 0 left, 1 middle, 2 right
    int x = 0;      // 0-based cell coordinates
    int y = 0;
    std::uint8_t mods = ModNone;
};

struct PasteEvent {
    std::string text;
};

struct FocusEvent {
    bool gained = true;
};

using Event = std::variant<KeyEvent, MouseEvent, PasteEvent, FocusEvent>;

/// Incremental decoder for terminal input bytes (xterm-style keys, SGR mouse,
/// bracketed paste, focus reports, CSI-u).
class InputParser {
public:
    void feed(std::string_view bytes);
    /// Next complete event, or nullopt if more bytes are needed.
    std::optional<Event> next();
    /// True while the buffer holds a prefix that might still become a longer sequence
    /// (typically a lone ESC). The caller waits briefly, then calls flush().
    [[nodiscard]] bool pending() const { return !buffer_.empty(); }
    /// Interprets whatever is buffered as-is (a lone ESC becomes the Escape key).
    void flush() { flush_ = true; }

private:
    std::optional<Event> parse_escape();
    std::optional<Event> parse_csi(std::size_t start);
    std::optional<Event> parse_ss3();
    std::optional<Event> parse_paste();
    KeyEvent take(std::size_t n, Key key, char32_t ch = 0, std::uint8_t mods = ModNone);

    std::string buffer_;
    bool flush_ = false;
};

/// A key binding parsed from config text such as "ctrl-g", "alt-x", "f5", "ctrl-\".
struct KeySpec {
    Key key = Key::None;
    char32_t ch = 0;
    std::uint8_t mods = ModNone;

    static std::optional<KeySpec> parse(std::string_view text);
    /// The key of an event, e.g. one pressed to rebind something.
    static KeySpec of(const KeyEvent& ev) { return {.key = ev.key, .ch = ev.ch, .mods = ev.mods}; }
    [[nodiscard]] bool matches(const KeyEvent& ev) const;
    [[nodiscard]] std::string to_string() const; // "C-g", "F5", ...
    /// Text that parse() reads back: "ctrl-g", "f5", "space", "T".
    [[nodiscard]] std::string to_spec() const;
    bool operator==(const KeySpec&) const = default;
};

/// Short human label for a key event: "C-d", "Enter", "g".
std::string describe(const KeyEvent& ev);

} // namespace lv::tui
