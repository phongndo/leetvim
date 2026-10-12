#pragma once

// Internal: lenient accessors over nlohmann::json. Missing keys, nulls and mismatched types
// yield defaults; numbers that arrive as strings ("12", "58.0%") are converted. Never throw.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lv::leetcode::json {

using Json = nlohmann::json;

/// Parses `text`; returns a discarded value (is_discarded()) on malformed input.
Json parse(std::string_view text);
/// The value at `key`, or nullptr if `j` is not an object or the value is missing/null.
const Json* find(const Json& j, const char* key);

std::string to_text(const Json& v); // strings as-is, numbers formatted, others ""
std::optional<std::int64_t> to_int(const Json& v);
std::optional<double> to_double(const Json& v);

std::string get_string(const Json& j, const char* key);
std::int64_t get_i64(const Json& j, const char* key, std::int64_t fallback = 0);
int get_int(const Json& j, const char* key, int fallback = 0);
std::optional<double> get_double(const Json& j, const char* key);
bool get_bool(const Json& j, const char* key);
/// Array of strings (non-string elements become ""); empty if absent.
std::vector<std::string> get_strings(const Json& j, const char* key);
/// A field holding JSON encoded as a string (LeetCode's `stats`, `metaData`, ...).
/// Returns the decoded value, the field itself if it is already structured, or null.
Json get_nested(const Json& j, const char* key);

} // namespace lv::leetcode::json
