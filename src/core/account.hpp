#pragma once

#include "leetcode/browsers.hpp"
#include "leetcode/credentials.hpp"
#include "leetcode/error.hpp"
#include "leetcode/models.hpp"

#include <optional>
#include <string>
#include <vector>

// Linking leetvim to a LeetCode account: find sessions the user's browsers already have,
// confirm them with LeetCode, and refresh them from the same browser when they expire.
namespace lv::core {

struct Account {
    leetcode::Credentials creds;
    leetcode::UserStatus status; // signed in; username, premium
    std::string from;            // "Helium · Default", or "pasted cookie"
};

struct Discovery {
    std::vector<Account> accounts;     // signed-in sessions, one per username, freshest first
    std::vector<std::string> notes;    // stores that couldn't be used, and why
    std::vector<std::string> searched; // browsers that were looked at
};

/// The cookie domain for a base URL: "https://leetcode.com" -> "leetcode.com".
std::string cookie_domain(std::string_view base_url);

/// Scans the browsers and keeps the sessions LeetCode confirms as signed in.
Discovery discover_accounts(const std::string& base_url, const leetcode::ScanOptions& options = {});

/// Confirms credentials with LeetCode.
leetcode::Result<Account> verify_account(const leetcode::Credentials& creds,
                                         const std::string& base_url, std::string from);

/// A fresh session from the browser profile `creds` was imported from, if that browser is
/// still signed in (pasted credentials have no source and can't be refreshed).
std::optional<Account> refresh_account(const leetcode::Credentials& creds,
                                       const std::string& base_url);

} // namespace lv::core
