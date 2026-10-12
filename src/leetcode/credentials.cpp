#include "leetcode/credentials.hpp"

#include "leetcode/json.hpp"
#include "util/paths.hpp"
#include "util/strings.hpp"

#include <cstdlib>
#include <exception>
#include <system_error>

namespace lv::leetcode {

namespace {

constexpr std::string_view quotes_and_space = " \t\r\n\"'`";

std::string_view strip(std::string_view s) {
    const auto first = s.find_first_not_of(quotes_and_space);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(quotes_and_space);
    return s.substr(first, last - first + 1);
}

/// Drops a leading "Cookie:" (any case), e.g. from a copied request header.
std::string_view drop_cookie_prefix(std::string_view s) {
    constexpr std::string_view prefix = "cookie:";
    if (s.size() >= prefix.size() && str::iequals(s.substr(0, prefix.size()), prefix)) {
        return strip(s.substr(prefix.size()));
    }
    return s;
}

std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(strip(value)) : std::string{};
}

} // namespace

std::optional<Credentials> parse_cookie_string(std::string_view text) {
    Credentials creds;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find_first_of(";\n", start);
        const auto part = drop_cookie_prefix(strip(text.substr(start, end - start)));
        if (const auto eq = part.find('='); eq != std::string_view::npos) {
            const auto name = strip(part.substr(0, eq));
            const auto value = strip(part.substr(eq + 1));
            if (name == "LEETCODE_SESSION" && !value.empty()) {
                creds.session = value;
            }
            if (name == "csrftoken" && !value.empty()) {
                creds.csrf = value;
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    if (creds.empty()) {
        return std::nullopt;
    }
    return creds;
}

std::filesystem::path credentials_path() { return paths::data_dir() / "credentials"; }

Credentials load_credentials() {
    Credentials creds;
    std::string text;
    if (paths::read_file(credentials_path(), text)) {
        const json::Json doc = json::parse(text);
        creds.session = json::get_string(doc, "LEETCODE_SESSION");
        creds.csrf = json::get_string(doc, "csrftoken");
        creds.source = json::get_string(doc, "source");
    }
    if (auto session = env("LEETCODE_SESSION"); !session.empty()) {
        creds.session = session;
    }
    if (auto csrf = env("LEETCODE_CSRF_TOKEN"); !csrf.empty()) {
        creds.csrf = csrf;
    }
    return creds;
}

bool save_credentials(const Credentials& creds) {
    try {
        json::Json doc = {{"LEETCODE_SESSION", creds.session}, {"csrftoken", creds.csrf}};
        if (!creds.source.empty()) doc["source"] = creds.source;
        const auto text = doc.dump(2, ' ', false, json::Json::error_handler_t::replace) + '\n';
        using std::filesystem::perms;
        return paths::write_file(credentials_path(), text, perms::owner_read | perms::owner_write);
    } catch (const std::exception&) {
        return false;
    }
}

bool clear_credentials() {
    std::error_code ec;
    std::filesystem::remove(credentials_path(), ec);
    return !ec;
}

} // namespace lv::leetcode
