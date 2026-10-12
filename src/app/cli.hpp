#pragma once

namespace lv::app {

/// Entry point: parses the command line and runs the TUI or a one-shot command.
int run_cli(int argc, const char* const* argv);

} // namespace lv::app
