#include "leetcode/credentials.hpp"
#include "support.hpp"
#include "util/paths.hpp"

#include <doctest/doctest.h>

#include <filesystem>

using namespace lv::leetcode;
using lv::test::ScopedEnv;
using lv::test::TempDir;

namespace fs = std::filesystem;

namespace {

/// Parses `text`, requiring both cookies to be found.
Credentials cookies(std::string_view text) {
    const auto creds = parse_cookie_string(text);
    REQUIRE(creds.has_value());
    return creds.value_or(Credentials{});
}

} // namespace

TEST_CASE("cookie strings") {
    const auto simple = cookies("LEETCODE_SESSION=abc; csrftoken=def");
    CHECK(simple.session == "abc");
    CHECK(simple.csrf == "def");

    const auto browser =
        cookies("gr_user_id=1f2e; csrftoken=TOKEN123; _ga=GA1.2.3; "
                "LEETCODE_SESSION=eyJhbGciOiJIUzI1NiJ9.eyJ1c2VyIjoiZHAifQ.sig=; INGRESSCOOKIE=x|y");
    CHECK(browser.session == "eyJhbGciOiJIUzI1NiJ9.eyJ1c2VyIjoiZHAifQ.sig=");
    CHECK(browser.csrf == "TOKEN123");

    const auto header = cookies("Cookie: csrftoken=c1; LEETCODE_SESSION=s1");
    CHECK(header.session == "s1");
    CHECK(header.csrf == "c1");

    const auto lower = cookies("cookie:LEETCODE_SESSION=s2;csrftoken=c2");
    CHECK(lower.session == "s2");
    CHECK(lower.csrf == "c2");

    const auto quoted = cookies("  \"LEETCODE_SESSION=\"s3\"; csrftoken='c3'\"\n");
    CHECK(quoted.session == "s3");
    CHECK(quoted.csrf == "c3");

    const auto lines = cookies("\n LEETCODE_SESSION = s4 \r\n csrftoken=c4\r\n");
    CHECK(lines.session == "s4");
    CHECK(lines.csrf == "c4");

    CHECK_FALSE(parse_cookie_string("LEETCODE_SESSION=only").has_value());
    CHECK_FALSE(parse_cookie_string("csrftoken=only").has_value());
    CHECK_FALSE(parse_cookie_string("LEETCODE_SESSION=; csrftoken=").has_value());
    CHECK_FALSE(parse_cookie_string("").has_value());
    CHECK_FALSE(parse_cookie_string("garbage without equals").has_value());
}

TEST_CASE("credentials file round trip") {
    const TempDir tmp;
    const ScopedEnv data_home("XDG_DATA_HOME", tmp.path().c_str());
    const ScopedEnv no_session("LEETCODE_SESSION", nullptr);
    const ScopedEnv no_csrf("LEETCODE_CSRF_TOKEN", nullptr);

    CHECK(credentials_path() == tmp.path() / "leetvim" / "credentials");
    CHECK(load_credentials().empty());

    REQUIRE(save_credentials({.session = "sess", .csrf = "tok"}));
    const auto perms = fs::status(credentials_path()).permissions();
    CHECK((perms & fs::perms::all) == (fs::perms::owner_read | fs::perms::owner_write));

    const auto loaded = load_credentials();
    CHECK(loaded.session == "sess");
    CHECK(loaded.csrf == "tok");

    {
        const ScopedEnv session("LEETCODE_SESSION", "env-sess");
        const ScopedEnv csrf("LEETCODE_CSRF_TOKEN", "env-tok");
        const auto from_env = load_credentials();
        CHECK(from_env.session == "env-sess");
        CHECK(from_env.csrf == "env-tok");
    }

    CHECK(clear_credentials());
    CHECK_FALSE(fs::exists(credentials_path()));
    CHECK(load_credentials().empty());
    CHECK(clear_credentials()); // nothing to remove is fine
}

TEST_CASE("corrupt credentials file loads as empty") {
    const TempDir tmp;
    const ScopedEnv data_home("XDG_DATA_HOME", tmp.path().c_str());
    const ScopedEnv no_session("LEETCODE_SESSION", nullptr);
    const ScopedEnv no_csrf("LEETCODE_CSRF_TOKEN", nullptr);
    REQUIRE(lv::paths::write_file(credentials_path(), "{\"LEETCODE_SESSION\": "));
    CHECK(load_credentials().empty());
}
