#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace lv::core {

/// Splits a command line with shell-like quoting ("nvim -u 'my init.lua'").
std::vector<std::string> split_command(std::string_view command);

/// The editor command: `configured` if set, else the first of nvim, vim, vi on PATH,
/// else $VISUAL / $EDITOR. Empty if nothing is available.
std::vector<std::string> resolve_editor(std::string_view configured);

/// True for vi-family editors, which understand the keys leetvim sends to save files.
bool is_vim_like(const std::vector<std::string>& argv);

/// Searches PATH for an executable.
std::string find_executable(std::string_view name);

} // namespace lv::core
