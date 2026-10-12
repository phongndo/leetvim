#pragma once

// Internal: LeetCode's error conventions, shared by the parsers and the client.

#include "leetcode/error.hpp"

#include <string>
#include <string_view>

namespace lv::leetcode {

/// Classifies a message LeetCode sent back: "too many requests" -> RateLimited,
/// "not signed in" -> Auth, anything else -> Api.
Error message_error(std::string message);

/// Maps a non-2xx response to an Error. `authenticated` says whether cookies were sent,
/// which decides how 401/403 are explained.
Error status_error(long status, std::string_view body, bool authenticated);

} // namespace lv::leetcode
