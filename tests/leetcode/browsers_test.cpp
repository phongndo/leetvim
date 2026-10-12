#include "leetcode/browsers.hpp"
#include "util/paths.hpp"

#include <doctest/doctest.h>

#include <sqlite3.h>
#include <unistd.h>

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>

using namespace lv::leetcode;
using namespace lv::leetcode::browser_detail;
namespace fs = std::filesystem;

namespace {

std::string hex(std::string_view bytes) {
    static constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    for (const char c : bytes) {
        const auto u = static_cast<unsigned char>(c);
        out += digits[u >> 4U];
        out += digits[u & 15U];
    }
    return out;
}

struct TempDir {
    fs::path path;
    TempDir() {
        std::string tmpl = (fs::temp_directory_path() / "leetvim-browsers-XXXXXX").string();
        path = mkdtemp(tmpl.data());
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;
};

void exec(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    INFO(sql);
    REQUIRE(rc == SQLITE_OK);
}

/// A minimal Chromium cookie database.
void make_chromium_db(
    const fs::path& path, int version,
    const std::vector<std::tuple<std::string, std::string, std::string, std::int64_t>>& rows) {
    fs::create_directories(path.parent_path());
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    exec(db, "CREATE TABLE meta(key TEXT, value TEXT)");
    exec(db, "INSERT INTO meta VALUES('version', '" + std::to_string(version) + "')");
    exec(db, "CREATE TABLE cookies(host_key TEXT, name TEXT, value TEXT, encrypted_value BLOB, "
             "expires_utc INTEGER)");
    for (const auto& [host, name, encrypted, expires] : rows) {
        sqlite3_stmt* stmt = nullptr;
        REQUIRE(
            sqlite3_prepare_v2(db, "INSERT INTO cookies VALUES(?, ?, '', ?, ?)", -1, &stmt, nullptr)
            == SQLITE_OK);
        sqlite3_bind_text(stmt, 1, host.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 3, encrypted.data(), static_cast<int>(encrypted.size()),
                          SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, expires);
        REQUIRE(sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

/// A minimal Firefox cookie database.
void make_firefox_db(
    const fs::path& path,
    const std::vector<std::tuple<std::string, std::string, std::string, std::int64_t>>& rows) {
    fs::create_directories(path.parent_path());
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    exec(db, "CREATE TABLE moz_cookies(host TEXT, name TEXT, value TEXT, expiry INTEGER)");
    for (const auto& [host, name, value, expiry] : rows) {
        exec(db, std::format("INSERT INTO moz_cookies VALUES('{}', '{}', '{}', {})", host, name,
                             value, expiry));
    }
    sqlite3_close(db);
}

void put_le32(std::string& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out += static_cast<char>((v >> (8U * static_cast<unsigned>(i))) & 0xFFU);
}

void put_be32(std::string& out, std::uint32_t v) {
    for (int i = 3; i >= 0; --i)
        out += static_cast<char>((v >> (8U * static_cast<unsigned>(i))) & 0xFFU);
}

/// One binarycookies cookie record.
std::string binary_cookie(const std::string& host, const std::string& name,
                          const std::string& value, double expiry_mac) {
    const std::string path = "/";
    const std::uint32_t header = 56;
    const auto host_at = header;
    const auto name_at = host_at + static_cast<std::uint32_t>(host.size()) + 1;
    const auto path_at = name_at + static_cast<std::uint32_t>(name.size()) + 1;
    const auto value_at = path_at + static_cast<std::uint32_t>(path.size()) + 1;
    const auto size = value_at + static_cast<std::uint32_t>(value.size()) + 1;
    std::string c;
    put_le32(c, size);
    put_le32(c, 0);
    put_le32(c, 1); // flags
    put_le32(c, 0);
    put_le32(c, host_at);
    put_le32(c, name_at);
    put_le32(c, path_at);
    put_le32(c, value_at);
    c.append(8, '\0'); // end of header marker
    const auto bits = std::bit_cast<std::uint64_t>(expiry_mac);
    for (int i = 0; i < 8; ++i)
        c += static_cast<char>((bits >> (8U * static_cast<unsigned>(i))) & 0xFFU);
    c.append(8, '\0'); // creation date
    for (const auto* s : {&host, &name, &path, &value}) {
        c += *s;
        c += '\0';
    }
    return c;
}

std::string binarycookies_file(const std::vector<std::string>& cookies) {
    std::string page;
    put_le32(page, 0x00000100);
    put_le32(page, static_cast<std::uint32_t>(cookies.size()));
    auto offset = static_cast<std::uint32_t>(8 + (4 * cookies.size()) + 4);
    for (const auto& c : cookies) {
        put_le32(page, offset);
        offset += static_cast<std::uint32_t>(c.size());
    }
    put_le32(page, 0); // page footer
    for (const auto& c : cookies) page += c;
    std::string file = "cook";
    put_be32(file, 1);
    put_be32(file, static_cast<std::uint32_t>(page.size()));
    return file + page;
}

constexpr std::int64_t future = 4'102'444'800; // 2100-01-01

} // namespace

TEST_CASE("cookie hosts match the domain and its subdomains only") {
    CHECK(host_matches(".leetcode.com", "leetcode.com"));
    CHECK(host_matches("leetcode.com", "leetcode.com"));
    CHECK(host_matches("www.leetcode.com", "leetcode.com"));
    CHECK_FALSE(host_matches("notleetcode.com", "leetcode.com"));
    CHECK_FALSE(host_matches("leetcode.com.evil.io", "leetcode.com"));
}

TEST_CASE("chromium keys match the reference PBKDF2 derivation") {
    CHECK(hex(chromium_key("peanuts", 1)) == "fd621fe5a2b402539dfa147ca9272778");
    CHECK(hex(chromium_key("secret", 1003)) == "1a7404704ee35b4506624ed49171a534");
}

TEST_CASE("chromium cookie values decrypt, with and without the host hash") {
    const std::string key = chromium_key("secret", 1003);
    const std::string value = "eyJhbGciOiJIUzI1NiJ9.session";
    CHECK(chromium_decrypt(chromium_encrypt(value, key), key, false) == value);

    const std::string hashed = std::string(32, '\x7f') + value; // schema >= 24 layout
    CHECK(chromium_decrypt(chromium_encrypt(hashed, key, "v11"), key, true) == value);

    CHECK_FALSE(chromium_decrypt(chromium_encrypt(value, key), chromium_key("wrong", 1003), false)
                    .has_value());
    CHECK_FALSE(chromium_decrypt("v10short", key, false).has_value());
    CHECK_FALSE(chromium_decrypt("plain-value", key, false).has_value());
}

TEST_CASE("chromium databases are read without touching the original") {
    const TempDir dir;
    const auto db = dir.path / "Default" / "Cookies";
    const std::string key = chromium_key("secret", 1003);
    make_chromium_db(
        db, 24,
        {{".leetcode.com", "LEETCODE_SESSION", chromium_encrypt("s", key), 0},
         {"leetcode.com", "csrftoken", chromium_encrypt("c", key), 13'000'000'000'000'000},
         {".notleetcode.com", "LEETCODE_SESSION", "x", 0},
         {".github.com", "user_session", "y", 0}});
    int version = 0;
    std::string error;
    const auto cookies = read_chromium_db(db, "leetcode.com", version, error);
    CHECK(error.empty());
    CHECK(version == 24);
    REQUIRE(cookies.size() == 2);
    CHECK(cookies[0].name == "LEETCODE_SESSION");
    CHECK(cookies[0].value.starts_with("v10"));
    CHECK(cookies[1].expires == (13'000'000'000'000'000 / 1'000'000) - 11'644'473'600);
    CHECK_FALSE(fs::exists(fs::path(db.string() + "-journal")));

    const auto missing = read_chromium_db(dir.path / "nope", "leetcode.com", version, error);
    CHECK(missing.empty());
    CHECK_FALSE(error.empty());
}

TEST_CASE("firefox databases are read, including millisecond expiries") {
    const TempDir dir;
    const auto db = dir.path / "cookies.sqlite";
    make_firefox_db(db, {{".leetcode.com", "LEETCODE_SESSION", "abc", future * 1000},
                         {"leetcode.com", "csrftoken", "def", future},
                         {"example.com", "other", "zzz", future}});
    std::string error;
    const auto cookies = read_firefox_db(db, "leetcode.com", error);
    CHECK(error.empty());
    REQUIRE(cookies.size() == 2);
    CHECK(cookies[0].value == "abc");
    CHECK(cookies[0].expires == future);
    CHECK(cookies[1].expires == future);
}

TEST_CASE("safari binarycookies are parsed") {
    const auto file =
        binarycookies_file({binary_cookie(".leetcode.com", "LEETCODE_SESSION", "abc", 1e9),
                            binary_cookie("leetcode.com", "csrftoken", "def", 2e9)});
    const auto cookies = parse_binarycookies(file);
    REQUIRE(cookies.size() == 2);
    CHECK(cookies[0].host == ".leetcode.com");
    CHECK(cookies[0].name == "LEETCODE_SESSION");
    CHECK(cookies[0].value == "abc");
    CHECK(cookies[0].expires == 1'000'000'000 + 978'307'200);
    CHECK(cookies[1].value == "def");

    CHECK(parse_binarycookies("").empty());
    CHECK(parse_binarycookies("cook\xff\xff\xff\xff").empty());
    CHECK(parse_binarycookies(file.substr(0, file.size() - 20)).size() <= 2); // truncated: no crash
}

TEST_CASE("scanning finds a signed-in Firefox-family profile and skips expired ones") {
    const TempDir home;
    const char* old_home = std::getenv("HOME");
    const std::string saved = old_home != nullptr ? old_home : "";
    setenv("HOME", home.path.c_str(), 1);
#ifdef __APPLE__
    const fs::path profiles = home.path / "Library" / "Application Support" / "zen" / "Profiles";
#else
    const fs::path profiles = home.path / ".zen";
#endif
    make_firefox_db(profiles / "abc123.Default (release)" / "cookies.sqlite",
                    {{".leetcode.com", "LEETCODE_SESSION", "live-session", future},
                     {"leetcode.com", "csrftoken", "csrf-1", future}});
    make_firefox_db(profiles / "def456.Old" / "cookies.sqlite",
                    {{".leetcode.com", "LEETCODE_SESSION", "stale", 1000},
                     {"leetcode.com", "csrftoken", "csrf-2", future}});
    make_firefox_db(profiles / "ghi789.NoLeetCode" / "cookies.sqlite",
                    {{"leetcode.com", "csrftoken", "csrf-3", future}});

    std::vector<std::string> statuses;
    const auto scan =
        scan_browsers({.domain = "leetcode.com", .only = "", .on_status = [&](std::string_view s) {
                           statuses.emplace_back(s);
                       }});
    setenv("HOME", saved.c_str(), 1);

    REQUIRE(scan.sessions.size() == 1);
    const auto& s = scan.sessions.front();
    CHECK(s.browser == "Zen");
    CHECK(s.profile == "Default (release)");
    CHECK(s.label() == "Zen · Default (release)");
    CHECK(s.creds.session == "live-session");
    CHECK(s.creds.csrf == "csrf-1");
    CHECK(s.creds.source == "Zen/abc123.Default (release)");
    CHECK(std::ranges::find(scan.searched, "Zen") != scan.searched.end());
    REQUIRE(scan.problems.size() == 1);
    CHECK(scan.problems.front().find("expired") != std::string::npos);
    CHECK(statuses.empty()); // no keychain needed

    // Restricting to a source only reads that profile.
    setenv("HOME", home.path.c_str(), 1);
    const auto only =
        scan_browsers({.domain = "leetcode.com", .only = "Zen/def456.Old", .on_status = {}});
    setenv("HOME", saved.c_str(), 1);
    CHECK(only.sessions.empty());
    CHECK(only.problems.size() == 1);
}
