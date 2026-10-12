#include "leetcode/cache.hpp"
#include "leetcode/parse.hpp"
#include "support.hpp"
#include "util/paths.hpp"

#include <doctest/doctest.h>

#include <chrono>

using namespace lv::leetcode;
using lv::test::fixture;
using lv::test::TempDir;

TEST_CASE("problem list cache round trip") {
    const TempDir tmp;
    const Cache cache(tmp.path());
    CHECK_FALSE(cache.load_problems().has_value());

    auto page = parse_problem_page(fixture("problem-list.json"));
    REQUIRE(page.has_value());
    auto problems = page->problems;
    problems[0].status = Status::Solved;
    problems[1].status = Status::Attempted;
    problems[2].favorite = true;
    problems[3].paid_only = true;
    problems[4].tags.clear();
    REQUIRE(cache.save_problems(problems));

    const auto loaded = cache.load_problems();
    REQUIRE(loaded.has_value());
    const auto [cached, fetched_at] = loaded.value_or(Cache::Problems{});
    CHECK(cached == problems);
    const auto age = std::chrono::system_clock::now() - fetched_at;
    CHECK(age >= std::chrono::seconds(-1));
    CHECK(age < std::chrono::minutes(1));
}

TEST_CASE("question cache round trip") {
    const TempDir tmp;
    const Cache cache(tmp.path());
    for (const auto* name : {
             "question-two-sum.json",
             "question-lru-cache.json",
             "question-combine-two-tables.json",
             "question-paid-only.json",
         }) {
        CAPTURE(name);
        auto q = parse_question(fixture(name));
        REQUIRE(q.has_value());
        q->status = Status::Attempted;
        REQUIRE(cache.save_question(*q));
        CHECK(cache.load_question(q->slug) == *q);
    }
    CHECK_FALSE(cache.load_question("not-cached").has_value());
}

TEST_CASE("corrupt or foreign cache files are ignored") {
    const TempDir tmp;
    const Cache cache(tmp.path());
    const auto problems_file = tmp.path() / "problems.json";
    for (const auto* contents : {
             "",
             "{",
             "null",
             "[]",
             R"({"version": 999, "fetched_at": 1, "problems": []})",
             R"({"fetched_at": 1, "problems": []})",
             R"({"version": 1, "problems": {}})",
             R"({"version": 1, "fetched_at": 1, "problems": [{"title": "no slug"}]})",
         }) {
        CAPTURE(contents);
        REQUIRE(lv::paths::write_file(problems_file, contents));
        CHECK_FALSE(cache.load_problems().has_value());
    }

    auto q = parse_question(fixture("question-two-sum.json"));
    REQUIRE(q.has_value());
    REQUIRE(cache.save_question(*q));
    const auto question_file = tmp.path() / "questions" / "two-sum.json";
    std::string text;
    REQUIRE(lv::paths::read_file(question_file, text));
    REQUIRE(lv::paths::write_file(question_file, text.substr(0, text.size() / 2)));
    CHECK_FALSE(cache.load_question("two-sum").has_value());

    // A file whose slug does not match its name is not trusted either.
    REQUIRE(lv::paths::write_file(tmp.path() / "questions" / "three-sum.json", text));
    CHECK_FALSE(cache.load_question("three-sum").has_value());
}

TEST_CASE("slugs that are not safe file names are rejected") {
    const TempDir tmp;
    const Cache cache(tmp.path());
    Question q;
    q.slug = "../escape";
    CHECK_FALSE(cache.save_question(q));
    CHECK_FALSE(cache.load_question("../escape").has_value());
    CHECK_FALSE(cache.load_question("").has_value());
    CHECK_FALSE(cache.load_question("a/b").has_value());
}
