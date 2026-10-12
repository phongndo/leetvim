#include "leetcode/response.hpp"

#include "leetcode/json.hpp"
#include "util/strings.hpp"

#include <format>

namespace lv::leetcode {

namespace {

// The human-readable message in an error body: GraphQL {"errors": [{"message"}]},
// REST {"error": ...} or Django {"detail": ...}.
std::string body_message(std::string_view body) {
    const json::Json doc = json::parse(body);
    if (doc.is_discarded()) {
        return {};
    }
    if (const json::Json* errors = json::find(doc, "errors");
        errors != nullptr && errors->is_array() && !errors->empty()) {
        return json::get_string(errors->front(), "message");
    }
    for (const char* key : {"error", "detail", "message"}) {
        if (auto message = json::get_string(doc, key); !message.empty()) {
            return message;
        }
    }
    return {};
}

} // namespace

Error message_error(std::string message) {
    const std::string lower = str::to_lower(message);
    const auto has = [&](std::string_view s) { return lower.contains(s); };
    Error::Kind kind = Error::Kind::Api;
    if (has("too many") || has("too soon") || has("too frequent") || has("rate limit")
        || has("try again later")) {
        kind = Error::Kind::RateLimited;
    } else if (has("not authenticated") || has("login") || has("log in") || has("sign in")
               || has("signed in")) {
        kind = Error::Kind::Auth;
    }
    return Error{.kind = kind, .message = std::move(message)};
}

Error status_error(long status, std::string_view body, bool authenticated) {
    const bool cloudflare =
        body.contains("challenges.cloudflare.com") || body.contains("Just a moment...");
    std::string detail = cloudflare ? std::string{} : body_message(body);
    Error error;
    if (status == 429) {
        error = Error{
            .kind = Error::Kind::RateLimited,
            .message = "LeetCode is rate limiting requests; wait a moment and retry",
        };
    } else if (status == 401 || status == 403 || status == 499) {
        // 499 is what LeetCode answers when the CSRF token does not match the session.
        std::string message;
        if (!authenticated) {
            message = "Not signed in to LeetCode";
        } else if (cloudflare) {
            message = "LeetCode (Cloudflare) blocked the request; the session cookies may have "
                      "expired, sign in again";
        } else {
            message = "LeetCode rejected the session cookies; they have probably expired, sign "
                      "in again";
        }
        error = Error{.kind = Error::Kind::Auth, .message = std::move(message)};
    } else if (status >= 400 && status < 500 && !detail.empty()) {
        error = message_error(std::move(detail));
    } else {
        error = Error{
            .kind = Error::Kind::Http,
            .message = detail.empty()
                           ? std::format("LeetCode returned HTTP {}", status)
                           : std::format("LeetCode returned HTTP {}: {}", status, detail),
        };
    }
    error.http_status = status;
    return error;
}

} // namespace lv::leetcode
