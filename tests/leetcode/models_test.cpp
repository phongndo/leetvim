#include "leetcode/models.hpp"
#include "leetcode/parse.hpp"
#include "support.hpp"

#include <doctest/doctest.h>

using namespace lv::leetcode;

namespace {

Question question_fixture(std::string_view name) {
    auto q = parse_question(lv::test::fixture(name));
    REQUIRE(q.has_value());
    return *q;
}

} // namespace

TEST_CASE("difficulty names round trip") {
    CHECK(to_string(Difficulty::Easy) == "Easy");
    CHECK(to_string(Difficulty::Medium) == "Medium");
    CHECK(to_string(Difficulty::Hard) == "Hard");
    CHECK(to_string(Difficulty::Unknown) == "?");
    CHECK(parse_difficulty("Easy") == Difficulty::Easy);
    CHECK(parse_difficulty("MEDIUM") == Difficulty::Medium);
    CHECK(parse_difficulty("hard") == Difficulty::Hard);
    CHECK(parse_difficulty("") == Difficulty::Unknown);
    CHECK(parse_difficulty("extreme") == Difficulty::Unknown);
}

TEST_CASE("status strings map to solved, attempted or todo") {
    CHECK(parse_status("ac") == Status::Solved);
    CHECK(parse_status("AC") == Status::Solved);
    CHECK(parse_status("SOLVED") == Status::Solved);
    CHECK(parse_status("notac") == Status::Attempted);
    CHECK(parse_status("TRIED") == Status::Attempted);
    CHECK(parse_status("ATTEMPTED") == Status::Attempted);
    CHECK(parse_status("") == Status::Todo);
    CHECK(parse_status("TO_DO") == Status::Todo);
}

TEST_CASE("snippet lookup by language slug") {
    const auto q = question_fixture("question-two-sum.json");
    const CodeSnippet* cpp = q.snippet("cpp");
    REQUIRE(cpp != nullptr);
    CHECK(cpp->lang == "C++");
    CHECK(cpp->code.find("twoSum") != std::string::npos);
    CHECK(q.snippet("cobol") == nullptr);
}

TEST_CASE("lines per case follows the problem kind") {
    CHECK(question_fixture("question-two-sum.json").lines_per_case() == 2);
    CHECK(question_fixture("question-lru-cache.json").lines_per_case() == 2);
    CHECK(question_fixture("question-combine-two-tables.json").lines_per_case() == 1);
    CHECK(question_fixture("question-paid-only.json").lines_per_case() == 1);

    Question shell;
    shell.meta.manual = true;
    CHECK(shell.lines_per_case() == 0);

    Question three;
    three.meta.function_name = "f";
    three.meta.param_names = {"a", "b", "c"};
    CHECK(three.lines_per_case() == 3);
}

TEST_CASE("per-difficulty counts") {
    const PerDifficulty counts{.all = 10, .easy = 5, .medium = 3, .hard = 2};
    CHECK(counts.get(Difficulty::Easy) == 5);
    CHECK(counts.get(Difficulty::Medium) == 3);
    CHECK(counts.get(Difficulty::Hard) == 2);
    CHECK(counts.get(Difficulty::Unknown) == 10);
}
