#include "tui/input.hpp"

#include "util/strings.hpp"
#include "util/unicode.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <utility>
#include <vector>

namespace lv::tui {

namespace {

constexpr std::string_view paste_end = "\x1b[201~";

std::uint8_t mods_from_param(int param) {
    if (param <= 1) return ModNone;
    const int bits = param - 1;
    std::uint8_t mods = ModNone;
    if ((bits & 1) != 0) mods |= ModShift;
    if ((bits & (2 | 8)) != 0) mods |= ModAlt; // treat Meta as Alt
    if ((bits & 4) != 0) mods |= ModCtrl;
    return mods;
}

std::vector<int> parse_params(std::string_view params) {
    std::vector<int> out;
    for (const auto part : str::split(params, ';')) {
        const auto main = part.substr(0, part.find(':')); // drop sub-parameters
        int value = 0;
        std::from_chars(main.data(), main.data() + main.size(), value);
        out.push_back(value);
    }
    return out;
}

/// Length of the UTF-8 sequence introduced by `lead` (1 for invalid leads).
std::size_t utf8_length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

/// Maps a C0 control byte (or DEL) to the key it represents.
std::pair<Key, std::pair<char32_t, std::uint8_t>> control_key(unsigned char c) {
    switch (c) {
    case 0x0d: return {Key::Enter, {0, ModNone}};
    case 0x09: return {Key::Tab, {0, ModNone}};
    case 0x7f: return {Key::Backspace, {0, ModNone}};
    case 0x1b: return {Key::Escape, {0, ModNone}};
    case 0x00: return {Key::Char, {U' ', ModCtrl}};
    case 0x1c: return {Key::Char, {U'\\', ModCtrl}};
    case 0x1d: return {Key::Char, {U']', ModCtrl}};
    case 0x1e: return {Key::Char, {U'^', ModCtrl}};
    case 0x1f: return {Key::Char, {U'_', ModCtrl}};
    default: return {Key::Char, {static_cast<char32_t>(U'a' + c - 1), ModCtrl}};
    }
}

Key tilde_key(int code) {
    switch (code) {
    case 1:
    case 7: return Key::Home;
    case 2: return Key::Insert;
    case 3: return Key::Delete;
    case 4:
    case 8: return Key::End;
    case 5: return Key::PageUp;
    case 6: return Key::PageDown;
    case 11: return Key::F1;
    case 12: return Key::F2;
    case 13: return Key::F3;
    case 14: return Key::F4;
    case 15: return Key::F5;
    case 17: return Key::F6;
    case 18: return Key::F7;
    case 19: return Key::F8;
    case 20: return Key::F9;
    case 21: return Key::F10;
    case 23: return Key::F11;
    case 24: return Key::F12;
    default: return Key::None;
    }
}

Key letter_key(char final) {
    switch (final) {
    case 'A': return Key::Up;
    case 'B': return Key::Down;
    case 'C': return Key::Right;
    case 'D': return Key::Left;
    case 'H': return Key::Home;
    case 'F': return Key::End;
    case 'P': return Key::F1;
    case 'Q': return Key::F2;
    case 'R': return Key::F3;
    case 'S': return Key::F4;
    default: return Key::None;
    }
}

struct KeyName {
    std::string_view name;
    Key key;
};

constexpr std::array key_names = std::to_array<KeyName>({
    {"enter", Key::Enter},
    {"return", Key::Enter},
    {"cr", Key::Enter},
    {"tab", Key::Tab},
    {"backspace", Key::Backspace},
    {"bs", Key::Backspace},
    {"esc", Key::Escape},
    {"escape", Key::Escape},
    {"up", Key::Up},
    {"down", Key::Down},
    {"left", Key::Left},
    {"right", Key::Right},
    {"home", Key::Home},
    {"end", Key::End},
    {"pageup", Key::PageUp},
    {"pagedown", Key::PageDown},
    {"insert", Key::Insert},
    {"delete", Key::Delete},
    {"del", Key::Delete},
    {"f1", Key::F1},
    {"f2", Key::F2},
    {"f3", Key::F3},
    {"f4", Key::F4},
    {"f5", Key::F5},
    {"f6", Key::F6},
    {"f7", Key::F7},
    {"f8", Key::F8},
    {"f9", Key::F9},
    {"f10", Key::F10},
    {"f11", Key::F11},
    {"f12", Key::F12},
});

std::string key_label(Key key, char32_t ch) {
    switch (key) {
    case Key::Char:
        if (ch == U' ') return "Space";
        return unicode::to_utf8(ch);
    case Key::Enter: return "Enter";
    case Key::Tab: return "Tab";
    case Key::Backspace: return "BS";
    case Key::Escape: return "Esc";
    case Key::Up: return "↑";
    case Key::Down: return "↓";
    case Key::Left: return "←";
    case Key::Right: return "→";
    case Key::Home: return "Home";
    case Key::End: return "End";
    case Key::PageUp: return "PgUp";
    case Key::PageDown: return "PgDn";
    case Key::Insert: return "Ins";
    case Key::Delete: return "Del";
    case Key::None: return "?";
    default: return "F" + std::to_string(static_cast<int>(key) - static_cast<int>(Key::F1) + 1);
    }
}

std::string with_mods(std::uint8_t mods, const std::string& label) {
    std::string prefix;
    if ((mods & ModCtrl) != 0) prefix += "C-";
    if ((mods & ModAlt) != 0) prefix += "M-";
    if ((mods & ModShift) != 0) prefix += "S-";
    return prefix + label;
}

} // namespace

void InputParser::feed(std::string_view bytes) { buffer_.append(bytes); }

KeyEvent InputParser::take(std::size_t n, Key key, char32_t ch, std::uint8_t mods) {
    KeyEvent ev{.key = key, .ch = ch, .mods = mods, .raw = buffer_.substr(0, n)};
    buffer_.erase(0, n);
    return ev;
}

std::optional<Event> InputParser::next() {
    if (buffer_.empty()) {
        flush_ = false;
        return std::nullopt;
    }
    const auto lead = static_cast<unsigned char>(buffer_[0]);
    if (lead == 0x1b) {
        if (auto ev = parse_escape()) return ev;
        if (buffer_.starts_with("\x1b[200~")) return std::nullopt; // paste still arriving
        if (!flush_) return std::nullopt;
        // Give up waiting: a lone ESC, or ESC + byte typed with Alt.
        if (buffer_.size() >= 2) {
            const auto second = static_cast<unsigned char>(buffer_[1]);
            if (second >= 0x20 && second < 0x7f) return take(2, Key::Char, second, ModAlt);
        }
        return take(1, Key::Escape);
    }
    if (lead < 0x20 || lead == 0x7f) {
        const auto [key, rest] = control_key(lead);
        return take(1, key, rest.first, rest.second);
    }
    const std::size_t len = utf8_length(lead);
    if (buffer_.size() < len && !flush_) return std::nullopt;
    std::size_t pos = 0;
    const char32_t cp = unicode::decode(buffer_, pos);
    return take(pos, Key::Char, cp);
}

std::optional<Event> InputParser::parse_escape() {
    if (buffer_.size() < 2) return std::nullopt;
    const auto second = static_cast<unsigned char>(buffer_[1]);
    if (second == '[') return parse_csi(2);
    if (second == 'O') return parse_ss3();
    if (second == 0x1b) return take(1, Key::Escape);
    if (second < 0x20 || second == 0x7f) {
        const auto [key, rest] = control_key(second);
        return take(2, key, rest.first, static_cast<std::uint8_t>(rest.second | ModAlt));
    }
    const std::size_t len = utf8_length(second);
    if (buffer_.size() < 1 + len) return std::nullopt;
    std::size_t pos = 1;
    const char32_t cp = unicode::decode(buffer_, pos);
    return take(pos, Key::Char, cp, ModAlt);
}

std::optional<Event> InputParser::parse_csi(std::size_t start) {
    std::size_t i = start;
    while (i < buffer_.size()) {
        const auto c = static_cast<unsigned char>(buffer_[i]);
        if (c >= 0x40 && c <= 0x7e) break;
        if (c < 0x20 || i - start > 64) {
            // Not a CSI sequence after all; drop the introducer.
            return take(i, Key::None);
        }
        ++i;
    }
    if (i >= buffer_.size()) return std::nullopt;

    const char final = buffer_[i];
    std::string_view body(buffer_.data() + start, i - start);
    const std::size_t total = i + 1;

    if (body.starts_with('<') && (final == 'M' || final == 'm')) {
        const auto p = parse_params(body.substr(1));
        if (p.size() < 3) return take(total, Key::None);
        const int b = p[0];
        MouseEvent ev;
        ev.x = p[1] - 1;
        ev.y = p[2] - 1;
        ev.button = b & 3;
        if ((b & 4) != 0) ev.mods |= ModShift;
        if ((b & 8) != 0) ev.mods |= ModAlt;
        if ((b & 16) != 0) ev.mods |= ModCtrl;
        if ((b & 64) != 0) {
            if ((b & 3) > 1) return take(total, Key::None); // horizontal wheel
            ev.kind = (b & 1) != 0 ? MouseEvent::Kind::WheelDown : MouseEvent::Kind::WheelUp;
        } else if ((b & 32) != 0) {
            ev.kind = (b & 3) == 3 ? MouseEvent::Kind::Move : MouseEvent::Kind::Drag;
        } else {
            ev.kind = final == 'M' ? MouseEvent::Kind::Press : MouseEvent::Kind::Release;
        }
        buffer_.erase(0, total);
        return ev;
    }

    if (final == '~' && body == "200") return parse_paste();

    if (body.empty() && (final == 'I' || final == 'O')) {
        buffer_.erase(0, total);
        return FocusEvent{.gained = final == 'I'};
    }

    const auto params = parse_params(body);
    const int first = params.empty() ? 0 : params[0];
    const std::uint8_t mods =
        params.size() >= 2 ? mods_from_param(params[1]) : std::uint8_t{ModNone};

    if (final == '~') return take(total, tilde_key(first), 0, mods);
    if (final == 'Z') return take(total, Key::Tab, 0, ModShift);
    if (final == 'u') {
        switch (first) {
        case 13: return take(total, Key::Enter, 0, mods);
        case 9: return take(total, Key::Tab, 0, mods);
        case 27: return take(total, Key::Escape, 0, mods);
        case 127: return take(total, Key::Backspace, 0, mods);
        default:
            return take(total, Key::Char, static_cast<char32_t>(first),
                        static_cast<std::uint8_t>(mods & ~ModShift));
        }
    }
    return take(total, letter_key(final), 0, mods);
}

std::optional<Event> InputParser::parse_ss3() {
    if (buffer_.size() < 3) return std::nullopt;
    return take(3, letter_key(buffer_[2]));
}

std::optional<Event> InputParser::parse_paste() {
    constexpr std::size_t start_len = 6; // "\x1b[200~"
    const std::size_t end = buffer_.find(paste_end, start_len);
    if (end == std::string::npos) return std::nullopt;
    PasteEvent ev{.text = buffer_.substr(start_len, end - start_len)};
    buffer_.erase(0, end + paste_end.size());
    return ev;
}

std::optional<KeySpec> KeySpec::parse(std::string_view text) {
    KeySpec spec;
    std::string lowered = str::to_lower(str::trim(text));
    std::string_view rest = lowered;
    std::string_view original = str::trim(text);
    const auto strip = [&](std::string_view prefix) {
        if (rest.size() > prefix.size() && rest.starts_with(prefix)) {
            rest.remove_prefix(prefix.size());
            original.remove_prefix(prefix.size());
            return true;
        }
        return false;
    };
    bool progress = true;
    while (progress) {
        progress = false;
        for (const auto [prefix, mod] : std::array<std::pair<std::string_view, Mod>, 8>{{
                 {"ctrl-", ModCtrl},
                 {"ctrl+", ModCtrl},
                 {"c-", ModCtrl},
                 {"alt-", ModAlt},
                 {"alt+", ModAlt},
                 {"m-", ModAlt},
                 {"shift-", ModShift},
                 {"s-", ModShift},
             }}) {
            if (strip(prefix)) {
                spec.mods |= mod;
                progress = true;
            }
        }
    }
    if (rest.empty()) return std::nullopt;
    if (rest == "space") {
        spec.key = Key::Char;
        spec.ch = U' ';
        return spec;
    }
    for (const auto& [name, key] : key_names) {
        if (rest == name) {
            spec.key = key;
            return spec;
        }
    }
    // A single character; keep its original case unless Ctrl makes case irrelevant.
    std::size_t pos = 0;
    const char32_t cp = unicode::decode(original, pos);
    if (pos != original.size()) return std::nullopt;
    spec.key = Key::Char;
    spec.ch = cp;
    if ((spec.mods & ModCtrl) != 0 && cp >= U'A' && cp <= U'Z') spec.ch = cp - U'A' + U'a';
    if ((spec.mods & ModShift) != 0 && cp >= U'a' && cp <= U'z') {
        spec.ch = cp - U'a' + U'A';
        spec.mods &= static_cast<std::uint8_t>(~ModShift);
    }
    return spec;
}

bool KeySpec::matches(const KeyEvent& ev) const {
    if (ev.key != key || ev.mods != mods) return false;
    return key != Key::Char || ev.ch == ch;
}

std::string KeySpec::to_string() const { return with_mods(mods, key_label(key, ch)); }

std::string KeySpec::to_spec() const {
    std::string out;
    if ((mods & ModCtrl) != 0) out += "ctrl-";
    if ((mods & ModAlt) != 0) out += "alt-";
    if ((mods & ModShift) != 0) out += "shift-";
    if (key == Key::Char) return out + (ch == U' ' ? "space" : unicode::to_utf8(ch));
    const auto* name = std::ranges::find(key_names, key, &KeyName::key);
    return name != key_names.end() ? out + std::string(name->name) : out;
}

std::string describe(const KeyEvent& ev) { return with_mods(ev.mods, key_label(ev.key, ev.ch)); }

} // namespace lv::tui
