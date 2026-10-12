#pragma once

#include "leetcode/credentials.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Finds the LeetCode session a browser already has, so linking an account needs no
// copy-pasting: Chromium-based browsers (Chrome, Helium, Arc, Brave, Edge, …), Firefox-based
// ones (Firefox, Zen, LibreWolf, …) and WebKit ones (Safari, Orion).
namespace lv::leetcode {

struct BrowserSession {
    std::string browser; // "Chrome"
    std::string profile; // "Default", "Work"
    Credentials creds;   // creds.source identifies browser and profile for later refreshes
    std::chrono::system_clock::time_point expires;

    [[nodiscard]] std::string label() const; // "Chrome · Work"
};

struct BrowserScan {
    std::vector<BrowserSession> sessions; // newest expiry first
    std::vector<std::string> problems;    // why a store could not be read, for the user
    std::vector<std::string> searched;    // browsers that were looked at
};

struct ScanOptions {
    std::string domain = "leetcode.com";
    /// Restrict to one source: a browser name ("helium") or a Credentials::source value.
    std::string only;
    /// Called before slow or prompting steps ("unlocking Helium's cookie key…").
    std::function<void(std::string_view)> on_status;
};

/// Reads session cookies for `options.domain` from every installed browser. Chromium stores
/// are decrypted with the browser's key from the macOS Keychain (the first access shows a
/// system prompt; "Always Allow" makes later refreshes silent) or libsecret on Linux. Profiles
/// without a LeetCode cookie are skipped before any key is requested.
BrowserScan scan_browsers(const ScanOptions& options = {});

// Building blocks, exposed for tests.
namespace browser_detail {

struct RawCookie {
    std::string host;
    std::string name;
    std::string value;
    std::int64_t expires = 0; // unix seconds; 0 for session cookies
};

/// True if a cookie for `host` is sent to `domain` ("leetcode.com", ".leetcode.com").
bool host_matches(std::string_view host, std::string_view domain);

/// AES-128-CBC key Chromium derives from its stored password.
std::string chromium_key(std::string_view password, int iterations);
/// Decrypts a "v10"/"v11" cookie value. Cookie databases at schema version 24 and later
/// prefix the plaintext with a SHA-256 of the host, which `strip_host_hash` removes.
std::optional<std::string> chromium_decrypt(std::string_view encrypted, std::string_view key,
                                            bool strip_host_hash);
/// The inverse, for tests.
std::string chromium_encrypt(std::string_view plaintext, std::string_view key,
                             std::string_view prefix = "v10");

/// Parses Safari's Cookies.binarycookies format.
std::vector<RawCookie> parse_binarycookies(std::string_view data);

/// Reads cookies for `domain` from a SQLite cookie store (Chromium or Firefox schema).
/// Chromium rows keep their encrypted bytes in `value`; `schema_version` reports meta.version.
std::vector<RawCookie> read_chromium_db(const std::filesystem::path& db, std::string_view domain,
                                        int& schema_version, std::string& error);
std::vector<RawCookie> read_firefox_db(const std::filesystem::path& db, std::string_view domain,
                                       std::string& error);

} // namespace browser_detail

} // namespace lv::leetcode
