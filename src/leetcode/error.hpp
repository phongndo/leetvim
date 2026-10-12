#pragma once

#include <cstdint>
#include <expected>
#include <string>

namespace lv::leetcode {

struct Error {
    enum class Kind : std::uint8_t {
        Network,     // DNS, TLS, timeouts, connection failures
        Http,        // unexpected HTTP status
        Auth,        // missing/expired cookies (401/403, or "not signed in")
        RateLimited, // HTTP 429 or LeetCode's "too many requests" responses
        Parse,       // response JSON did not have the expected shape
        Api,         // LeetCode returned an error message (GraphQL errors, etc.)
        Cancelled,   // stop was requested
    };

    Kind kind = Kind::Api;
    std::string message;
    long http_status = 0;
};

template <typename T>
using Result = std::expected<T, Error>;

} // namespace lv::leetcode
