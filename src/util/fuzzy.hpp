#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace lv::fuzzy {

/// Case-insensitive fuzzy match of `pattern` against `text`.
/// Returns a score (higher is better) or nullopt when `pattern` is not a subsequence of
/// `text`. Contiguous runs, word starts and prefix matches score higher. When `positions`
/// is given it receives the byte offsets of the matched characters.
std::optional<int> match(std::string_view pattern, std::string_view text,
                         std::vector<std::size_t>* positions = nullptr);

} // namespace lv::fuzzy
