#include "tui/term_pane.hpp"

#include "tui/terminal.hpp"
#include "util/unicode.hpp"

#include <vterm.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>

#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

namespace lv::tui {

namespace {

TermPane* self(void* user) { return static_cast<TermPane*>(user); }

int cb_damage(VTermRect /*rect*/, void* user) {
    self(user)->on_damage();
    return 1;
}

int cb_moverect(VTermRect /*dest*/, VTermRect /*src*/, void* user) {
    self(user)->on_damage();
    return 1;
}

int cb_movecursor(VTermPos pos, VTermPos /*old*/, int /*visible*/, void* user) {
    self(user)->on_cursor(pos.row, pos.col);
    return 1;
}

// NOLINTNEXTLINE(misc-const-correctness): signature fixed by libvterm
int cb_settermprop(VTermProp prop, VTermValue* val, void* user) {
    self(user)->on_prop(prop, val);
    return 1;
}

int cb_bell(void* /*user*/) { return 1; }

int cb_sb_pushline(int /*cols*/, const VTermScreenCell* /*cells*/, void* /*user*/) { return 0; }

constexpr VTermScreenCallbacks screen_callbacks{
    .damage = cb_damage,
    .moverect = cb_moverect,
    .movecursor = cb_movecursor,
    .settermprop = cb_settermprop,
    .bell = cb_bell,
    .resize = nullptr,
    .sb_pushline = cb_sb_pushline,
    .sb_popline = nullptr,
    .sb_clear = nullptr,
};

void cb_output(const char* s, std::size_t len, void* user) { self(user)->on_output(s, len); }

int cb_selection_set(VTermSelectionMask mask, VTermStringFragment frag, void* user) {
    self(user)->on_selection(mask, frag.str, frag.len, frag.initial, frag.final);
    return 1;
}

constexpr VTermSelectionCallbacks selection_callbacks{
    .set = cb_selection_set,
    .query = nullptr,
};

VTermModifier to_vterm_mods(std::uint8_t mods) {
    unsigned m = VTERM_MOD_NONE;
    if ((mods & ModShift) != 0) m |= VTERM_MOD_SHIFT;
    if ((mods & ModAlt) != 0) m |= VTERM_MOD_ALT;
    if ((mods & ModCtrl) != 0) m |= VTERM_MOD_CTRL;
    // VTermModifier is a set of bit flags, so combinations aren't enumerators.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    return static_cast<VTermModifier>(m);
}

VTermKey to_vterm_key(Key key) {
    switch (key) {
    case Key::Up: return VTERM_KEY_UP;
    case Key::Down: return VTERM_KEY_DOWN;
    case Key::Left: return VTERM_KEY_LEFT;
    case Key::Right: return VTERM_KEY_RIGHT;
    case Key::Home: return VTERM_KEY_HOME;
    case Key::End: return VTERM_KEY_END;
    case Key::PageUp: return VTERM_KEY_PAGEUP;
    case Key::PageDown: return VTERM_KEY_PAGEDOWN;
    case Key::Insert: return VTERM_KEY_INS;
    case Key::Delete: return VTERM_KEY_DEL;
    default:
        if (key >= Key::F1 && key <= Key::F12) {
            return static_cast<VTermKey>(
                VTERM_KEY_FUNCTION(static_cast<int>(key) - static_cast<int>(Key::F1) + 1));
        }
        return VTERM_KEY_NONE;
    }
}

Color to_color(const VTermColor& c, Color fallback, bool foreground) {
    if (foreground ? VTERM_COLOR_IS_DEFAULT_FG(&c) : VTERM_COLOR_IS_DEFAULT_BG(&c)) {
        return fallback;
    }
    if (VTERM_COLOR_IS_INDEXED(&c)) return Color::indexed(c.indexed.idx);
    return Color::rgb(c.rgb.red, c.rgb.green, c.rgb.blue);
}

// Variables that would make the editor believe it talks to the outer terminal (or tmux)
// directly; inside the pane it talks to libvterm.
constexpr std::array scrubbed_env{
    "TMUX",
    "TMUX_PANE",
    "STY",
    "TERM_PROGRAM",
    "TERM_PROGRAM_VERSION",
    "KITTY_WINDOW_ID",
    "KITTY_PID",
    "WEZTERM_PANE",
    "VTE_VERSION",
    "ITERM_SESSION_ID",
    "LC_TERMINAL",
    "LC_TERMINAL_VERSION",
    "NVIM",
    "NVIM_LISTEN_ADDRESS",
};

} // namespace

TermPane::~TermPane() {
    terminate();
    if (vt_ != nullptr) vterm_free(vt_);
}

bool TermPane::spawn(const Options& options, std::string& error) {
    if (options.argv.empty()) {
        error = "no command to run";
        return false;
    }
    rows_ = std::max(1, options.rows);
    cols_ = std::max(1, options.cols);

    if (vt_ == nullptr) {
        vt_ = vterm_new(rows_, cols_);
        vterm_set_utf8(vt_, 1);
        vterm_output_set_callback(vt_, cb_output, this);
        screen_ = vterm_obtain_screen(vt_);
        vterm_screen_set_callbacks(screen_, &screen_callbacks, this);
        vterm_screen_enable_altscreen(screen_, 1);
        selection_scratch_.resize(4096);
        vterm_state_set_selection_callbacks(vterm_obtain_state(vt_), &selection_callbacks, this,
                                            selection_scratch_.data(), selection_scratch_.size());
    } else {
        vterm_set_size(vt_, rows_, cols_);
    }
    vterm_screen_reset(screen_, 1);

    std::vector<std::string> args = options.argv;
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);

    winsize ws{};
    ws.ws_row = static_cast<unsigned short>(rows_);
    ws.ws_col = static_cast<unsigned short>(cols_);
    int master = -1;
    const pid_t pid = forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0) {
        error = std::string("forkpty failed: ") + std::strerror(errno);
        return false;
    }
    if (pid == 0) {
        // Child: only async-signal-safe-ish calls from here until exec.
        if (!options.cwd.empty() && chdir(options.cwd.c_str()) != 0) _exit(126);
        for (const char* name : scrubbed_env) unsetenv(name);
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        for (const auto& [key, value] : options.env) setenv(key.c_str(), value.c_str(), 1);
        (void)std::signal(SIGPIPE, SIG_DFL);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    pid_ = pid;
    master_ = master;
    exit_status_.reset();
    fcntl(master_, F_SETFL, fcntl(master_, F_GETFL) | O_NONBLOCK);
    fcntl(master_, F_SETFD, FD_CLOEXEC);
    dirty_ = true;
    return true;
}

bool TermPane::pump() {
    if (master_ < 0) return false;
    std::array<char, 16384> buf{};
    while (true) {
        const ssize_t n = ::read(master_, buf.data(), buf.size());
        if (n > 0) {
            vterm_input_write(vt_, buf.data(), static_cast<std::size_t>(n));
            vterm_screen_flush_damage(screen_);
            dirty_ = true;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) return true;
        // EOF or EIO: the slave side closed, so the program exited.
        close_master();
        reap();
        return false;
    }
}

bool TermPane::reap() {
    if (pid_ <= 0) return false;
    int status = 0;
    const pid_t r = waitpid(pid_, &status, WNOHANG);
    if (r == pid_) {
        exit_status_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        pid_ = -1;
        close_master();
        return false;
    }
    return true;
}

void TermPane::close_master() {
    if (master_ >= 0) {
        ::close(master_);
        master_ = -1;
    }
}

void TermPane::resize(int rows, int cols) {
    rows = std::max(1, rows);
    cols = std::max(1, cols);
    if (rows == rows_ && cols == cols_) return;
    rows_ = rows;
    cols_ = cols;
    if (vt_ != nullptr) vterm_set_size(vt_, rows_, cols_);
    if (master_ >= 0) {
        winsize ws{};
        ws.ws_row = static_cast<unsigned short>(rows_);
        ws.ws_col = static_cast<unsigned short>(cols_);
        ioctl(master_, TIOCSWINSZ, &ws);
    }
    dirty_ = true;
}

void TermPane::write_master(std::string_view bytes) {
    while (master_ >= 0 && !bytes.empty()) {
        const ssize_t n = ::write(master_, bytes.data(), bytes.size());
        if (n > 0) {
            bytes.remove_prefix(static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && errno == EAGAIN) {
            pollfd pfd{.fd = master_, .events = POLLOUT, .revents = 0};
            if (poll(&pfd, 1, 200) <= 0) return; // the program stopped reading; drop input
            continue;
        }
        return;
    }
}

void TermPane::on_output(const char* bytes, std::size_t len) { write_master({bytes, len}); }

void TermPane::send_bytes(std::string_view bytes) { write_master(bytes); }

void TermPane::send_key(const KeyEvent& ev) {
    if (vt_ == nullptr || master_ < 0) return;
    const VTermKey key = to_vterm_key(ev.key);
    if (key != VTERM_KEY_NONE) {
        // Let libvterm encode keys whose bytes depend on modes the program set
        // (application cursor keys and the like).
        vterm_keyboard_key(vt_, key, to_vterm_mods(ev.mods));
        return;
    }
    if (!ev.raw.empty()) {
        write_master(ev.raw);
        return;
    }
    if (ev.key == Key::Char) {
        vterm_keyboard_unichar(vt_, ev.ch, to_vterm_mods(ev.mods));
    }
}

void TermPane::send_mouse(const MouseEvent& ev, int col, int row) {
    if (vt_ == nullptr || master_ < 0 || mouse_mode_ == 0) return;
    const VTermModifier mods = to_vterm_mods(ev.mods);
    vterm_mouse_move(vt_, row, col, mods);
    switch (ev.kind) {
    case MouseEvent::Kind::Press: vterm_mouse_button(vt_, ev.button + 1, true, mods); break;
    case MouseEvent::Kind::Release: vterm_mouse_button(vt_, ev.button + 1, false, mods); break;
    case MouseEvent::Kind::WheelUp: vterm_mouse_button(vt_, 4, true, mods); break;
    case MouseEvent::Kind::WheelDown: vterm_mouse_button(vt_, 5, true, mods); break;
    case MouseEvent::Kind::Drag:
    case MouseEvent::Kind::Move: break;
    }
}

void TermPane::send_paste(std::string_view text) {
    if (vt_ == nullptr || master_ < 0) return;
    vterm_keyboard_start_paste(vt_);
    write_master(text);
    vterm_keyboard_end_paste(vt_);
}

void TermPane::set_focus(bool focused) {
    if (vt_ == nullptr) return;
    VTermState* state = vterm_obtain_state(vt_);
    if (focused) {
        vterm_state_focus_in(state);
    } else {
        vterm_state_focus_out(state);
    }
}

void TermPane::on_cursor(int row, int col) {
    cursor_row_ = row;
    cursor_col_ = col;
    dirty_ = true;
}

void TermPane::on_prop(int prop, const void* value) {
    const auto* val = static_cast<const VTermValue*>(value);
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE: cursor_visible_ = val->boolean != 0; break;
    case VTERM_PROP_CURSORBLINK: cursor_blink_ = val->boolean != 0; break;
    case VTERM_PROP_CURSORSHAPE: cursor_shape_ = val->number; break;
    case VTERM_PROP_MOUSE: mouse_mode_ = val->number; break;
    case VTERM_PROP_TITLE:
        if (val->string.initial) title_buffer_.clear();
        title_buffer_.append(val->string.str, val->string.len);
        if (val->string.final) title_ = title_buffer_;
        break;
    default: break;
    }
    dirty_ = true;
}

void TermPane::on_selection(int mask, const char* bytes, std::size_t len, bool initial,
                            bool final) {
    if ((mask & VTERM_SELECTION_CLIPBOARD) == 0) return;
    if (initial) selection_.clear();
    selection_.append(bytes, len);
    // Forward the program's OSC 52 copy to the real terminal's clipboard.
    if (final) Terminal::copy_to_clipboard(selection_);
}

void TermPane::draw(const Painter& p, Rect r, Color default_fg, Color default_bg) const {
    if (screen_ == nullptr) return;
    const int rows = std::min(rows_, r.h);
    const int cols = std::min(cols_, r.w);
    VTermScreenCell cell{};
    std::string text;
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            vterm_screen_get_cell(screen_, VTermPos{.row = row, .col = col}, &cell);
            if (cell.chars[0] == UINT32_MAX) continue; // wide-char tail
            Style style{
                .fg = to_color(cell.fg, default_fg, true),
                .bg = to_color(cell.bg, default_bg, false),
            };
            if (cell.attrs.bold != 0U) style.attrs |= AttrBold;
            if (cell.attrs.italic != 0U) style.attrs |= AttrItalic;
            if (cell.attrs.underline != 0U) style.attrs |= AttrUnderline;
            if (cell.attrs.blink != 0U) style.attrs |= AttrBlink;
            if (cell.attrs.reverse != 0U) style.attrs |= AttrReverse;
            if (cell.attrs.strike != 0U) style.attrs |= AttrStrike;
            text.clear();
            if (cell.attrs.conceal == 0U) {
                for (const std::uint32_t ch : cell.chars) {
                    if (ch == 0) break;
                    unicode::append_utf8(text, static_cast<char32_t>(ch));
                }
            }
            if (text.empty() || text == "\t") text = " ";
            const int width = cell.width == 2 ? 2 : 1;
            if (r.x + col + width <= r.right()) {
                p.canvas().put(r.x + col, r.y + row, text, width, style);
            }
            if (width == 2) ++col;
        }
    }
}

std::optional<Cursor> TermPane::cursor(Rect r) const {
    if (!cursor_visible_ || master_ < 0) return std::nullopt;
    if (cursor_row_ >= r.h || cursor_col_ >= r.w) return std::nullopt;
    CursorShape shape = CursorShape::Block;
    switch (cursor_shape_) {
    case VTERM_PROP_CURSORSHAPE_UNDERLINE:
        shape = cursor_blink_ ? CursorShape::BlinkingUnderline : CursorShape::Underline;
        break;
    case VTERM_PROP_CURSORSHAPE_BAR_LEFT:
        shape = cursor_blink_ ? CursorShape::BlinkingBar : CursorShape::Bar;
        break;
    default: shape = cursor_blink_ ? CursorShape::BlinkingBlock : CursorShape::Block; break;
    }
    return Cursor{.x = r.x + cursor_col_, .y = r.y + cursor_row_, .shape = shape};
}

void TermPane::terminate() {
    // Close the master first: the terminal hangs up, so the program's exit-time writes fail
    // instead of blocking on a pty buffer nobody drains (which can wedge even SIGKILL).
    close_master();
    if (pid_ <= 0) return;
    kill(pid_, SIGHUP);
    for (int i = 0; i < 50 && reap(); ++i) usleep(10'000);
    if (pid_ > 0) {
        kill(pid_, SIGKILL);
        int status = 0;
        waitpid(pid_, &status, 0);
        pid_ = -1;
    }
}

} // namespace lv::tui
