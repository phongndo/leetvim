#include "leetcode/client.hpp"

#include <doctest/doctest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <string_view>
#include <thread>

using namespace lv::leetcode;
using namespace std::chrono_literals;

namespace {

using Clock = std::chrono::steady_clock;

bool live_tests() {
    const char* v = std::getenv("LEETVIM_LIVE_TESTS");
    return v != nullptr && std::string_view(v) == "1";
}

long long ms_since(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

/// A local TCP server that accepts connections (via the listen backlog) but never answers,
/// so requests to it hang until cancelled.
class HangingServer {
public:
    HangingServer() : fd_(socket(AF_INET, SOCK_STREAM, 0)) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t len = sizeof addr;
        auto* raw = reinterpret_cast<sockaddr*>(&addr);
        if (fd_ < 0 || bind(fd_, raw, len) != 0 || listen(fd_, 16) != 0
            || getsockname(fd_, raw, &len) != 0) {
            return;
        }
        url_ = std::format("http://127.0.0.1:{}", ntohs(addr.sin_port));
    }
    ~HangingServer() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }
    HangingServer(const HangingServer&) = delete;
    HangingServer& operator=(const HangingServer&) = delete;
    HangingServer(HangingServer&&) = delete;
    HangingServer& operator=(HangingServer&&) = delete;

    [[nodiscard]] const std::string& url() const { return url_; }

private:
    int fd_;
    std::string url_;
};

Credentials fake_creds() { return {.session = "s", .csrf = "c"}; }

/// Why a live call failed, for the nightly log: LeetCode's unofficial API changes without
/// notice, and a bare "has_value() is false" doesn't say whether it was blocked, rate
/// limited, unreachable or reshaped.
template <typename T>
std::string failure(const Result<T>& result) {
    if (result) {
        return "succeeded";
    }
    const Error& e = result.error();
    return std::format("{} (kind {}, HTTP {})", e.message, static_cast<int>(e.kind), e.http_status);
}

} // namespace

TEST_CASE("client basics") {
    const Client client(Credentials(), "https://leetcode.com/");
    CHECK(client.base_url() == "https://leetcode.com");
    CHECK(client.problem_url("two-sum") == "https://leetcode.com/problems/two-sum/");
    CHECK_FALSE(client.has_credentials());
    CHECK(Client(fake_creds()).has_credentials());
}

TEST_CASE("authenticated calls fail fast without credentials") {
    const Client client(Credentials(), "http://127.0.0.1:9");
    Question q;
    q.slug = "two-sum";
    q.question_id = "1";
    CHECK(client.streak().error().kind == Error::Kind::Auth);
    CHECK(client.run(q, "cpp", "", "").error().kind == Error::Kind::Auth);
    CHECK(client.submit(q, "cpp", "").error().kind == Error::Kind::Auth);
    CHECK(client.check("runcode_1", "two-sum").error().kind == Error::Kind::Auth);
    CHECK(client.wait("runcode_1", "two-sum").error().kind == Error::Kind::Auth);
    CHECK(client.submission_detail("1").error().kind == Error::Kind::Auth);
    CHECK(client.submissions("two-sum").error().kind == Error::Kind::Auth);
    CHECK(Client(fake_creds()).submission_detail("abc").error().kind == Error::Kind::Api);
}

TEST_CASE("unreachable server is a network error") {
    const Client client(Credentials(), "http://127.0.0.1:9");
    const auto daily = client.daily();
    REQUIRE_FALSE(daily.has_value());
    CHECK(daily.error().kind == Error::Kind::Network);
}

TEST_CASE("a stopped token cancels without touching the network") {
    std::stop_source stop;
    stop.request_stop();
    const Client client(fake_creds(), "http://127.0.0.1:9");
    CHECK(client.problems({}, stop.get_token()).error().kind == Error::Kind::Cancelled);
    CHECK(client.wait("runcode_1", "two-sum", stop.get_token()).error().kind
          == Error::Kind::Cancelled);
}

TEST_CASE("stopping cancels a hanging request promptly") {
    const HangingServer server;
    REQUIRE_FALSE(server.url().empty());
    const Client client(fake_creds(), server.url());
    std::stop_source stop;
    const auto start = Clock::now();
    const std::jthread stopper([&] {
        std::this_thread::sleep_for(200ms);
        stop.request_stop();
    });
    const auto result = client.wait("runcode_1", "two-sum", stop.get_token());
    const auto elapsed = ms_since(start);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind == Error::Kind::Cancelled);
    MESSAGE(std::format("cancelled after {} ms", elapsed));
    CHECK(elapsed < 900);
}

TEST_CASE("abort_all cancels in-flight and later requests") {
    // abort_all() is irreversible, so exercise it in a child process.
    const HangingServer server;
    REQUIRE_FALSE(server.url().empty());
    const pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        const Client client(Credentials(), server.url());
        const auto start = Clock::now();
        std::jthread aborter([] {
            std::this_thread::sleep_for(200ms);
            Client::abort_all();
        });
        const auto first = client.problems();
        const auto elapsed = ms_since(start);
        aborter.join();
        const auto later = client.daily();
        const bool ok = !first && first.error().kind == Error::Kind::Cancelled && elapsed < 900
                        && !later && later.error().kind == Error::Kind::Cancelled;
        _exit(ok ? 0 : 1);
    }
    int status = 0;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): doctest registration
TEST_CASE("live: public endpoints" * doctest::skip(!live_tests())) {
    const Client client;

    auto start = Clock::now();
    int progress_calls = 0;
    int last_done = 0;
    int last_total = 0;
    const auto problems = client.problems([&](int done, int total) {
        ++progress_calls;
        last_done = done;
        last_total = total;
    });
    REQUIRE_MESSAGE(problems.has_value(), failure(problems));
    MESSAGE(std::format("problems(): {} problems in {} ms ({} progress callbacks, last {}/{})",
                        problems->size(), ms_since(start), progress_calls, last_done, last_total));
    CHECK(problems->size() > 3500);
    CHECK(last_done == last_total);
    const auto two_sum = std::ranges::find(*problems, "two-sum", &ProblemSummary::slug);
    REQUIRE(two_sum != problems->end());
    CHECK(two_sum->number == 1);
    CHECK(std::ranges::count(*problems, Difficulty::Hard, &ProblemSummary::difficulty) > 500);

    start = Clock::now();
    const auto q = client.question("two-sum");
    REQUIRE_MESSAGE(q.has_value(), failure(q));
    MESSAGE(
        std::format("question(two-sum): {} snippets, {} ms", q->snippets.size(), ms_since(start)));
    CHECK(q->question_id == "1");
    CHECK(q->meta.function_name == "twoSum");
    CHECK(q->snippet("cpp") != nullptr);
    CHECK(q->total_accepted > 0);

    start = Clock::now();
    const auto daily = client.daily();
    REQUIRE_MESSAGE(daily.has_value(), failure(daily));
    MESSAGE(
        std::format("daily(): {} {} ({} ms)", daily->date, daily->problem.slug, ms_since(start)));
    CHECK_FALSE(daily->problem.slug.empty());

    start = Clock::now();
    const auto stats = client.user_stats("lee215");
    REQUIRE_MESSAGE(stats.has_value(), failure(stats));
    MESSAGE(std::format("user_stats(lee215): solved {} (E{} M{} H{}), {} calendar days, {} ms",
                        stats->solved.all, stats->solved.easy, stats->solved.medium,
                        stats->solved.hard, stats->calendar.size(), ms_since(start)));
    CHECK(stats->solved.all > 0);
    CHECK(stats->questions.all > 3500);

    const auto status = client.user_status();
    REQUIRE_MESSAGE(status.has_value(), failure(status));
    CHECK_FALSE(status->signed_in);

    const auto missing = client.question("no-such-problem-leetvim");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().kind == Error::Kind::Api);

    const auto nobody = client.user_stats("no-such-user-leetvim-xq9");
    REQUIRE_FALSE(nobody.has_value());
    CHECK(nobody.error().kind == Error::Kind::Api);
}
