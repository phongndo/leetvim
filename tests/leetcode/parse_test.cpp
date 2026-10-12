#include "leetcode/parse.hpp"
#include "leetcode/response.hpp"
#include "support.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace lv::leetcode;
using lv::test::fixture;

TEST_CASE("problem list page") {
    const auto page = parse_problem_page(fixture("problem-list.json"));
    REQUIRE(page.has_value());
    CHECK(page->total == 4081);
    REQUIRE(page->problems.size() == 5);
    const auto& first = page->problems[0];
    CHECK(first.frontend_id == "1");
    CHECK(first.number == 1);
    CHECK(first.title == "Two Sum");
    CHECK(first.slug == "two-sum");
    CHECK(first.difficulty == Difficulty::Easy);
    CHECK(first.ac_rate == doctest::Approx(58.002));
    CHECK_FALSE(first.paid_only);
    CHECK_FALSE(first.favorite);
    CHECK(first.has_solution);
    CHECK(first.has_video);
    CHECK(first.status == Status::Todo);
    CHECK(first.tags == std::vector<Tag>{{"Array", "array"}, {"Hash Table", "hash-table"}});
    CHECK(page->problems[3].difficulty == Difficulty::Hard);
    CHECK(page->problems[4].tags.size() == 4);
}

TEST_CASE("problem list tolerates statuses, string numbers and odd ids") {
    const auto page = parse_problem_page(R"({"data": {"problemsetQuestionList": {"total": "3",
        "questions": [
          {"acRate": "47.5", "difficulty": "MEDIUM", "frontendQuestionId": "LCP 01",
           "isFavor": true, "paidOnly": true, "status": "ac", "title": "A", "titleSlug": "a",
           "topicTags": null, "hasSolution": null, "hasVideoSolution": false},
          {"acRate": 12, "difficulty": "Hard", "frontendQuestionId": 42, "status": "notac",
           "title": "B", "titleSlug": "b", "topicTags": []},
          {"title": "no slug"}
        ]}}})");
    REQUIRE(page.has_value());
    CHECK(page->total == 3);
    REQUIRE(page->problems.size() == 2); // entries without a slug are dropped
    const auto& a = page->problems[0];
    CHECK(a.number == 0);
    CHECK(a.ac_rate == doctest::Approx(47.5));
    CHECK(a.difficulty == Difficulty::Medium);
    CHECK(a.paid_only);
    CHECK(a.favorite);
    CHECK(a.status == Status::Solved);
    CHECK(a.tags.empty());
    const auto& b = page->problems[1];
    CHECK(b.frontend_id == "42");
    CHECK(b.number == 42);
    CHECK(b.status == Status::Attempted);
}

TEST_CASE("malformed bodies and error envelopes") {
    for (const auto* body : {"", "not json", "<!DOCTYPE html><html></html>", "[1,2]"}) {
        const auto page = parse_problem_page(body);
        REQUIRE_FALSE(page.has_value());
        CHECK(page.error().kind == Error::Kind::Parse);
    }

    const auto no_data = parse_problem_page(R"({"something": 1})");
    REQUIRE_FALSE(no_data.has_value());
    CHECK(no_data.error().kind == Error::Kind::Parse);

    const auto graphql = parse_problem_page(
        R"({"errors": [{"message": "Cannot query field \"nope\" on type \"Query\"."}]})");
    REQUIRE_FALSE(graphql.has_value());
    CHECK(graphql.error().kind == Error::Kind::Api);
    CHECK(graphql.error().message == "Cannot query field \"nope\" on type \"Query\".");

    const auto wrong_shape = parse_problem_page(R"({"data": {"problemsetQuestionList": []}})");
    REQUIRE_FALSE(wrong_shape.has_value());
    CHECK(wrong_shape.error().kind == Error::Kind::Parse);

    const auto limited = parse_problem_page(
        R"({"errors": [{"message": "Too many requests, slow down"}], "data": null})");
    REQUIRE_FALSE(limited.has_value());
    CHECK(limited.error().kind == Error::Kind::RateLimited);
}

TEST_CASE("question detail: function problem") {
    const auto q = parse_question(fixture("question-two-sum.json"));
    REQUIRE(q.has_value());
    CHECK(q->question_id == "1");
    CHECK(q->frontend_id == "1");
    CHECK(q->title == "Two Sum");
    CHECK(q->slug == "two-sum");
    CHECK(q->content_html.starts_with("<p>You are given an array of integers"));
    CHECK(q->difficulty == Difficulty::Easy);
    CHECK_FALSE(q->paid_only);
    CHECK(q->likes == 70705);
    CHECK(q->dislikes == 2641);
    CHECK(q->category == "Algorithms");
    CHECK(q->tags.size() == 2);
    CHECK(q->snippets.size() > 10);
    CHECK(q->hints.size() == 3);
    REQUIRE_FALSE(q->similar.empty());
    CHECK(q->similar[0].title == "3Sum");
    CHECK(q->similar[0].slug == "3sum");
    CHECK(q->similar[0].difficulty == Difficulty::Medium);
    CHECK(q->example_testcases
          == std::vector<std::string>{"[2,7,11,15]\n9", "[3,2,4]\n6", "[3,3]\n6"});
    CHECK(q->sample_testcase == "[2,7,11,15]\n9");
    CHECK(q->meta.function_name == "twoSum");
    CHECK(q->meta.param_names == std::vector<std::string>{"nums", "target"});
    CHECK(q->meta.class_name.empty());
    CHECK_FALSE(q->meta.database);
    CHECK_FALSE(q->meta.manual);
    CHECK(q->total_accepted == 23771382);
    CHECK(q->total_submissions == 40983640);
    CHECK(q->ac_rate == "58.0%");
    CHECK(q->status == Status::Todo);
}

TEST_CASE("question detail: design, database and paid-only problems") {
    const auto lru = parse_question(fixture("question-lru-cache.json"));
    REQUIRE(lru.has_value());
    CHECK(lru->meta.class_name == "LRUCache");
    CHECK(lru->meta.function_name.empty());
    CHECK(lru->meta.param_names.empty());
    CHECK(lru->example_testcases.size() == 1);

    const auto sql = parse_question(fixture("question-combine-two-tables.json"));
    REQUIRE(sql.has_value());
    CHECK(sql->meta.database);
    CHECK(sql->meta.manual);
    CHECK(sql->category == "Database");
    CHECK(sql->snippet("mysql") != nullptr);
    CHECK(sql->example_testcases.size() == 1);

    const auto paid = parse_question(fixture("question-paid-only.json"));
    REQUIRE(paid.has_value());
    CHECK(paid->paid_only);
    CHECK(paid->content_html.empty());
    CHECK(paid->snippets.empty());
    CHECK(paid->meta.function_name == "canAttendMeetings");
    CHECK(paid->example_testcases.size() == 2);
}

TEST_CASE("question detail: missing question and broken nested JSON") {
    const auto missing = parse_question(R"({"data": {"question": null}})");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().kind == Error::Kind::Api);

    const auto q = parse_question(R"({"data": {"question": {
        "questionId": 7, "questionFrontendId": "7", "title": "T", "titleSlug": "t",
        "content": null, "codeSnippets": null, "likes": "12", "metaData": "{not json",
        "stats": "also not json", "similarQuestions": null, "hints": null,
        "exampleTestcaseList": null, "sampleTestCase": "1\n2"}}})");
    REQUIRE(q.has_value());
    CHECK(q->question_id == "7");
    CHECK(q->likes == 12);
    CHECK(q->meta == QuestionMeta{});
    CHECK(q->total_accepted == 0);
    CHECK(q->ac_rate.empty());
    CHECK(q->example_testcases == std::vector<std::string>{"1\n2"});
}

TEST_CASE("daily challenge") {
    const auto daily = parse_daily(fixture("daily.json"));
    REQUIRE(daily.has_value());
    CHECK(daily->date == "2026-10-12");
    CHECK_FALSE(daily->completed);
    CHECK(daily->problem.slug == "apply-operations-on-array-to-maximize-sum-of-squares");
    CHECK(daily->problem.number == 2897);
    CHECK(daily->problem.difficulty == Difficulty::Hard);
    CHECK(daily->problem.tags.size() == 4);
    CHECK_FALSE(daily->problem.has_video);

    const auto done = parse_daily(R"({"data": {"activeDailyCodingChallengeQuestion": {
        "date": "2026-10-11", "userStatus": "Finish",
        "question": {"frontendQuestionId": "1", "title": "Two Sum", "titleSlug": "two-sum",
                     "status": "ac"}}}})");
    REQUIRE(done.has_value());
    CHECK(done->completed);
    CHECK(done->problem.status == Status::Solved);

    const auto none = parse_daily(R"({"data": {"activeDailyCodingChallengeQuestion": null}})");
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error().kind == Error::Kind::Api);
}

TEST_CASE("user status") {
    const auto anon = parse_user_status(
        R"({"data":{"userStatus":{"username":"","isSignedIn":false,"isPremium":null,"avatar":null}}})");
    REQUIRE(anon.has_value());
    CHECK_FALSE(anon->signed_in);
    CHECK(anon->username.empty());
    CHECK_FALSE(anon->premium);
    CHECK(anon->avatar.empty());

    const auto me = parse_user_status(
        R"({"data":{"userStatus":{"username":"dp","isSignedIn":true,"isPremium":true,"avatar":"https://x/a.png"}}})");
    REQUIRE(me.has_value());
    CHECK(me->signed_in);
    CHECK(me->username == "dp");
    CHECK(me->premium);
    CHECK(me->avatar == "https://x/a.png");
}

TEST_CASE("user stats") {
    const auto stats = parse_user_stats(fixture("user-profile.json"));
    REQUIRE(stats.has_value());
    CHECK(stats->username == "lee215");
    CHECK(stats->real_name == "lee");
    CHECK(stats->ranking == 103905);
    CHECK(stats->reputation == 231153);
    CHECK(stats->questions.all == 4081);
    CHECK(stats->questions.hard == 982);
    CHECK(stats->solved.all == 686);
    CHECK(stats->solved.easy == 126);
    CHECK(stats->solved.medium == 411);
    CHECK(stats->solved.hard == 149);
    CHECK(stats->ac_submissions.all == 2115);
    CHECK(stats->total_submissions.all == 4936);
    CHECK(stats->total_submissions.hard == 1343);
    CHECK(stats->calendar.size() == 48);
    CHECK(stats->calendar.at(1767398400) == 3);
    CHECK(stats->calendar.at(1762560000) == 10);
    CHECK(stats->streak == 3);
    CHECK(stats->total_active_days == 48);
    REQUIRE(stats->languages.size() == 7);
    CHECK(stats->languages[0] == std::pair<std::string, int>{"C++", 151});
    REQUIRE_FALSE(stats->skills.empty());
    CHECK(stats->skills.front().level == "fundamental");
    CHECK(stats->skills.back().level == "advanced");
    const auto dp = std::ranges::find(stats->skills, "dynamic-programming", &SkillCount::slug);
    REQUIRE(dp != stats->skills.end());
    CHECK(dp->solved > 0);
    REQUIRE(stats->contest.has_value());
    const auto contest = stats->contest.value_or(ContestRanking{});
    CHECK(contest.attended == 27);
    CHECK(contest.rating == doctest::Approx(2204.316));
    CHECK(contest.global_ranking == 7558);
    CHECK(contest.total_participants == 887132);
    CHECK(contest.top_percentage == doctest::Approx(0.9));
    REQUIRE_FALSE(stats->recent_accepted.empty());
    CHECK(stats->recent_accepted[0].id == "2161702491");
    CHECK(stats->recent_accepted[0].timestamp == 1791086655);
    CHECK(stats->recent_accepted[0].lang == "java");
    CHECK(stats->badges.size() == 3);
}

TEST_CASE("user stats: unknown user and no contest history") {
    const auto missing = parse_user_stats(
        R"({"errors":[{"message":"That user does not exist.","path":["matchedUser"]}],
            "data":{"matchedUser":null,"userContestRanking":null}})");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().kind == Error::Kind::Api);
    CHECK(missing.error().message == "That user does not exist.");

    const auto fresh = parse_user_stats(R"({"data": {
        "allQuestionsCount": [{"difficulty": "All", "count": 10}],
        "matchedUser": {"username": "new", "profile": null, "submitStats": null,
                        "userCalendar": {"streak": 0, "submissionCalendar": "{}"}},
        "userContestRanking": null, "recentAcSubmissionList": []}})");
    REQUIRE(fresh.has_value());
    CHECK(fresh->username == "new");
    CHECK(fresh->questions.all == 10);
    CHECK_FALSE(fresh->contest.has_value());
    CHECK(fresh->calendar.empty());
}

TEST_CASE("streak") {
    const auto streak = parse_streak(
        R"({"data":{"streakCounter":{"streakCount":12,"daysSkipped":0,"currentDayCompleted":true}}})");
    REQUIRE(streak.has_value());
    CHECK(streak->count == 12);
    CHECK(streak->today_completed);

    const auto anon = parse_streak(R"({"data":{"streakCounter":null}})");
    REQUIRE_FALSE(anon.has_value());
    CHECK(anon.error().kind == Error::Kind::Auth);
}

TEST_CASE("run and submit ids") {
    CHECK(parse_run_id(R"({"interpret_id": "runcode_1_abc", "test_case": "[1]\n2"})")
          == "runcode_1_abc");
    CHECK(parse_run_id(R"({"submission_id": 1791090123})") == "1791090123");
    CHECK(parse_run_id(R"({"submission_id": "1791090123"})") == "1791090123");

    const auto limited = parse_run_id(R"({"error": "You have attempted to run code too soon."})");
    REQUIRE_FALSE(limited.has_value());
    CHECK(limited.error().kind == Error::Kind::RateLimited);

    const auto api = parse_run_id(R"({"error": "Something broke"})");
    REQUIRE_FALSE(api.has_value());
    CHECK(api.error().kind == Error::Kind::Api);
    CHECK(api.error().message == "Something broke");

    const auto empty = parse_run_id("{}");
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().kind == Error::Kind::Parse);
}

TEST_CASE("check: pending states") {
    const auto pending = parse_check(fixture("check-pending.json"));
    REQUIRE(pending.has_value());
    CHECK(pending->state == "PENDING");
    CHECK_FALSE(pending->finished);

    const auto started = parse_check(R"({"state": "STARTED"})");
    REQUIRE(started.has_value());
    CHECK_FALSE(started->finished);

    const auto bad = parse_check(R"({"foo": 1})");
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().kind == Error::Kind::Parse);
}

TEST_CASE("check: accepted run") {
    const auto r = parse_check(fixture("check-run-accepted.json"));
    REQUIRE(r.has_value());
    CHECK(r->finished);
    CHECK_FALSE(r->is_submission);
    CHECK(r->accepted());
    CHECK(r->status_msg == "Accepted");
    CHECK(r->run_success);
    CHECK(r->lang == "cpp");
    CHECK(r->runtime == "0 ms");
    CHECK(r->memory == "8.8 MB");
    CHECK_FALSE(r->runtime_percentile.has_value());
    CHECK(r->total_correct == 3);
    CHECK(r->total_testcases == 3);
    CHECK(r->compare_result == "111");
    CHECK(r->correct_answer);
    CHECK(r->answers == std::vector<std::string>{"[0,1]", "[1,2]", "[0,1]"});
    CHECK(r->expected_answers == r->answers); // trailing "" dropped
    CHECK(r->stdout_per_case == std::vector<std::string>{"", "", ""});
    CHECK(r->submission_id == "runcode_1791089999.4512345_abcdEFgh12");
}

TEST_CASE("check: run with a wrong answer and string-typed numbers") {
    const auto r = parse_check(fixture("check-run-wrong.json"));
    REQUIRE(r.has_value());
    CHECK_FALSE(r->is_submission);
    CHECK(r->verdict == Verdict::WrongAnswer); // "Accepted" + correct_answer:false
    CHECK(r->status_msg == "Wrong Answer");
    CHECK_FALSE(r->accepted());
    CHECK(r->total_correct == 2);
    CHECK(r->total_testcases == 3);
    CHECK(r->answers[1] == "[0,0]");
    CHECK(r->expected_answers[1] == "[1,2]");
    CHECK(r->stdout_per_case
          == std::vector<std::string>{"checking 4\n", "checking 3\n", "checking 2\n"});
    CHECK(r->code_output == "checking 4\nchecking 3\nchecking 2");
}

TEST_CASE("check: compile error") {
    const auto r = parse_check(fixture("check-compile-error.json"));
    REQUIRE(r.has_value());
    CHECK(r->verdict == Verdict::CompileError);
    CHECK_FALSE(r->run_success);
    CHECK(r->compile_error.ends_with("1 error generated."));
    CHECK(r->total_correct == -1);
    CHECK(r->total_testcases == -1);
    CHECK(r->answers.empty());
    CHECK(r->stdout_per_case.empty());
}

TEST_CASE("check: submissions") {
    const auto ok = parse_check(fixture("check-submit-accepted.json"));
    REQUIRE(ok.has_value());
    CHECK(ok->is_submission);
    CHECK(ok->accepted());
    CHECK(ok->runtime == "3 ms");
    REQUIRE(ok->runtime_percentile.has_value());
    CHECK(ok->runtime_percentile.value_or(0) == doctest::Approx(85.1236));
    REQUIRE(ok->memory_percentile.has_value());
    CHECK(ok->memory_percentile.value_or(0) == doctest::Approx(40.53));
    CHECK(ok->total_correct == 63);
    CHECK(ok->submission_id == "1791090123");

    const auto wrong = parse_check(fixture("check-submit-wrong.json"));
    REQUIRE(wrong.has_value());
    CHECK(wrong->is_submission);
    CHECK(wrong->verdict == Verdict::WrongAnswer);
    CHECK(wrong->total_correct == 25);
    CHECK(wrong->total_testcases == 63);
    CHECK(wrong->last_testcase == "[3,2,4]\n6");
    CHECK(wrong->expected_output == "[1,2]");
    CHECK(wrong->code_output == "[0,0]");
    CHECK(wrong->std_output == "i=0\ni=1\n");
    CHECK(wrong->submission_id == "1791090224");
    CHECK_FALSE(wrong->runtime_percentile.has_value());

    const auto crash = parse_check(fixture("check-submit-runtime-error.json"));
    REQUIRE(crash.has_value());
    CHECK(crash->verdict == Verdict::RuntimeError);
    CHECK(crash->runtime_error.find("SUMMARY: UndefinedBehaviorSanitizer") != std::string::npos);
    CHECK(crash->last_testcase == "[3,3]\n6");
}

TEST_CASE("submission detail") {
    const auto d = parse_submission_detail(fixture("submission-detail.json"));
    REQUIRE(d.has_value());
    CHECK(d->runtime_display == "3 ms");
    CHECK(d->runtime_percentile.value_or(0) == doctest::Approx(85.1236));
    REQUIRE(d->runtime_distribution.buckets.size() == 5);
    CHECK(d->runtime_distribution.buckets[0].first == "0");
    CHECK(d->runtime_distribution.buckets[0].second == doctest::Approx(54.4));
    CHECK(d->memory_display == "14.2 MB");
    CHECK(d->memory_distribution.buckets.size() == 3);
    CHECK(d->code.starts_with("class Solution"));
    CHECK(d->lang == "C++");
    CHECK(d->timestamp == 1791090123);
    CHECK(d->verdict == Verdict::Accepted);

    const auto none = parse_submission_detail(R"({"data":{"submissionDetails":null}})");
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error().kind == Error::Kind::Api);

    const auto sparse = parse_submission_detail(R"({"data":{"submissionDetails":{
        "runtimeDistribution": null, "memoryDistribution": "[[1, \"2.5\"]]",
        "lang": "cpp", "statusCode": "11", "timestamp": "17"}}})");
    REQUIRE(sparse.has_value());
    CHECK(sparse->runtime_distribution.buckets.empty());
    REQUIRE(sparse->memory_distribution.buckets.size() == 1);
    CHECK(sparse->memory_distribution.buckets[0].first == "1");
    CHECK(sparse->memory_distribution.buckets[0].second == doctest::Approx(2.5));
    CHECK(sparse->lang == "cpp");
    CHECK(sparse->verdict == Verdict::WrongAnswer);
    CHECK(sparse->timestamp == 17);
}

TEST_CASE("submission list") {
    const auto list = parse_submission_list(fixture("submission-list.json"));
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 2);
    CHECK((*list)[0].id == "1791090123");
    CHECK((*list)[0].status == "Accepted");
    CHECK((*list)[0].accepted);
    CHECK((*list)[0].lang == "C++");
    CHECK((*list)[0].runtime == "3 ms");
    CHECK((*list)[0].memory == "14.2 MB");
    CHECK((*list)[0].timestamp == 1791090123);
    CHECK_FALSE((*list)[1].accepted);
    CHECK((*list)[1].status == "Wrong Answer");

    const auto anon = parse_submission_list(
        R"({"data":{"questionSubmissionList":{"lastKey":null,"hasNext":null,"submissions":null}}})");
    REQUIRE_FALSE(anon.has_value());
    CHECK(anon.error().kind == Error::Kind::Auth);
}

TEST_CASE("HTTP status mapping") {
    const auto limited = status_error(429, "", true);
    CHECK(limited.kind == Error::Kind::RateLimited);
    CHECK(limited.http_status == 429);

    CHECK(status_error(403, "<html>", false).kind == Error::Kind::Auth);
    CHECK(status_error(403, "<html>", false).message == "Not signed in to LeetCode");
    const auto cloudflare =
        status_error(403, "<title>Just a moment...</title> challenges.cloudflare.com", true);
    CHECK(cloudflare.kind == Error::Kind::Auth);
    CHECK(cloudflare.message.find("Cloudflare") != std::string::npos);
    CHECK(status_error(499, "<title>403 Forbidden</title> CSRF verification failed", true).kind
          == Error::Kind::Auth);

    const auto bad_query =
        status_error(400, R"({"errors":[{"message":"Cannot query field"}]})", false);
    CHECK(bad_query.kind == Error::Kind::Api);
    CHECK(bad_query.message == "Cannot query field");
    CHECK(bad_query.http_status == 400);

    const auto server = status_error(502, "<html>bad gateway</html>", false);
    CHECK(server.kind == Error::Kind::Http);
    CHECK(server.message == "LeetCode returned HTTP 502");

    CHECK(message_error("Please login first").kind == Error::Kind::Auth);
    CHECK(message_error("Too many requests").kind == Error::Kind::RateLimited);
    CHECK(message_error("Something else").kind == Error::Kind::Api);
}

TEST_CASE("problem statuses come from the problems/all endpoint") {
    const auto snapshot = lv::leetcode::parse_statuses(fixture("problems-all.json"));
    REQUIRE(snapshot.has_value());
    CHECK(snapshot->username == "dp");
    REQUIRE(snapshot->statuses.size() == 3); // the entry without a slug is skipped
    CHECK(snapshot->statuses[0]
          == std::pair<std::string, lv::leetcode::Status>{"two-sum", lv::leetcode::Status::Solved});
    CHECK(snapshot->statuses[1].second == lv::leetcode::Status::Attempted);
    CHECK(snapshot->statuses[2].second == lv::leetcode::Status::Todo);
    CHECK_FALSE(lv::leetcode::parse_statuses(R"({"error": "rate limited"})").has_value());
    CHECK_FALSE(lv::leetcode::parse_statuses("<html>").has_value());
}
