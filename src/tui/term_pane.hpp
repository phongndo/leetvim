#pragma once

#include "tui/canvas.hpp"
#include "tui/input.hpp"
#include "tui/painter.hpp"
#include "tui/renderer.hpp"

#include <sys/types.h>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct VTerm;
struct VTermScreen;

namespace lv::tui {

/// Runs a program (the user's editor) inside a pseudo-terminal and emulates its screen
/// with libvterm, so it can be drawn inside a pane of the TUI.
class TermPane {
public:
    struct Options {
        std::vector<std::string> argv;
        std::filesystem::path cwd;
        std::vector<std::pair<std::string, std::string>> env; // added to the environment
        int rows = 24;
        int cols = 80;
    };

    TermPane() = default;
    ~TermPane();
    TermPane(const TermPane&) = delete;
    TermPane& operator=(const TermPane&) = delete;
    TermPane(TermPane&&) = delete;
    TermPane& operator=(TermPane&&) = delete;

    /// Starts the program. On failure returns false and sets `error`.
    bool spawn(const Options& options, std::string& error);

    /// Pseudo-terminal master to poll for readability; -1 when not running.
    [[nodiscard]] int fd() const { return master_; }
    [[nodiscard]] bool running() const { return master_ >= 0; }
    [[nodiscard]] std::optional<int> exit_status() const { return exit_status_; }

    /// Reads pending output into the emulator. Returns false once the program has exited.
    bool pump();
    /// Reaps the child if it exited (call on SIGCHLD). Returns false once it has exited.
    bool reap();

    void resize(int rows, int cols);
    [[nodiscard]] int rows() const { return rows_; }
    [[nodiscard]] int cols() const { return cols_; }

    void send_key(const KeyEvent& ev);
    /// `col`/`row` are relative to the pane.
    void send_mouse(const MouseEvent& ev, int col, int row);
    void send_paste(std::string_view text);
    void send_bytes(std::string_view bytes);
    void set_focus(bool focused);

    /// Draws the emulated screen at r.x/r.y (clipped to r). Cells using the program's
    /// default colors get `default_fg`/`default_bg`.
    void draw(const Painter& p, Rect r, Color default_fg, Color default_bg) const;
    /// Hardware cursor for a pane drawn at r, if the program shows one.
    [[nodiscard]] std::optional<Cursor> cursor(Rect r) const;

    [[nodiscard]] const std::string& title() const { return title_; }
    [[nodiscard]] bool wants_mouse() const { return mouse_mode_ != 0; }
    /// True if the screen changed since the last call.
    bool take_dirty() { return std::exchange(dirty_, false); }

    /// Asks the program to exit (SIGHUP, then SIGTERM/SIGKILL if it lingers) and reaps it.
    void terminate();

    // libvterm callbacks.
    void on_output(const char* bytes, std::size_t len);
    void on_damage() { dirty_ = true; }
    void on_cursor(int row, int col);
    void on_prop(int prop, const void* value);
    void on_selection(int mask, const char* bytes, std::size_t len, bool initial, bool final);

private:
    void close_master();
    void write_master(std::string_view bytes);

    VTerm* vt_ = nullptr;
    VTermScreen* screen_ = nullptr;
    int master_ = -1;
    pid_t pid_ = -1;
    std::optional<int> exit_status_;
    int rows_ = 0;
    int cols_ = 0;
    int cursor_row_ = 0;
    int cursor_col_ = 0;
    bool cursor_visible_ = true;
    bool cursor_blink_ = false;
    int cursor_shape_ = 1;
    int mouse_mode_ = 0;
    bool dirty_ = true;
    std::string title_;
    std::string title_buffer_;
    std::string selection_;
    std::vector<char> selection_scratch_;
};

} // namespace lv::tui
