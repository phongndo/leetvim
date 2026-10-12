#include "tui/term_pane.hpp"

#include <doctest/doctest.h>

#include <poll.h>

#include <chrono>
#include <string>

using namespace lv::tui;

namespace {

std::string row_text(const Canvas& c, int y) {
    std::string out;
    for (int x = 0; x < c.width(); ++x) out += c.at(x, y).text;
    return out;
}

/// Pumps the pane until the program exits or the timeout passes.
void run_to_exit(TermPane& pane) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (pane.running() && std::chrono::steady_clock::now() < deadline) {
        pollfd pfd{.fd = pane.fd(), .events = POLLIN, .revents = 0};
        poll(&pfd, 1, 50);
        pane.pump();
        pane.reap();
    }
}

} // namespace

TEST_CASE("term pane runs a program and emulates its screen") {
    TermPane pane;
    std::string error;
    const bool ok = pane.spawn(
        {.argv = {"/bin/sh", "-c",
                  "printf 'hello \\033[31mred\\033[0m\\r\\nsize %s' \"$(stty size)\"; "
                  "printf '\\033]2;my title\\007'; printf '\\nenv=%s' \"$LEETVIM_TEST\""},
         .cwd = "/",
         .env = {{"LEETVIM_TEST", "yes"}},
         .rows = 5,
         .cols = 30},
        error);
    REQUIRE_MESSAGE(ok, error);
    run_to_exit(pane);
    CHECK_FALSE(pane.running());
    CHECK(pane.exit_status() == 0);
    CHECK(pane.title() == "my title");

    Canvas canvas(30, 5);
    pane.draw(Painter(canvas, canvas.bounds()), canvas.bounds(), Color::none(), Color::none());
    CHECK(row_text(canvas, 0).starts_with("hello red"));
    CHECK(canvas.at(6, 0).style.fg == Color::indexed(1));
    CHECK(canvas.at(0, 0).style.fg.is_default());
    CHECK(row_text(canvas, 1).starts_with("size 5 30"));
    CHECK(row_text(canvas, 2).starts_with("env=yes"));
}

TEST_CASE("term pane forwards keys to the program") {
    TermPane pane;
    std::string error;
    REQUIRE(pane.spawn({.argv = {"/bin/sh", "-c", "read line; printf 'got:%s' \"$line\""},
                        .cwd = {},
                        .env = {},
                        .rows = 3,
                        .cols = 20},
                       error));
    pane.send_key(KeyEvent{.key = Key::Char, .ch = 'h', .mods = ModNone, .raw = "h"});
    pane.send_key(KeyEvent{.key = Key::Char, .ch = 'i', .mods = ModNone, .raw = "i"});
    pane.send_key(KeyEvent{.key = Key::Enter, .ch = 0, .mods = ModNone, .raw = "\r"});
    run_to_exit(pane);
    Canvas canvas(20, 3);
    pane.draw(Painter(canvas, canvas.bounds()), canvas.bounds(), Color::none(), Color::none());
    CHECK(row_text(canvas, 1).starts_with("got:hi"));
}

TEST_CASE("term pane reports exec failure via exit status") {
    TermPane pane;
    std::string error;
    REQUIRE(pane.spawn(
        {.argv = {"/definitely/not/a/program"}, .cwd = {}, .env = {}, .rows = 3, .cols = 10},
        error));
    run_to_exit(pane);
    CHECK(pane.exit_status() == 127);
}

TEST_CASE("terminate does not hang on a program that floods output on hangup") {
    TermPane pane;
    std::string error;
    // Ignores SIGHUP's default action and keeps writing, like an editor restoring the screen.
    REQUIRE(pane.spawn({.argv = {"/bin/sh", "-c",
                                 "trap 'while :; do printf xxxxxxxxxxxxxxxx; done' HUP; "
                                 "while :; do sleep 1; done"},
                        .cwd = {},
                        .env = {},
                        .rows = 3,
                        .cols = 10},
                       error));
    const auto start = std::chrono::steady_clock::now();
    pane.terminate();
    CHECK_FALSE(pane.running());
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(3));
}
