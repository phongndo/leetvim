#include "tui/terminal.hpp"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <string>

namespace lv::tui {

namespace {

// Signal handlers may only touch async-signal-safe state, hence these globals.
termios g_saved{};
volatile std::sig_atomic_t g_raw = 0;
std::array<int, 2> g_signal_pipe{-1, -1};

constexpr std::string_view enter_seq = "\x1b[?1049h" // alternate screen
                                       "\x1b[?1l"    // normal (not application) cursor keys
                                       "\x1b[?2004h" // bracketed paste
                                       "\x1b[?1004h" // focus events
                                       "\x1b[?25l";
constexpr std::string_view mouse_on = "\x1b[?1002h\x1b[?1006h";
constexpr std::string_view mouse_off = "\x1b[?1002l\x1b[?1006l";
constexpr std::string_view leave_seq = "\x1b[?1002l\x1b[?1006l" // mouse off
                                       "\x1b[?2004l\x1b[?1004l"
                                       "\x1b[0 q" // default cursor shape
                                       "\x1b[0m\x1b[?25h"
                                       "\x1b[?1049l";

void write_all(int fd, std::string_view bytes) {
    while (!bytes.empty()) {
        const ssize_t n = ::write(fd, bytes.data(), bytes.size());
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return;
        }
        bytes.remove_prefix(static_cast<std::size_t>(n));
    }
}

void restore_raw() {
    if (g_raw == 0) return;
    write_all(STDOUT_FILENO, leave_seq);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved);
    g_raw = 0;
}

extern "C" void forward_signal(int sig) {
    const int saved_errno = errno;
    if (g_signal_pipe[1] >= 0) {
        const auto byte = static_cast<char>(sig);
        [[maybe_unused]] const ssize_t n = ::write(g_signal_pipe[1], &byte, 1);
    }
    errno = saved_errno;
}

extern "C" void fatal_signal(int sig) {
    restore_raw();
    (void)std::signal(sig, SIG_DFL);
    (void)std::raise(sig);
}

void install_handlers() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    if (pipe(g_signal_pipe.data()) == 0) {
        for (const int fd : g_signal_pipe) {
            (void)fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
            (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
        }
    }
    struct sigaction sa{};
    sa.sa_handler = forward_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    for (const int sig : {SIGWINCH, SIGTERM, SIGHUP, SIGCONT, SIGCHLD})
        (void)sigaction(sig, &sa, nullptr);
    (void)std::signal(SIGPIPE, SIG_IGN);
    for (const int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL})
        (void)std::signal(sig, fatal_signal);
    (void)std::atexit(restore_raw);
}

} // namespace

Terminal::~Terminal() { leave(); }

bool Terminal::enter(Options options) {
    if (active_) return true;
    if (isatty(STDIN_FILENO) == 0 || isatty(STDOUT_FILENO) == 0) return false;
    options_ = options;
    install_handlers();
    if (tcgetattr(STDIN_FILENO, &g_saved) != 0) return false;
    apply_modes();
    active_ = true;
    return true;
}

void Terminal::apply_modes() const {
    termios raw = g_saved;
    raw.c_iflag &= ~static_cast<tcflag_t>(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~static_cast<tcflag_t>(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    g_raw = 1;
    std::string seq(enter_seq);
    if (options_.mouse) seq += mouse_on;
    write_all(STDOUT_FILENO, seq);
}

void Terminal::set_mouse(bool on) {
    options_.mouse = on;
    if (active_) write_all(STDOUT_FILENO, on ? mouse_on : mouse_off);
}

void Terminal::leave() {
    if (!active_) return;
    restore_raw();
    active_ = false;
}

void Terminal::suspend() const {
    if (active_) restore_raw();
}

void Terminal::resume() {
    if (active_) apply_modes();
}

void Terminal::stop_process() {
    suspend();
    (void)std::raise(SIGTSTP);
    // Execution continues here after SIGCONT.
    resume();
}

TermSize Terminal::size() {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        return {.width = ws.ws_col, .height = ws.ws_row};
    }
    return {};
}

bool Terminal::truecolor_supported() {
    const char* colorterm = std::getenv("COLORTERM");
    if (colorterm == nullptr) return false;
    const std::string_view v(colorterm);
    return v == "truecolor" || v == "24bit";
}

void Terminal::write(std::string_view bytes) { write_all(STDOUT_FILENO, bytes); }

int Terminal::input_fd() { return STDIN_FILENO; }

int Terminal::signal_fd() { return g_signal_pipe[0]; }

int Terminal::read_signal() {
    char byte = 0;
    if (g_signal_pipe[0] >= 0 && ::read(g_signal_pipe[0], &byte, 1) == 1) return byte;
    return 0;
}

void Terminal::set_title(std::string_view title) {
    std::string seq = "\x1b]2;";
    seq += title;
    seq += '\a';
    write_all(STDOUT_FILENO, seq);
}

void Terminal::copy_to_clipboard(std::string_view text) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    std::size_t i = 0;
    while (i + 2 < text.size()) {
        const auto n = (static_cast<unsigned char>(text[i]) << 16U)
                       | (static_cast<unsigned char>(text[i + 1]) << 8U)
                       | static_cast<unsigned char>(text[i + 2]);
        encoded += alphabet[(n >> 18U) & 63U];
        encoded += alphabet[(n >> 12U) & 63U];
        encoded += alphabet[(n >> 6U) & 63U];
        encoded += alphabet[n & 63U];
        i += 3;
    }
    if (i < text.size()) {
        unsigned n = static_cast<unsigned char>(text[i]) << 16U;
        if (i + 1 < text.size()) n |= static_cast<unsigned char>(text[i + 1]) << 8U;
        encoded += alphabet[(n >> 18U) & 63U];
        encoded += alphabet[(n >> 12U) & 63U];
        encoded += i + 1 < text.size() ? alphabet[(n >> 6U) & 63U] : '=';
        encoded += '=';
    }
    write_all(STDOUT_FILENO, "\x1b]52;c;" + encoded + "\a");
}

} // namespace lv::tui
