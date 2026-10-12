#pragma once

#include <optional>
#include <string>
#include <vector>

namespace lv::process {

/// Runs a program found on PATH, without a shell, and returns its stdout (trailing newline
/// removed); nullopt if it can't start or exits non-zero. Its stderr is discarded.
std::optional<std::string> capture(const std::vector<std::string>& argv);

} // namespace lv::process
