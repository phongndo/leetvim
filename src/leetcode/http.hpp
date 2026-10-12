#pragma once

// Internal: a minimal blocking HTTP client over libcurl. Each thread reuses its own easy
// handle, so keep-alive connections survive between requests made on the same thread.

#include "leetcode/error.hpp"

#include <stop_token>
#include <string>
#include <vector>

namespace lv::leetcode::http {

struct Request {
    std::string url;
    std::vector<std::string> headers; // "Name: value"
    bool post = false;
    std::string body; // sent when post is true
};

struct Response {
    long status = 0;
    std::string body;
};

/// Only transport failures are errors (Network, or Cancelled once `stop` is requested or
/// abort_all() was called); every HTTP status comes back as a Response.
Result<Response> perform(const Request& request, const std::stop_token& stop = {});

/// Makes every in-flight and future request in the process fail with Cancelled. Irreversible.
void abort_all();
/// Stopped once abort_all() has been called.
std::stop_token abort_token();

} // namespace lv::leetcode::http
