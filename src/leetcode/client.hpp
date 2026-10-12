#pragma once

#include "leetcode/credentials.hpp"
#include "leetcode/error.hpp"
#include "leetcode/models.hpp"

#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace lv::leetcode {

/// Blocking LeetCode API client. Methods are const and safe to call concurrently from
/// different threads (each thread uses its own HTTP connection). Long-running calls take a
/// stop_token and return Error::Kind::Cancelled when stopped.
class Client {
public:
    explicit Client(Credentials creds = Credentials(),
                    std::string base_url = "https://leetcode.com");

    [[nodiscard]] const Credentials& credentials() const { return creds_; }
    [[nodiscard]] bool has_credentials() const { return !creds_.empty(); }
    [[nodiscard]] const std::string& base_url() const { return base_url_; }

    /// Aborts every in-flight and future request in the process, e.g. when quitting; they
    /// fail with Error::Kind::Cancelled. Irreversible.
    static void abort_all();

    using Progress = std::function<void(int done, int total)>;

    /// The whole problem set (all categories), fetched page by page in parallel.
    /// Statuses are filled in when authenticated.
    [[nodiscard]] Result<std::vector<ProblemSummary>> problems(const Progress& progress = {},
                                                               std::stop_token stop = {}) const;
    [[nodiscard]] Result<Question> question(std::string_view slug) const;
    [[nodiscard]] Result<DailyChallenge> daily() const;
    [[nodiscard]] Result<UserStatus> user_status() const;
    [[nodiscard]] Result<UserStats> user_stats(std::string_view username) const;
    [[nodiscard]] Result<StreakInfo> streak() const;
    /// Solved/attempted status of every problem for the signed-in user, in one request.
    [[nodiscard]] Result<StatusSnapshot> statuses() const;

    /// Starts a test run on LeetCode's judge with `input` (testcases joined by '\n').
    /// Returns the interpret id to poll.
    [[nodiscard]] Result<std::string> run(const Question& q, std::string_view lang,
                                          std::string_view code, std::string_view input) const;
    /// Submits a solution. Returns the submission id to poll.
    [[nodiscard]] Result<std::string> submit(const Question& q, std::string_view lang,
                                             std::string_view code) const;
    /// One poll of a run or submission.
    [[nodiscard]] Result<RunResult> check(std::string_view id, std::string_view slug) const;
    /// Polls until the judge finishes (or ~2 minutes pass), reporting intermediate states.
    [[nodiscard]] Result<RunResult>
    wait(std::string_view id, std::string_view slug, std::stop_token stop = {},
         const std::function<void(const RunResult&)>& on_progress = {}) const;

    [[nodiscard]] Result<SubmissionDetail> submission_detail(std::string_view id) const;
    [[nodiscard]] Result<std::vector<SubmissionSummary>> submissions(std::string_view slug,
                                                                     int limit = 20) const;

    [[nodiscard]] std::string problem_url(std::string_view slug) const;

private:
    Credentials creds_;
    std::string base_url_;
};

} // namespace lv::leetcode
