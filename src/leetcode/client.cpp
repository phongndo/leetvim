#include "leetcode/client.hpp"

#include "leetcode/http.hpp"
#include "leetcode/parse.hpp"
#include "leetcode/response.hpp"
#include "util/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <format>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>

namespace lv::leetcode {

namespace {

using Json = nlohmann::json;

constexpr int page_size = 100; // the API's maximum
constexpr int max_page_workers = 6;

constexpr std::string_view problems_query = R"(
query problemsetQuestionList($categorySlug: String, $limit: Int, $skip: Int, $filters: QuestionListFilterInput) {
  problemsetQuestionList: questionList(categorySlug: $categorySlug, limit: $limit, skip: $skip, filters: $filters) {
    total: totalNum
    questions: data {
      acRate difficulty frontendQuestionId: questionFrontendId isFavor paidOnly: isPaidOnly status
      title titleSlug topicTags { name slug } hasSolution hasVideoSolution
    }
  }
})";

constexpr std::string_view question_query = R"(
query questionData($titleSlug: String!) {
  question(titleSlug: $titleSlug) {
    questionId questionFrontendId title titleSlug content isPaidOnly difficulty likes dislikes
    exampleTestcaseList sampleTestCase topicTags { name slug } codeSnippets { lang langSlug code }
    stats hints similarQuestions metaData status categoryTitle
  }
})";

constexpr std::string_view daily_query = R"(
query questionOfToday {
  activeDailyCodingChallengeQuestion {
    date userStatus link
    question {
      acRate difficulty frontendQuestionId: questionFrontendId isFavor paidOnly: isPaidOnly status
      title titleSlug topicTags { name slug } hasSolution hasVideoSolution
    }
  }
})";

constexpr std::string_view user_status_query = R"(
query globalData { userStatus { username isSignedIn isPremium avatar } })";

constexpr std::string_view user_stats_query = R"(
query userProfile($username: String!) {
  allQuestionsCount { difficulty count }
  matchedUser(username: $username) {
    username
    profile { ranking realName userAvatar reputation }
    submitStats {
      acSubmissionNum { difficulty count submissions }
      totalSubmissionNum { difficulty count submissions }
    }
    userCalendar { activeYears streak totalActiveDays submissionCalendar }
    languageProblemCount { languageName problemsSolved }
    tagProblemCounts {
      advanced { tagName tagSlug problemsSolved }
      intermediate { tagName tagSlug problemsSolved }
      fundamental { tagName tagSlug problemsSolved }
    }
    badges { name }
  }
  userContestRanking(username: $username) {
    attendedContestsCount rating globalRanking totalParticipants topPercentage
  }
  recentAcSubmissionList(username: $username, limit: 15) { id title titleSlug timestamp lang }
})";

constexpr std::string_view streak_query = R"(
query getStreakCounter { streakCounter { streakCount daysSkipped currentDayCompleted } })";

constexpr std::string_view submission_detail_query = R"(
query submissionDetails($submissionId: Int!) {
  submissionDetails(submissionId: $submissionId) {
    runtime runtimeDisplay runtimePercentile runtimeDistribution
    memory memoryDisplay memoryPercentile memoryDistribution
    code timestamp statusCode lang { name verboseName }
  }
})";

constexpr std::string_view submission_list_query = R"(
query submissionList($offset: Int!, $limit: Int!, $lastKey: String, $questionSlug: String!) {
  questionSubmissionList(offset: $offset, limit: $limit, lastKey: $lastKey, questionSlug: $questionSlug) {
    lastKey hasNext
    submissions { id title titleSlug status statusDisplay lang langName runtime timestamp url isPending memory }
  }
})";

Error not_signed_in() {
    return Error{.kind = Error::Kind::Auth, .message = "Not signed in to LeetCode"};
}

Error cancelled() { return Error{.kind = Error::Kind::Cancelled, .message = "Cancelled"}; }

/// A stop token that fires when either the caller's token or Client::abort_all() does.
class LinkedStop {
public:
    explicit LinkedStop(std::stop_token stop)
        : caller_(std::move(stop), Forward{&source_}),
          abort_(http::abort_token(), Forward{&source_}) {}

    [[nodiscard]] std::stop_token token() const { return source_.get_token(); }
    [[nodiscard]] bool stopped() const { return source_.stop_requested(); }

private:
    struct Forward {
        std::stop_source* target;
        void operator()() const { target->request_stop(); }
    };

    std::stop_source source_;
    std::stop_callback<Forward> caller_;
    std::stop_callback<Forward> abort_;
};

std::string dump(const Json& j) { return j.dump(-1, ' ', false, Json::error_handler_t::replace); }

std::vector<std::string> headers_for(const Client& client, std::string_view referer) {
    std::vector<std::string> headers{
        "Accept: application/json, text/plain, */*",
        "Content-Type: application/json",
        std::format("Referer: {}", referer),
        std::format("Origin: {}", client.base_url()),
    };
    if (client.has_credentials()) {
        const auto& creds = client.credentials();
        headers.push_back(
            std::format("Cookie: LEETCODE_SESSION={}; csrftoken={}", creds.session, creds.csrf));
        headers.push_back(std::format("x-csrftoken: {}", creds.csrf));
    }
    return headers;
}

/// Performs the request; non-2xx statuses become errors.
Result<std::string> send(const Client& client, const http::Request& request,
                         const std::stop_token& stop) {
    auto response = http::perform(request, stop);
    if (!response) {
        return std::unexpected(response.error());
    }
    if (response->status >= 200 && response->status < 300) {
        return std::move(response->body);
    }
    auto error = status_error(response->status, response->body, client.has_credentials());
    log::debug("{} -> {}", request.url, error.message);
    return std::unexpected(std::move(error));
}

Result<std::string> graphql(const Client& client, std::string_view query, Json variables,
                            std::string_view referer, const std::stop_token& stop = {}) {
    const Json payload = {{"query", query}, {"variables", std::move(variables)}};
    const http::Request request{
        .url = client.base_url() + "/graphql",
        .headers = headers_for(client, referer),
        .post = true,
        .body = dump(payload),
    };
    return send(client, request, stop);
}

Result<std::string> post_json(const Client& client, std::string_view path, std::string_view slug,
                              const Json& payload) {
    const http::Request request{
        .url = client.base_url() + std::string(path),
        .headers = headers_for(client, client.problem_url(slug)),
        .post = true,
        .body = dump(payload),
    };
    return send(client, request, {});
}

template <typename T>
Result<T> then_parse(const Result<std::string>& body, Result<T> (*parse)(std::string_view)) {
    if (!body) {
        return std::unexpected(body.error());
    }
    return parse(*body);
}

Result<ProblemPage> fetch_page(const Client& client, int skip, const std::stop_token& stop) {
    const Json variables = {
        {"categorySlug", ""},
        {"limit", page_size},
        {"skip", skip},
        {"filters", Json::object()},
    };
    return then_parse(
        graphql(client, problems_query, variables, client.base_url() + "/problemset/", stop),
        parse_problem_page);
}

Result<RunResult> check_once(const Client& client, std::string_view id, std::string_view slug,
                             const std::stop_token& stop) {
    if (!client.has_credentials()) {
        return std::unexpected(not_signed_in());
    }
    if (id.empty()) {
        return std::unexpected(Error{.message = "Missing submission id"});
    }
    const http::Request request{
        .url = std::format("{}/submissions/detail/{}/check/", client.base_url(), id),
        .headers = headers_for(client, client.problem_url(slug)),
    };
    auto result = then_parse(send(client, request, stop), parse_check);
    if (result && result->submission_id.empty()) {
        result->submission_id = id;
    }
    return result;
}

/// Fetches pages 1.. of the problem list on a few threads, reporting progress from the
/// calling thread. `pages` holds page 0 already.
std::optional<Error> fetch_remaining_pages(const Client& client,
                                           std::vector<std::vector<ProblemSummary>>& pages,
                                           int total, const Client::Progress& progress,
                                           const std::stop_token& stop) {
    std::mutex mutex;
    std::condition_variable changed;
    std::size_t next_page = 1;
    int fetched = static_cast<int>(pages[0].size());
    int running = 0;
    std::optional<Error> failure;
    std::stop_source abort;
    const std::stop_callback forward(stop, [&abort] { abort.request_stop(); });

    const auto worker = [&] {
        const auto token = abort.get_token();
        while (true) {
            std::size_t page = 0;
            {
                const std::scoped_lock lock(mutex);
                if (failure || next_page >= pages.size() || token.stop_requested()) {
                    break;
                }
                page = next_page++;
            }
            auto result = fetch_page(client, static_cast<int>(page) * page_size, token);
            const std::scoped_lock lock(mutex);
            if (result) {
                fetched += static_cast<int>(result->problems.size());
                pages[page] = std::move(result->problems);
            } else if (!failure) {
                failure = std::move(result.error());
                abort.request_stop();
            }
            changed.notify_all();
        }
        const std::scoped_lock lock(mutex);
        --running;
        changed.notify_all();
    };

    std::vector<std::jthread> threads;
    const auto workers = std::min<std::size_t>(max_page_workers, pages.size() - 1);
    for (std::size_t i = 0; i < workers; ++i) {
        const std::scoped_lock lock(mutex);
        try {
            threads.emplace_back(worker);
            ++running;
        } catch (const std::system_error&) {
            break;
        }
    }
    if (threads.empty() && pages.size() > 1) {
        running = 1;
        worker();
    }

    std::unique_lock lock(mutex);
    int reported = -1;
    while (true) {
        changed.wait(lock, [&] { return running == 0 || fetched != reported; });
        if (fetched != reported) {
            reported = fetched;
            if (progress) {
                lock.unlock();
                progress(reported, total);
                lock.lock();
            }
        }
        if (running == 0 && fetched == reported) {
            break;
        }
    }
    lock.unlock();
    threads.clear();
    return failure;
}

} // namespace

Client::Client(Credentials creds, std::string base_url)
    : creds_(std::move(creds)), base_url_(std::move(base_url)) {
    while (base_url_.ends_with('/')) {
        base_url_.pop_back();
    }
}

void Client::abort_all() { http::abort_all(); }

Result<std::vector<ProblemSummary>> Client::problems(const Progress& progress,
                                                     std::stop_token stop) const {
    const auto started = std::chrono::steady_clock::now();
    const LinkedStop linked(std::move(stop));
    auto first = fetch_page(*this, 0, linked.token());
    if (!first) {
        return std::unexpected(first.error());
    }
    const int total = std::max(first->total, static_cast<int>(first->problems.size()));
    std::vector<std::vector<ProblemSummary>> pages(
        static_cast<std::size_t>(std::max(1, (total + page_size - 1) / page_size)));
    pages[0] = std::move(first->problems);

    auto failure = fetch_remaining_pages(*this, pages, total, progress, linked.token());
    if (linked.stopped()) {
        return std::unexpected(cancelled());
    }
    if (failure) {
        return std::unexpected(std::move(*failure));
    }

    // Pages can overlap if problems were added mid-fetch.
    std::vector<ProblemSummary> all;
    all.reserve(static_cast<std::size_t>(total));
    std::unordered_set<std::string> seen;
    for (auto& page : pages) {
        for (auto& p : page) {
            if (seen.insert(p.slug).second) {
                all.push_back(std::move(p));
            }
        }
    }
    log::debug("fetched {} problems in {} pages ({})", all.size(), pages.size(),
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - started));
    return all;
}

Result<Question> Client::question(std::string_view slug) const {
    return then_parse(graphql(*this, question_query, {{"titleSlug", slug}}, problem_url(slug)),
                      parse_question);
}

Result<DailyChallenge> Client::daily() const {
    return then_parse(graphql(*this, daily_query, Json::object(), base_url_ + "/problemset/"),
                      parse_daily);
}

Result<UserStatus> Client::user_status() const {
    return then_parse(graphql(*this, user_status_query, Json::object(), base_url_ + "/"),
                      parse_user_status);
}

Result<UserStats> Client::user_stats(std::string_view username) const {
    const std::string referer = std::format("{}/u/{}/", base_url_, username);
    return then_parse(graphql(*this, user_stats_query, {{"username", username}}, referer),
                      parse_user_stats);
}

Result<StreakInfo> Client::streak() const {
    if (!has_credentials()) {
        return std::unexpected(not_signed_in());
    }
    return then_parse(graphql(*this, streak_query, Json::object(), base_url_ + "/problemset/"),
                      parse_streak);
}

Result<StatusSnapshot> Client::statuses() const {
    if (!has_credentials()) {
        return std::unexpected(not_signed_in());
    }
    const http::Request request{
        .url = base_url_ + "/api/problems/all/",
        .headers = headers_for(*this, base_url_ + "/problemset/"),
    };
    return then_parse(send(*this, request, {}), parse_statuses);
}

Result<std::string> Client::run(const Question& q, std::string_view lang, std::string_view code,
                                std::string_view input) const {
    if (!has_credentials()) {
        return std::unexpected(not_signed_in());
    }
    const Json payload = {
        {"lang", lang},
        {"question_id", q.question_id},
        {"typed_code", code},
        {"data_input", input},
    };
    return then_parse(
        post_json(*this, std::format("/problems/{}/interpret_solution/", q.slug), q.slug, payload),
        parse_run_id);
}

Result<std::string> Client::submit(const Question& q, std::string_view lang,
                                   std::string_view code) const {
    if (!has_credentials()) {
        return std::unexpected(not_signed_in());
    }
    const Json payload = {{"lang", lang}, {"question_id", q.question_id}, {"typed_code", code}};
    return then_parse(
        post_json(*this, std::format("/problems/{}/submit/", q.slug), q.slug, payload),
        parse_run_id);
}

Result<RunResult> Client::check(std::string_view id, std::string_view slug) const {
    return check_once(*this, id, slug, {});
}

Result<RunResult> Client::wait(std::string_view id, std::string_view slug, std::stop_token stop,
                               const std::function<void(const RunResult&)>& on_progress) const {
    using namespace std::chrono_literals;
    constexpr int max_network_retries = 3;
    const auto deadline = std::chrono::steady_clock::now() + 2min;
    std::chrono::milliseconds delay = 700ms;
    int network_failures = 0;
    const LinkedStop linked(std::move(stop));
    std::mutex mutex;
    std::condition_variable_any sleeper;

    while (true) {
        auto result = check_once(*this, id, slug, linked.token());
        if (result) {
            if (result->finished) {
                return result;
            }
            network_failures = 0;
            if (on_progress) {
                on_progress(*result);
            }
        } else if (result.error().kind == Error::Kind::RateLimited) {
            delay *= 2;
        } else if (result.error().kind != Error::Kind::Network
                   || ++network_failures > max_network_retries) {
            return result;
        }
        if (linked.stopped()) {
            return std::unexpected(cancelled());
        }
        if (std::chrono::steady_clock::now() + delay > deadline) {
            return std::unexpected(Error{
                .kind = Error::Kind::Network,
                .message = "LeetCode's judge did not finish in time",
            });
        }
        std::unique_lock lock(mutex);
        sleeper.wait_for(lock, linked.token(), delay, [] { return false; });
        if (linked.stopped()) {
            return std::unexpected(cancelled());
        }
        delay = std::min<std::chrono::milliseconds>(delay * 5 / 4, 3s);
    }
}

Result<SubmissionDetail> Client::submission_detail(std::string_view id) const {
    if (!has_credentials()) {
        return std::unexpected(not_signed_in());
    }
    std::int64_t number = 0;
    const char* first = id.data();
    const char* last = first + id.size();
    if (const auto [end, ec] = std::from_chars(first, last, number);
        ec != std::errc{} || end != last || id.empty()) {
        return std::unexpected(Error{.message = std::format("Invalid submission id: {}", id)});
    }
    const std::string referer = std::format("{}/submissions/detail/{}/", base_url_, id);
    auto detail =
        then_parse(graphql(*this, submission_detail_query, {{"submissionId", number}}, referer),
                   parse_submission_detail);
    if (detail) {
        detail->id = id;
    }
    return detail;
}

Result<std::vector<SubmissionSummary>> Client::submissions(std::string_view slug, int limit) const {
    if (!has_credentials()) {
        return std::unexpected(not_signed_in());
    }
    const Json variables = {
        {"offset", 0},
        {"limit", limit},
        {"lastKey", nullptr},
        {"questionSlug", slug},
    };
    return then_parse(graphql(*this, submission_list_query, variables, problem_url(slug)),
                      parse_submission_list);
}

std::string Client::problem_url(std::string_view slug) const {
    return std::format("{}/problems/{}/", base_url_, slug);
}

} // namespace lv::leetcode
