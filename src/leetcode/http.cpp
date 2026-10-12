#include "leetcode/http.hpp"

#include "util/log.hpp"

#include <curl/curl.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>

namespace lv::leetcode::http {

namespace {

constexpr long connect_timeout_ms = 10'000;
constexpr long total_timeout_ms = 30'000;
// How often a transfer checks for cancellation (curl_easy_perform would only check ~1/s).
constexpr int poll_interval_ms = 50;
constexpr const char* user_agent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/129.0.0.0 Safari/537.36";

struct EasyDeleter {
    void operator()(CURL* curl) const { curl_easy_cleanup(curl); }
};
struct MultiDeleter {
    void operator()(CURLM* multi) const { curl_multi_cleanup(multi); }
};
struct ListDeleter {
    void operator()(curl_slist* list) const { curl_slist_free_all(list); }
};
using EasyHandle = std::unique_ptr<CURL, EasyDeleter>;
using MultiHandle = std::unique_ptr<CURLM, MultiDeleter>;
using HeaderList = std::unique_ptr<curl_slist, ListDeleter>;

/// One per thread. The multi handle owns the connection cache, so keep-alive connections
/// are reused by later requests on the same thread.
struct Session {
    MultiHandle multi{curl_multi_init()};
    EasyHandle easy{curl_easy_init()};
};

bool is_file(const char* path) {
    std::error_code ec;
    return path != nullptr && *path != '\0' && std::filesystem::is_regular_file(path, ec);
}

bool is_dir(const char* path) {
    std::error_code ec;
    return path != nullptr && *path != '\0' && std::filesystem::is_directory(path, ec);
}

// Explicit environment overrides first, then libcurl's compiled-in bundle if it exists,
// then the usual system locations. Empty means "leave libcurl's default alone".
std::string find_ca_bundle(CURL* curl) {
    for (const char* var : {"SSL_CERT_FILE", "NIX_SSL_CERT_FILE"}) {
        if (const char* path = std::getenv(var); is_file(path)) {
            return path;
        }
    }
#if LIBCURL_VERSION_NUM >= 0x075400 // CURLINFO_CAINFO / CAPATH: 7.84.0
    const char* builtin = nullptr;
    if (curl_easy_getinfo(curl, CURLINFO_CAINFO, &builtin) == CURLE_OK && is_file(builtin)) {
        return {};
    }
    const char* builtin_dir = nullptr;
    if (curl_easy_getinfo(curl, CURLINFO_CAPATH, &builtin_dir) == CURLE_OK && is_dir(builtin_dir)) {
        return {};
    }
#else
    (void)curl;
#endif
    for (const char* path : {
             "/etc/ssl/cert.pem",
             "/etc/ssl/certs/ca-certificates.crt",
             "/etc/pki/tls/certs/ca-bundle.crt",
         }) {
        if (is_file(path)) {
            return path;
        }
    }
    return {};
}

void global_init() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

const std::string& ca_bundle() {
    static const std::string path = [] {
        const EasyHandle probe(curl_easy_init());
        return probe ? find_ca_bundle(probe.get()) : std::string{};
    }();
    return path;
}

Session* thread_session() {
    global_init();
    thread_local Session session;
    if (!session.multi || !session.easy) {
        return nullptr;
    }
    curl_easy_reset(session.easy.get());
    return &session;
}

// NOLINTNEXTLINE(misc-const-correctness): must match libcurl's write callback type
std::size_t on_body(char* data, std::size_t size, std::size_t count, void* user) {
    static_cast<std::string*>(user)->append(data, size * count);
    return size * count;
}

std::stop_source& abort_source() {
    static std::stop_source source;
    return source;
}

bool stopped(const std::stop_token& stop) {
    return stop.stop_requested() || abort_source().stop_requested();
}

Error cancelled() { return Error{.kind = Error::Kind::Cancelled, .message = "Cancelled"}; }

Error network(std::string_view detail) {
    return Error{
        .kind = Error::Kind::Network,
        .message = std::format("Could not reach LeetCode: {}", detail),
    };
}

/// Drives one transfer to completion on `session`, checking for cancellation between polls.
/// Returns nullopt when cancelled.
std::optional<CURLcode> run_transfer(const Session& session, const std::stop_token& stop) {
    CURLM* multi = session.multi.get();
    CURL* easy = session.easy.get();
    if (curl_multi_add_handle(multi, easy) != CURLM_OK) {
        return CURLE_FAILED_INIT;
    }
    std::optional<CURLcode> result = CURLE_OK;
    int running = 1;
    while (true) {
        if (curl_multi_perform(multi, &running) != CURLM_OK) {
            result = CURLE_FAILED_INIT;
            break;
        }
        if (running == 0) {
            break;
        }
        if (stopped(stop)) {
            result = std::nullopt;
            break;
        }
        if (curl_multi_poll(multi, nullptr, 0, poll_interval_ms, nullptr) != CURLM_OK) {
            result = CURLE_FAILED_INIT;
            break;
        }
    }
    if (result) {
        int queued = 0;
        while (const CURLMsg* msg = curl_multi_info_read(multi, &queued)) {
            if (msg->msg == CURLMSG_DONE && msg->easy_handle == easy) {
                result = msg->data.result;
            }
        }
    }
    curl_multi_remove_handle(multi, easy);
    return result;
}

} // namespace

void abort_all() { abort_source().request_stop(); }

std::stop_token abort_token() { return abort_source().get_token(); }

Result<Response> perform(const Request& request, const std::stop_token& stop) {
    if (stopped(stop)) {
        return std::unexpected(cancelled());
    }
    const Session* session = thread_session();
    if (session == nullptr) {
        return std::unexpected(network("could not initialise libcurl"));
    }
    CURL* curl = session->easy.get();

    HeaderList headers;
    for (const auto& header : request.headers) {
        curl_slist* list = curl_slist_append(headers.get(), header.c_str());
        if (list == nullptr) {
            return std::unexpected(network("out of memory"));
        }
        if (!headers) {
            headers.reset(list); // appending returns the (unchanged) head otherwise
        }
    }

    Response response;
    std::array<char, CURL_ERROR_SIZE> error_buffer{};
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, total_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(&response.body));
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer.data());
    if (const auto& ca = ca_bundle(); !ca.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
    }
    if (request.post) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(request.body.size()));
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
    }

    const auto start = std::chrono::steady_clock::now();
    const auto code = run_transfer(*session, stop);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    const char* method = request.post ? "POST" : "GET";

    if (!code) {
        log::debug("{} {} cancelled after {}", method, request.url, elapsed);
        return std::unexpected(cancelled());
    }
    if (*code != CURLE_OK) {
        const std::string detail =
            error_buffer[0] != '\0' ? error_buffer.data() : curl_easy_strerror(*code);
        log::debug("{} {} failed after {}: {}", method, request.url, elapsed, detail);
        return std::unexpected(network(detail));
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    log::debug("{} {} -> {} ({} bytes, {})", method, request.url, response.status,
               response.body.size(), elapsed);
    return response;
}

} // namespace lv::leetcode::http
