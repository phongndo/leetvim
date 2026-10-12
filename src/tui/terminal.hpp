#pragma once

#include <string_view>

namespace lv::tui {

struct TermSize {
    int width = 80;
    int height = 24;
};

/// Owns the controlling terminal while the TUI runs: raw mode, the alternate screen,
/// mouse/paste/focus reporting, and a self-pipe that turns SIGWINCH/SIGTERM/SIGHUP/SIGCONT
/// into readable bytes for the event loop. The terminal is restored on leave(), on
/// destruction and on fatal signals.
class Terminal {
public:
    struct Options {
        bool mouse = true;
    };

    Terminal() = default;
    ~Terminal();
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;
    Terminal(Terminal&&) = delete;
    Terminal& operator=(Terminal&&) = delete;

    /// Returns false if stdin/stdout is not a terminal.
    bool enter(Options options);
    void leave();
    [[nodiscard]] bool active() const { return active_; }

    /// Restores the terminal so another program can use it; resume() takes it back.
    void suspend() const;
    void resume();
    /// Ctrl-Z: suspend, stop the process, resume once continued.
    void stop_process();

    [[nodiscard]] static TermSize size();
    [[nodiscard]] static bool truecolor_supported();
    static void write(std::string_view bytes);

    [[nodiscard]] static int input_fd();
    /// Readable whenever a signal arrived; read_signal() returns its number (or 0).
    [[nodiscard]] static int signal_fd();
    static int read_signal();

    /// Sets the window title (OSC 2).
    static void set_title(std::string_view title);
    /// Copies text to the system clipboard via OSC 52.
    static void copy_to_clipboard(std::string_view text);

private:
    void apply_modes() const;

    bool active_ = false;
    Options options_{};
};

} // namespace lv::tui
