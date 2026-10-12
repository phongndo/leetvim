#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lv::str {

std::string_view trim(std::string_view s);
std::vector<std::string_view> split(std::string_view s, char sep);
/// Splits on '\n', dropping a trailing '\r' from each line.
std::vector<std::string_view> lines(std::string_view s);
std::string join(std::span<const std::string> parts, std::string_view sep);

std::string to_lower(std::string_view s);
bool iequals(std::string_view a, std::string_view b);
bool icontains(std::string_view haystack, std::string_view needle);
std::string replace_all(std::string_view s, std::string_view from, std::string_view to);

/// 1234567 -> "1.2M", 23800 -> "23.8K".
std::string human_count(std::int64_t n);
/// Formats `value` (0-100) with one decimal: "53.7%".
std::string percent(double value);

} // namespace lv::str
