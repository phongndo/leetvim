#include "core/account.hpp"

#include "leetcode/client.hpp"

#include <algorithm>
#include <format>

namespace lv::core {

std::string cookie_domain(std::string_view base_url) {
    if (const auto scheme = base_url.find("://"); scheme != std::string_view::npos) {
        base_url.remove_prefix(scheme + 3);
    }
    base_url = base_url.substr(0, base_url.find('/'));
    if (base_url.starts_with("www.")) base_url.remove_prefix(4);
    return std::string(base_url);
}

leetcode::Result<Account> verify_account(const leetcode::Credentials& creds,
                                         const std::string& base_url, std::string from) {
    const leetcode::Client client(creds, base_url);
    auto status = client.user_status();
    if (!status) return std::unexpected(status.error());
    if (!status->signed_in) {
        return std::unexpected(
            leetcode::Error{.kind = leetcode::Error::Kind::Auth,
                            .message = "LeetCode says this session is signed out"});
    }
    return Account{.creds = creds, .status = std::move(*status), .from = std::move(from)};
}

Discovery discover_accounts(const std::string& base_url, const leetcode::ScanOptions& options) {
    auto scan = leetcode::scan_browsers(options);
    Discovery out{
        .accounts = {}, .notes = std::move(scan.problems), .searched = std::move(scan.searched)};
    for (const auto& session : scan.sessions) {
        auto account = verify_account(session.creds, base_url, session.label());
        if (!account) {
            out.notes.push_back(std::format("{}: {}", session.label(), account.error().message));
            continue;
        }
        // Several profiles may hold the same account; the first (freshest) wins.
        const bool seen = std::ranges::any_of(out.accounts, [&](const Account& a) {
            return a.status.username == account->status.username;
        });
        if (!seen) out.accounts.push_back(std::move(*account));
    }
    return out;
}

std::optional<Account> refresh_account(const leetcode::Credentials& creds,
                                       const std::string& base_url) {
    if (creds.source.empty()) return std::nullopt;
    const auto found = discover_accounts(
        base_url, {.domain = cookie_domain(base_url), .only = creds.source, .on_status = {}});
    if (found.accounts.empty()) return std::nullopt;
    return found.accounts.front();
}

} // namespace lv::core
