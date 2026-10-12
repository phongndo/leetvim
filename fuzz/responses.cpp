// Every LeetCode API response parser, fed arbitrary bodies: they must fail cleanly.
#include "leetcode/parse.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    namespace lc = lv::leetcode;
    const std::string_view body(reinterpret_cast<const char*>(data), size);
    (void)lc::parse_problem_page(body);
    (void)lc::parse_question(body);
    (void)lc::parse_daily(body);
    (void)lc::parse_user_status(body);
    (void)lc::parse_user_stats(body);
    (void)lc::parse_streak(body);
    (void)lc::parse_statuses(body);
    (void)lc::parse_run_id(body);
    (void)lc::parse_check(body);
    (void)lc::parse_submission_detail(body);
    (void)lc::parse_submission_list(body);
    return 0;
}
