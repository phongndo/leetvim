#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace lv::leetcode {

/// The two browser cookies LeetCode needs for authenticated requests.
struct Credentials {
    std::string session; // LEETCODE_SESSION
    std::string csrf;    // csrftoken
    std::string source;  // browser profile it was imported from ("Helium/Default"); empty if pasted

    [[nodiscard]] bool empty() const { return session.empty() || csrf.empty(); }
};

/// Extracts LEETCODE_SESSION and csrftoken from a pasted cookie string. Accepts a raw
/// `Cookie:` header value ("a=b; LEETCODE_SESSION=...; csrftoken=..."), with or without the
/// "Cookie:" prefix, surrounding quotes or newlines. Returns nullopt if either is missing.
std::optional<Credentials> parse_cookie_string(std::string_view text);

/// $XDG_DATA_HOME/leetvim/credentials
std::filesystem::path credentials_path();

/// Loads credentials: LEETCODE_SESSION / LEETCODE_CSRF_TOKEN environment variables take
/// precedence over the credentials file. Returns empty credentials if neither is present.
Credentials load_credentials();
/// Saves to credentials_path() with mode 0600 (including where they came from).
bool save_credentials(const Credentials& creds);
bool clear_credentials();

} // namespace lv::leetcode
