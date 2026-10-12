#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lv::leetcode {

enum class Difficulty : std::uint8_t { Unknown, Easy, Medium, Hard };
enum class Status : std::uint8_t { Todo, Attempted, Solved };

std::string_view to_string(Difficulty d); // "Easy", "Medium", "Hard", "?"
/// Accepts "Easy"/"EASY"/"easy" (and so on); anything else is Unknown.
Difficulty parse_difficulty(std::string_view s);
/// Maps LeetCode status strings: "ac"/"AC"/"SOLVED" -> Solved,
/// "notac"/"TRIED"/"ATTEMPTED" -> Attempted, everything else (null, "TO_DO") -> Todo.
Status parse_status(std::string_view s);

struct Tag {
    std::string name;
    std::string slug;

    bool operator==(const Tag&) const = default;
};

/// One row of the problem set.
struct ProblemSummary {
    std::string frontend_id; // shown to users, e.g. "1"
    int number = 0;          // frontend_id as an integer for sorting; 0 if not numeric
    std::string title;
    std::string slug;
    Difficulty difficulty = Difficulty::Unknown;
    double ac_rate = 0.0; // percent, 0-100
    bool paid_only = false;
    bool favorite = false;
    bool has_solution = false;
    bool has_video = false;
    Status status = Status::Todo;
    std::vector<Tag> tags;

    bool operator==(const ProblemSummary&) const = default;
};

struct CodeSnippet {
    std::string lang;      // "C++"
    std::string lang_slug; // "cpp"
    std::string code;

    bool operator==(const CodeSnippet&) const = default;
};

struct SimilarQuestion {
    std::string title;
    std::string slug;
    Difficulty difficulty = Difficulty::Unknown;

    bool operator==(const SimilarQuestion&) const = default;
};

/// Parsed `metaData` JSON of a question.
struct QuestionMeta {
    std::string function_name;            // "twoSum"; empty for design problems
    std::vector<std::string> param_names; // ["nums", "target"]
    std::string class_name;               // design problems: "LRUCache"
    bool database = false;                // SQL / pandas problems
    bool manual = false;

    bool operator==(const QuestionMeta&) const = default;
};

struct Question {
    std::string question_id; // internal id that run/submit expect
    std::string frontend_id;
    std::string title;
    std::string slug;
    std::string content_html; // empty for paid-only problems without premium
    Difficulty difficulty = Difficulty::Unknown;
    bool paid_only = false;
    int likes = 0;
    int dislikes = 0;
    std::string category; // "Algorithms", "Database", "Shell", "Concurrency", "pandas"
    std::vector<Tag> tags;
    std::vector<CodeSnippet> snippets;
    std::vector<std::string> hints; // HTML fragments
    std::vector<SimilarQuestion> similar;
    std::vector<std::string> example_testcases; // one entry per case, lines joined by '\n'
    std::string sample_testcase;
    QuestionMeta meta;
    std::int64_t total_accepted = 0;
    std::int64_t total_submissions = 0;
    std::string ac_rate; // as LeetCode formats it, e.g. "58.0%"
    Status status = Status::Todo;

    bool operator==(const Question&) const = default;

    [[nodiscard]] const CodeSnippet* snippet(std::string_view lang_slug) const;
    /// Number of input lines that make up one testcase: the parameter count for function
    /// problems, 2 for design problems, 1 for database problems, 0 when unknown (shell
    /// problems; treat the whole input as one case).
    [[nodiscard]] std::size_t lines_per_case() const;
};

/// Per-problem progress of the signed-in user, from one request.
struct StatusSnapshot {
    std::string username;                                 // empty when the session is not signed in
    std::vector<std::pair<std::string, Status>> statuses; // slug -> status
};

struct DailyChallenge {
    std::string date; // "2026-10-12"
    bool completed = false;
    ProblemSummary problem;
};

struct UserStatus {
    bool signed_in = false;
    std::string username;
    bool premium = false;
    std::string avatar;
};

struct PerDifficulty {
    int all = 0;
    int easy = 0;
    int medium = 0;
    int hard = 0;

    [[nodiscard]] int get(Difficulty d) const; // Unknown -> all
};

struct ContestRanking {
    int attended = 0;
    double rating = 0.0;
    int global_ranking = 0;
    int total_participants = 0;
    double top_percentage = 0.0;
};

struct RecentSubmission {
    std::string id;
    std::string title;
    std::string slug;
    std::string lang;
    std::int64_t timestamp = 0; // unix seconds
};

struct SkillCount {
    std::string name;
    std::string slug;
    std::string level; // "fundamental", "intermediate", "advanced"
    int solved = 0;
};

struct UserStats {
    std::string username;
    std::string real_name;
    int ranking = 0;
    int reputation = 0;
    PerDifficulty solved;            // accepted problems
    PerDifficulty questions;         // problems that exist
    PerDifficulty ac_submissions;    // accepted submissions
    PerDifficulty total_submissions; // all submissions
    /// Unix timestamp of a UTC midnight -> submissions that day (the current year window).
    std::map<std::int64_t, int> calendar;
    int streak = 0;
    int total_active_days = 0;
    std::vector<std::pair<std::string, int>> languages; // name -> problems solved
    std::vector<SkillCount> skills;
    std::optional<ContestRanking> contest;
    std::vector<RecentSubmission> recent_accepted;
    std::vector<std::string> badges;
};

/// Requires authentication.
struct StreakInfo {
    int count = 0;
    bool today_completed = false;
};

/// LeetCode judge status codes.
enum class Verdict : std::uint8_t {
    Unknown = 0,
    Accepted = 10,
    WrongAnswer = 11,
    MemoryLimitExceeded = 12,
    OutputLimitExceeded = 13,
    TimeLimitExceeded = 14,
    RuntimeError = 15,
    InternalError = 16,
    CompileError = 20,
    Timeout = 30,
};

/// Result of polling /submissions/detail/<id>/check/ for a run or a submission.
struct RunResult {
    bool finished = false; // state == "SUCCESS"
    std::string state;     // "PENDING", "STARTED", "SUCCESS"
    bool is_submission = false;
    Verdict verdict = Verdict::Unknown;
    std::string status_msg; // "Accepted", "Wrong Answer", ...
    bool run_success = false;
    std::string lang;
    std::string runtime; // "0 ms"
    std::string memory;  // "8.8 MB"
    std::optional<double> runtime_percentile;
    std::optional<double> memory_percentile;
    int total_correct = -1;
    int total_testcases = -1;
    std::string compare_result; // run: one '1'/'0' per case

    // Run (interpret) results, one entry per testcase.
    std::vector<std::string> answers;
    std::vector<std::string> expected_answers;
    std::vector<std::string> stdout_per_case;
    bool correct_answer = false;

    // Errors (full_* variants preferred when present).
    std::string compile_error;
    std::string runtime_error;

    // Submission failures: the failing case.
    std::string last_testcase;
    std::string expected_output;
    std::string code_output;
    std::string std_output;

    std::string submission_id;

    [[nodiscard]] bool accepted() const { return verdict == Verdict::Accepted; }
};

/// A histogram LeetCode shows after an accepted submission.
struct Distribution {
    std::vector<std::pair<std::string, double>> buckets; // bucket label -> percent of users
};

struct SubmissionDetail {
    std::string id;
    std::string runtime_display; // "3 ms"
    std::optional<double> runtime_percentile;
    Distribution runtime_distribution;
    std::string memory_display; // "14.2 MB"
    std::optional<double> memory_percentile;
    Distribution memory_distribution;
    std::string code;
    std::string lang;
    std::int64_t timestamp = 0;
    Verdict verdict = Verdict::Unknown;
};

struct SubmissionSummary {
    std::string id;
    std::string status; // "Accepted", "Wrong Answer", ...
    std::string lang;
    std::string runtime;
    std::string memory;
    std::int64_t timestamp = 0;
    bool accepted = false;
};

} // namespace lv::leetcode
