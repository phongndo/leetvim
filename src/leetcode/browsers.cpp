#include "leetcode/browsers.hpp"

#include "util/log.hpp"
#include "util/paths.hpp"
#include "util/strings.hpp"

#include <nlohmann/json.hpp>

#include <fcntl.h>
#include <openssl/evp.h>
#include <spawn.h>
#include <sqlite3.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <system_error>

// glibc's <unistd.h> declares this too, but macOS and other libcs don't.
extern char** environ; // NOLINT(readability-redundant-declaration)

namespace lv::leetcode {

namespace fs = std::filesystem;
using browser_detail::RawCookie;

namespace {

constexpr std::string_view session_cookie = "LEETCODE_SESSION";
constexpr std::string_view csrf_cookie = "csrftoken";

/// Chromium-based browser: where it keeps profiles and how its cookie key is stored.
struct Chromium {
    std::string_view name;
    std::string_view mac_dir;          // under ~/Library/Application Support
    std::string_view keychain_service; // macOS Keychain item holding the key password
    std::string_view keychain_account;
    std::string_view linux_dir;  // under ~/.config
    std::string_view secret_app; // libsecret "application" attribute on Linux
};

constexpr auto chromium_browsers = std::to_array<Chromium>({
    {"Chrome", "Google/Chrome", "Chrome Safe Storage", "Chrome", "google-chrome", "chrome"},
    {"Chrome Beta", "Google/Chrome Beta", "Chrome Safe Storage", "Chrome", "google-chrome-beta",
     "chrome"},
    {"Chrome Canary", "Google/Chrome Canary", "Chrome Safe Storage", "Chrome", "", ""},
    {"Chromium", "Chromium", "Chromium Safe Storage", "Chromium", "chromium", "chromium"},
    {"Helium", "net.imput.helium", "Helium Storage Key", "Helium", "net.imput.helium", "helium"},
    {"Arc", "Arc/User Data", "Arc Safe Storage", "Arc", "", ""},
    {"Brave", "BraveSoftware/Brave-Browser", "Brave Safe Storage", "Brave",
     "BraveSoftware/Brave-Browser", "brave"},
    {"Edge", "Microsoft Edge", "Microsoft Edge Safe Storage", "Microsoft Edge", "microsoft-edge",
     "microsoft-edge"},
    {"Vivaldi", "Vivaldi", "Vivaldi Safe Storage", "Vivaldi", "vivaldi", "vivaldi"},
    {"Opera", "com.operasoftware.Opera", "Opera Safe Storage", "Opera", "opera", "opera"},
    {"Dia", "Dia/User Data", "Dia Safe Storage", "Dia", "", ""},
    {"Comet", "Comet", "Comet Safe Storage", "Comet", "", ""},
});

struct Firefox {
    std::string_view name;
    std::string_view mac_dir;   // under ~/Library/Application Support
    std::string_view linux_dir; // under ~
};

constexpr auto firefox_browsers = std::to_array<Firefox>({
    {"Firefox", "Firefox/Profiles", ".mozilla/firefox"},
    {"Zen", "zen/Profiles", ".zen"},
    {"LibreWolf", "librewolf/Profiles", ".librewolf"},
    {"Floorp", "Floorp/Profiles", ".floorp"},
    {"Waterfox", "Waterfox/Profiles", ".waterfox"},
});

struct WebKit {
    std::string_view name;
    std::string_view path; // under ~
};

constexpr auto webkit_browsers = std::to_array<WebKit>({
    {"Safari", "Library/Containers/com.apple.Safari/Data/Library/Cookies/Cookies.binarycookies"},
    {"Safari", "Library/Cookies/Cookies.binarycookies"},
    {"Orion", "Library/HTTPStorages/com.kagi.kagimacOS.binarycookies"},
});

#ifdef __APPLE__
constexpr bool macos = true;
#else
constexpr bool macos = false;
#endif

fs::path app_support() { return paths::home() / "Library" / "Application Support"; }

std::int64_t now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// Runs a helper and returns its stdout (trailing newline removed), or nullopt on failure.
std::optional<std::string> capture(const std::vector<std::string>& argv) {
    std::array<int, 2> out{};
    if (pipe(out.data()) != 0) return std::nullopt;
    posix_spawn_file_actions_t actions{};
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, out[0]);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    std::vector<std::string> args = argv;
    std::vector<char*> ptrs;
    ptrs.reserve(args.size() + 1);
    for (auto& a : args) ptrs.push_back(a.data());
    ptrs.push_back(nullptr);
    pid_t pid = -1;
    const int rc = posix_spawnp(&pid, ptrs[0], &actions, nullptr, ptrs.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(out[1]);
    std::string result;
    if (rc == 0) {
        std::array<char, 4096> buf{};
        while (true) {
            const ssize_t n = read(out[0], buf.data(), buf.size());
            if (n > 0) {
                result.append(buf.data(), static_cast<std::size_t>(n));
            } else if (n == 0 || errno != EINTR) {
                break;
            }
        }
    }
    close(out[0]);
    if (rc != 0) return std::nullopt;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return std::nullopt;
    while (result.ends_with('\n') || result.ends_with('\r')) result.pop_back();
    return result;
}

/// Copies a SQLite database (plus its WAL) to a private temp dir, so a running browser's
/// lock can't interfere. Removed when the returned guard dies.
class DbCopy {
public:
    explicit DbCopy(const fs::path& db) {
        std::string tmpl = (fs::temp_directory_path() / "leetvim-cookies-XXXXXX").string();
        if (mkdtemp(tmpl.data()) == nullptr) return;
        dir_ = tmpl;
        std::error_code ec;
        for (const std::string suffix : {"", "-wal", "-journal"}) {
            fs::path from = db;
            from += suffix;
            if (!fs::exists(from, ec)) continue;
            fs::copy_file(from, dir_ / (db.filename().string() + suffix), ec);
            if (ec && suffix.empty()) {
                error_ = ec.message();
                return;
            }
        }
        path_ = dir_ / db.filename();
    }
    ~DbCopy() {
        std::error_code ec;
        if (!dir_.empty()) fs::remove_all(dir_, ec);
    }
    DbCopy(const DbCopy&) = delete;
    DbCopy& operator=(const DbCopy&) = delete;
    DbCopy(DbCopy&&) = delete;
    DbCopy& operator=(DbCopy&&) = delete;

    [[nodiscard]] const fs::path& path() const { return path_; }
    [[nodiscard]] const std::string& error() const { return error_; }

private:
    fs::path dir_;
    fs::path path_;
    std::string error_;
};

struct SqliteCloser {
    void operator()(sqlite3* db) const { sqlite3_close(db); }
};
struct StmtFinalizer {
    void operator()(sqlite3_stmt* s) const { sqlite3_finalize(s); }
};
using SqliteDb = std::unique_ptr<sqlite3, SqliteCloser>;
using SqliteStmt = std::unique_ptr<sqlite3_stmt, StmtFinalizer>;

SqliteDb open_db(const fs::path& path, std::string& error) {
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &raw, SQLITE_OPEN_READONLY, nullptr);
    SqliteDb db(raw);
    if (rc != SQLITE_OK) {
        error = db ? sqlite3_errmsg(db.get()) : "cannot open the cookie database";
        return nullptr;
    }
    return db;
}

std::string column_blob(sqlite3_stmt* stmt, int col) {
    const auto* data = static_cast<const char*>(sqlite3_column_blob(stmt, col));
    const int size = sqlite3_column_bytes(stmt, col);
    return data == nullptr ? std::string() : std::string(data, static_cast<std::size_t>(size));
}

/// Runs a query with one text parameter, calling `row` for each result row.
bool query(sqlite3* db, const char* sql, std::string_view param,
           const std::function<void(sqlite3_stmt*)>& row, std::string& error) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db);
        return false;
    }
    const SqliteStmt stmt(raw);
    if (!param.empty()) {
        sqlite3_bind_text(stmt.get(), 1, param.data(), static_cast<int>(param.size()),
                          SQLITE_TRANSIENT);
    }
    int rc = 0;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) row(stmt.get());
    if (rc != SQLITE_DONE) {
        error = sqlite3_errmsg(db);
        return false;
    }
    return true;
}

template <typename T>
T read_le(std::string_view data, std::size_t at) {
    T value{};
    std::memcpy(&value, data.data() + at, sizeof(T));
    if constexpr (std::endian::native == std::endian::big) value = std::byteswap(value);
    return value;
}

std::uint32_t read_be32(std::string_view data, std::size_t at) {
    return std::byteswap(read_le<std::uint32_t>(data, at));
}

std::string c_string(std::string_view data, std::size_t at) {
    if (at >= data.size()) return {};
    const auto end = data.find('\0', at);
    return std::string(data.substr(at, end == std::string_view::npos ? end : end - at));
}

/// Display names of Chromium profiles ("Profile 5" -> "Work") from the "Local State" file.
std::map<std::string, std::string> chromium_profile_names(const fs::path& dir) {
    std::map<std::string, std::string> names;
    std::string text;
    if (!paths::read_file(dir / "Local State", text)) return names;
    const auto doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded()) return names;
    const auto cache = doc.find("profile");
    if (cache == doc.end() || !cache->is_object()) return names;
    const auto info = cache->find("info_cache");
    if (info == cache->end() || !info->is_object()) return names;
    for (const auto& [key, value] : info->items()) {
        if (value.is_object() && value.contains("name") && value["name"].is_string()) {
            names[key] = value["name"].get<std::string>();
        }
    }
    return names;
}

bool wanted(const ScanOptions& options, std::string_view browser, std::string_view source) {
    return options.only.empty() || str::iequals(options.only, browser) || options.only == source;
}

void status(const ScanOptions& options, std::string_view message) {
    if (options.on_status) options.on_status(message);
}

/// Turns the cookies of one profile into a session, if it has both cookies and they are live.
void collect(BrowserScan& scan, std::string browser, std::string profile, std::string source,
             const std::vector<RawCookie>& cookies) {
    BrowserSession s{
        .browser = std::move(browser), .profile = std::move(profile), .creds = {}, .expires = {}};
    std::int64_t expires = 0;
    for (const auto& c : cookies) {
        if (c.name == session_cookie) {
            s.creds.session = c.value;
            expires = c.expires;
        } else if (c.name == csrf_cookie) {
            s.creds.csrf = c.value;
        }
    }
    if (s.creds.session.empty()) return;
    if (s.creds.csrf.empty()) {
        scan.problems.push_back(
            std::format("{}: found a session but no csrftoken cookie", s.label()));
        return;
    }
    if (expires != 0 && expires < now_unix()) {
        scan.problems.push_back(std::format(
            "{}: the LeetCode session expired; open leetcode.com there to renew it", s.label()));
        return;
    }
    s.creds.source = std::move(source);
    s.expires = std::chrono::system_clock::time_point{std::chrono::seconds{expires}};
    scan.sessions.push_back(std::move(s));
}

void scan_chromium(BrowserScan& scan, const ScanOptions& options) {
    std::map<std::string, std::optional<std::string>, std::less<>> keys; // per key password
    for (const auto& b : chromium_browsers) {
        const fs::path dir =
            macos ? app_support() / b.mac_dir
                  : (b.linux_dir.empty() ? fs::path() : paths::home() / ".config" / b.linux_dir);
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) continue;
        if (!wanted(options, b.name, "") && !options.only.starts_with(std::string(b.name) + "/")) {
            continue;
        }
        scan.searched.emplace_back(b.name);
        const auto names = chromium_profile_names(dir);
        std::vector<fs::path> profiles{dir};
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.is_directory(ec)) profiles.push_back(entry.path());
        }
        for (const auto& profile : profiles) {
            fs::path db = profile / "Network" / "Cookies";
            if (!fs::exists(db, ec)) db = profile / "Cookies";
            if (!fs::exists(db, ec)) continue;
            const std::string profile_dir = profile == dir ? "" : profile.filename().string();
            const std::string source = std::format("{}/{}", b.name, profile_dir);
            if (!wanted(options, b.name, source)) continue;
            const auto name_it = names.find(profile_dir);
            const std::string display = name_it != names.end() ? name_it->second : profile_dir;

            int version = 0;
            std::string error;
            auto cookies = browser_detail::read_chromium_db(db, options.domain, version, error);
            if (!error.empty()) {
                scan.problems.push_back(std::format("{} · {}: {}", b.name, display, error));
                continue;
            }
            const bool has_session = std::ranges::any_of(
                cookies, [](const RawCookie& c) { return c.name == session_cookie; });
            if (!has_session) continue;

            // Only now ask for the key: it may show a Keychain prompt.
            const std::string key_id(b.keychain_service);
            if (!keys.contains(key_id)) {
                status(options, std::format("unlocking {}'s cookie key (approve the macOS "
                                            "prompt; Always Allow keeps refreshes silent)",
                                            b.name));
                std::optional<std::string> password;
                if (macos) {
                    password = capture({"/usr/bin/security", "find-generic-password", "-w", "-s",
                                        std::string(b.keychain_service), "-a",
                                        std::string(b.keychain_account)});
                    if (!password) {
                        password = capture({"/usr/bin/security", "find-generic-password", "-w",
                                            "-s", std::string(b.keychain_service)});
                    }
                } else if (!b.secret_app.empty()) {
                    password = capture(
                        {"secret-tool", "lookup", "application", std::string(b.secret_app)});
                }
                keys[key_id] = password;
            }
            const auto& password = keys[key_id];
            for (auto& c : cookies) {
                if (c.name != session_cookie && c.name != csrf_cookie) continue;
                if (!c.value.starts_with("v10") && !c.value.starts_with("v11")) continue;
                std::optional<std::string> plain;
                if (macos) {
                    if (password) {
                        plain = browser_detail::chromium_decrypt(
                            c.value, browser_detail::chromium_key(*password, 1003), version >= 24);
                    }
                } else if (c.value.starts_with("v11") && password) {
                    plain = browser_detail::chromium_decrypt(
                        c.value, browser_detail::chromium_key(*password, 1), version >= 24);
                } else {
                    plain = browser_detail::chromium_decrypt(
                        c.value, browser_detail::chromium_key("peanuts", 1), version >= 24);
                }
                c.value = plain.value_or("");
            }
            if (!password && macos) {
                scan.problems.push_back(std::format(
                    "{}: could not read its cookie key from the Keychain (denied?)", b.name));
                continue;
            }
            collect(scan, std::string(b.name), display, source, cookies);
        }
    }
}

void scan_firefox(BrowserScan& scan, const ScanOptions& options) {
    for (const auto& b : firefox_browsers) {
        const fs::path dir = macos ? app_support() / b.mac_dir : paths::home() / b.linux_dir;
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        if (!wanted(options, b.name, "") && !options.only.starts_with(std::string(b.name) + "/")) {
            continue;
        }
        scan.searched.emplace_back(b.name);
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            const fs::path db = entry.path() / "cookies.sqlite";
            if (!fs::exists(db, ec)) continue;
            const std::string profile_dir = entry.path().filename().string();
            const std::string source = std::format("{}/{}", b.name, profile_dir);
            if (!wanted(options, b.name, source)) continue;
            // "abcd1234.Default (release)" -> "Default (release)"
            const auto dot = profile_dir.find('.');
            const std::string display =
                dot == std::string::npos ? profile_dir : profile_dir.substr(dot + 1);
            std::string error;
            const auto cookies = browser_detail::read_firefox_db(db, options.domain, error);
            if (!error.empty()) {
                scan.problems.push_back(std::format("{} · {}: {}", b.name, display, error));
                continue;
            }
            collect(scan, std::string(b.name), display, source, cookies);
        }
    }
}

void scan_webkit(BrowserScan& scan, const ScanOptions& options) {
    if (!macos) return;
    for (const auto& b : webkit_browsers) {
        const fs::path file = paths::home() / b.path;
        std::error_code ec;
        if (!fs::exists(file, ec) || !wanted(options, b.name, b.name)) continue;
        if (std::ranges::find(scan.searched, b.name) == scan.searched.end()) {
            scan.searched.emplace_back(b.name);
        }
        std::string data;
        if (!paths::read_file(file, data)) {
            scan.problems.push_back(std::format(
                "{}: can't read its cookies; give your terminal Full Disk Access (System "
                "Settings → Privacy & Security) to import from {}",
                b.name, b.name));
            continue;
        }
        std::vector<RawCookie> cookies;
        for (auto& c : browser_detail::parse_binarycookies(data)) {
            if (browser_detail::host_matches(c.host, options.domain))
                cookies.push_back(std::move(c));
        }
        collect(scan, std::string(b.name), "", std::string(b.name), cookies);
    }
}

} // namespace

std::string BrowserSession::label() const {
    return profile.empty() ? browser : std::format("{} · {}", browser, profile);
}

BrowserScan scan_browsers(const ScanOptions& options) {
    BrowserScan scan;
    // Stores that need no key come first, so a prompt is only shown when necessary.
    scan_firefox(scan, options);
    scan_webkit(scan, options);
    scan_chromium(scan, options);
    std::ranges::stable_sort(scan.sessions, std::greater<>{}, &BrowserSession::expires);
    log::debug("browser scan: {} session(s), {} problem(s)", scan.sessions.size(),
               scan.problems.size());
    return scan;
}

namespace browser_detail {

bool host_matches(std::string_view host, std::string_view domain) {
    if (host.starts_with('.')) host.remove_prefix(1);
    return host == domain || host.ends_with("." + std::string(domain));
}

std::string chromium_key(std::string_view password, int iterations) {
    static constexpr std::string_view salt = "saltysalt";
    std::array<unsigned char, 16> key{};
    PKCS5_PBKDF2_HMAC_SHA1(password.data(), static_cast<int>(password.size()),
                           reinterpret_cast<const unsigned char*>(salt.data()),
                           static_cast<int>(salt.size()), iterations, static_cast<int>(key.size()),
                           key.data());
    return {reinterpret_cast<const char*>(key.data()), key.size()};
}

namespace {

struct CipherFree {
    void operator()(EVP_CIPHER_CTX* ctx) const { EVP_CIPHER_CTX_free(ctx); }
};

std::optional<std::string> aes_128_cbc(std::string_view input, std::string_view key, bool encrypt) {
    if (key.size() != 16) return std::nullopt;
    static constexpr std::array<unsigned char, 16> iv{' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
                                                      ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    const std::unique_ptr<EVP_CIPHER_CTX, CipherFree> ctx(EVP_CIPHER_CTX_new());
    if (!ctx
        || EVP_CipherInit_ex(ctx.get(), EVP_aes_128_cbc(), nullptr,
                             reinterpret_cast<const unsigned char*>(key.data()), iv.data(),
                             encrypt ? 1 : 0)
               != 1) {
        return std::nullopt;
    }
    std::string out(input.size() + 32, '\0');
    int written = 0;
    int final_written = 0;
    auto* dst = reinterpret_cast<unsigned char*>(out.data());
    if (EVP_CipherUpdate(ctx.get(), dst, &written,
                         reinterpret_cast<const unsigned char*>(input.data()),
                         static_cast<int>(input.size()))
            != 1
        || EVP_CipherFinal_ex(ctx.get(), dst + written, &final_written) != 1) {
        return std::nullopt; // wrong key shows up as bad padding
    }
    out.resize(static_cast<std::size_t>(written) + static_cast<std::size_t>(final_written));
    return out;
}

} // namespace

std::optional<std::string> chromium_decrypt(std::string_view encrypted, std::string_view key,
                                            bool strip_host_hash) {
    if (!encrypted.starts_with("v10") && !encrypted.starts_with("v11")) return std::nullopt;
    const auto body = encrypted.substr(3);
    if (body.empty() || body.size() % 16 != 0) return std::nullopt;
    auto plain = aes_128_cbc(body, key, false);
    if (!plain) return std::nullopt;
    if (strip_host_hash) {
        if (plain->size() < 32) return std::nullopt;
        plain->erase(0, 32);
    }
    // Cookie values are printable; anything else means the key was wrong.
    if (!std::ranges::all_of(*plain, [](char c) {
            const auto u = static_cast<unsigned char>(c);
            return u >= 0x20 && u < 0x7f;
        })) {
        return std::nullopt;
    }
    return plain;
}

std::string chromium_encrypt(std::string_view plaintext, std::string_view key,
                             std::string_view prefix) {
    return std::string(prefix) + aes_128_cbc(plaintext, key, true).value_or("");
}

std::vector<RawCookie> parse_binarycookies(std::string_view data) {
    // Layout: "cook", page count and sizes (big endian), then pages whose fields are little
    // endian. Every offset is bounds-checked; a damaged file yields what could be read.
    std::vector<RawCookie> out;
    if (data.size() < 8 || !data.starts_with("cook")) return out;
    const std::uint32_t pages = read_be32(data, 4);
    std::size_t offset = 8 + (static_cast<std::size_t>(pages) * 4);
    if (offset > data.size()) return out;
    constexpr std::int64_t mac_epoch = 978307200; // 2001-01-01 in unix time
    for (std::uint32_t p = 0; p < pages; ++p) {
        const std::uint32_t size = read_be32(data, 8 + (static_cast<std::size_t>(p) * 4));
        if (offset + size > data.size() || size < 8) break;
        const std::string_view page = data.substr(offset, size);
        offset += size;
        const auto count = read_le<std::uint32_t>(page, 4);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::size_t at = 8 + (static_cast<std::size_t>(i) * 4);
            if (at + 4 > page.size()) break;
            const std::size_t start = read_le<std::uint32_t>(page, at); // size_t: no wraparound
            if (start + 56 > page.size()) continue;
            const std::size_t csize = read_le<std::uint32_t>(page, start);
            if (csize < 56 || start + csize > page.size()) continue;
            const std::string_view cookie = page.substr(start, csize);
            RawCookie c;
            c.host = c_string(cookie, read_le<std::uint32_t>(cookie, 16));
            c.name = c_string(cookie, read_le<std::uint32_t>(cookie, 20));
            c.value = c_string(cookie, read_le<std::uint32_t>(cookie, 28));
            // A corrupt file can hold any double, and converting one outside int64's range
            // is undefined; treat those like session cookies.
            const auto expiry = read_le<double>(cookie, 40);
            c.expires = std::isfinite(expiry) && std::abs(expiry) < 1e15
                            ? static_cast<std::int64_t>(expiry) + mac_epoch
                            : 0;
            out.push_back(std::move(c));
        }
    }
    return out;
}

std::vector<RawCookie> read_chromium_db(const fs::path& db, std::string_view domain,
                                        int& schema_version, std::string& error) {
    std::vector<RawCookie> out;
    const DbCopy copy(db);
    if (copy.path().empty()) {
        error = copy.error().empty() ? "cannot copy the cookie database" : copy.error();
        return out;
    }
    const SqliteDb handle = open_db(copy.path(), error);
    if (!handle) return out;
    schema_version = 0;
    std::string ignored;
    query(
        handle.get(), "SELECT value FROM meta WHERE key = 'version'", "",
        [&](sqlite3_stmt* s) { schema_version = sqlite3_column_int(s, 0); }, ignored);
    const std::string like = "%" + std::string(domain);
    query(
        handle.get(),
        "SELECT host_key, name, value, encrypted_value, expires_utc FROM cookies "
        "WHERE host_key LIKE ?1",
        like,
        [&](sqlite3_stmt* s) {
            RawCookie c;
            c.host = column_blob(s, 0);
            if (!host_matches(c.host, domain)) return;
            c.name = column_blob(s, 1);
            const std::string encrypted = column_blob(s, 3);
            c.value = encrypted.empty() ? column_blob(s, 2) : encrypted;
            // Microseconds since 1601-01-01; 0 means "session cookie".
            const std::int64_t utc = sqlite3_column_int64(s, 4);
            c.expires = utc == 0 ? 0 : (utc / 1'000'000) - 11'644'473'600;
            out.push_back(std::move(c));
        },
        error);
    return out;
}

std::vector<RawCookie> read_firefox_db(const fs::path& db, std::string_view domain,
                                       std::string& error) {
    std::vector<RawCookie> out;
    const DbCopy copy(db);
    if (copy.path().empty()) {
        error = copy.error().empty() ? "cannot copy the cookie database" : copy.error();
        return out;
    }
    const SqliteDb handle = open_db(copy.path(), error);
    if (!handle) return out;
    const std::string like = "%" + std::string(domain);
    query(
        handle.get(), "SELECT host, name, value, expiry FROM moz_cookies WHERE host LIKE ?1", like,
        [&](sqlite3_stmt* s) {
            RawCookie c;
            c.host = column_blob(s, 0);
            if (!host_matches(c.host, domain)) return;
            c.name = column_blob(s, 1);
            c.value = column_blob(s, 2);
            std::int64_t expiry = sqlite3_column_int64(s, 3);
            if (expiry > 100'000'000'000) expiry /= 1000; // newer Firefox stores milliseconds
            c.expires = expiry;
            out.push_back(std::move(c));
        },
        error);
    return out;
}

} // namespace browser_detail

} // namespace lv::leetcode
