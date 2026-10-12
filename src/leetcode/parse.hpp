#pragma once

#include "leetcode/error.hpp"
#include "leetcode/models.hpp"

#include <string_view>
#include <vector>

// Pure JSON -> model parsers for LeetCode responses. Each takes the full response body
// (including the GraphQL {"data": ...} envelope where applicable). GraphQL "errors" become
// Error::Kind::Api; shape mismatches become Error::Kind::Parse. Never throw.
namespace lv::leetcode {

struct ProblemPage {
    int total = 0;
    std::vector<ProblemSummary> problems;
};

Result<ProblemPage> parse_problem_page(std::string_view body);
Result<Question> parse_question(std::string_view body);
Result<DailyChallenge> parse_daily(std::string_view body);
Result<UserStatus> parse_user_status(std::string_view body);
Result<UserStats> parse_user_stats(std::string_view body);
Result<StreakInfo> parse_streak(std::string_view body);
/// Body of GET /api/problems/all/.
Result<StatusSnapshot> parse_statuses(std::string_view body);
/// Body of POST interpret_solution/ -> interpret_id, or POST submit/ -> submission_id.
Result<std::string> parse_run_id(std::string_view body);
Result<RunResult> parse_check(std::string_view body);
Result<SubmissionDetail> parse_submission_detail(std::string_view body);
Result<std::vector<SubmissionSummary>> parse_submission_list(std::string_view body);

} // namespace lv::leetcode
