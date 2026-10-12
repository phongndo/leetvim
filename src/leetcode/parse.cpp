#include "leetcode/parse.hpp"

#include "leetcode/json.hpp"
#include "leetcode/response.hpp"
#include "util/strings.hpp"

#include <charconv>
#include <exception>
#include <format>
#include <utility>

namespace lv::leetcode {

namespace {

using json::Json;

std::unexpected<Error> failure(Error::Kind kind, std::string message) {
    return std::unexpected(Error{.kind = kind, .message = std::move(message)});
}

std::unexpected<Error> shape_error(std::string message) {
    return failure(Error::Kind::Parse, std::move(message));
}

/// Runs a parser, turning any stray exception into a Parse error.
template <typename T, typename F>
Result<T> guarded(const F& parse) {
    try {
        return parse();
    } catch (const std::exception& e) {
        return shape_error(std::format("Unexpected response from LeetCode: {}", e.what()));
    }
}

Result<Json> parse_object(std::string_view body) {
    Json doc = json::parse(body);
    if (doc.is_discarded() || !doc.is_object()) {
        return shape_error("LeetCode returned a malformed response");
    }
    return doc;
}

std::optional<Error> graphql_error(const Json& doc) {
    const Json* errors = json::find(doc, "errors");
    if (errors == nullptr || !errors->is_array() || errors->empty()) {
        return std::nullopt;
    }
    std::string message = json::get_string(errors->front(), "message");
    return message_error(message.empty() ? "LeetCode returned an error" : std::move(message));
}

/// REST endpoints report failures as {"error": "..."}.
std::optional<Error> rest_error(const Json& doc) {
    std::string message = json::get_string(doc, "error");
    if (message.empty()) {
        return std::nullopt;
    }
    return message_error(std::move(message));
}

/// The GraphQL `data` object, provided data.<field> is an object. Otherwise the GraphQL
/// error if there is one, else `missing`.
Result<Json> graphql_data(std::string_view body, const char* field, const Error& missing) {
    auto doc = parse_object(body);
    if (!doc) {
        return std::unexpected(doc.error());
    }
    const Json* data = json::find(*doc, "data");
    const Json* value = data != nullptr ? json::find(*data, field) : nullptr;
    if (value != nullptr && value->is_object()) {
        return std::move((*doc)["data"]);
    }
    if (auto error = graphql_error(*doc)) {
        return std::unexpected(std::move(*error));
    }
    if (data == nullptr) {
        return shape_error("LeetCode response has no data");
    }
    if (value == nullptr) {
        return std::unexpected(missing);
    }
    return shape_error(std::format("Unexpected shape for {}", field));
}

Result<Json> graphql_field(std::string_view body, const char* field, const Error& missing) {
    auto data = graphql_data(body, field, missing);
    if (!data) {
        return std::unexpected(data.error());
    }
    return std::move((*data)[field]);
}

const Json& array_or_empty(const Json& j, const char* key) {
    static const Json empty = Json::array();
    const Json* v = json::find(j, key);
    return v != nullptr && v->is_array() ? *v : empty;
}

template <typename T>
std::optional<T> whole_number(std::string_view s) {
    T n = 0;
    const char* begin = s.data();
    const char* end = begin + s.size();
    const auto [ptr, ec] = std::from_chars(begin, end, n);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return n;
}

Verdict to_verdict(std::int64_t code) {
    switch (code) {
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 20:
    case 30: return static_cast<Verdict>(code);
    default: return Verdict::Unknown;
    }
}

std::vector<Tag> parse_tags(const Json& j) {
    std::vector<Tag> tags;
    for (const auto& t : array_or_empty(j, "topicTags")) {
        tags.push_back({.name = json::get_string(t, "name"), .slug = json::get_string(t, "slug")});
    }
    return tags;
}

ProblemSummary parse_summary(const Json& q) {
    ProblemSummary p;
    p.frontend_id = json::get_string(q, "frontendQuestionId");
    if (p.frontend_id.empty()) {
        p.frontend_id = json::get_string(q, "questionFrontendId");
    }
    p.number = whole_number<int>(p.frontend_id).value_or(0);
    p.title = json::get_string(q, "title");
    p.slug = json::get_string(q, "titleSlug");
    p.difficulty = parse_difficulty(json::get_string(q, "difficulty"));
    p.ac_rate = json::get_double(q, "acRate").value_or(0.0);
    p.paid_only = json::get_bool(q, "paidOnly") || json::get_bool(q, "isPaidOnly");
    p.favorite = json::get_bool(q, "isFavor");
    p.has_solution = json::get_bool(q, "hasSolution");
    p.has_video = json::get_bool(q, "hasVideoSolution");
    p.status = parse_status(json::get_string(q, "status"));
    p.tags = parse_tags(q);
    return p;
}

QuestionMeta parse_meta(const Json& m) {
    QuestionMeta meta;
    if (!m.is_object()) {
        return meta;
    }
    meta.database = json::get_bool(m, "database");
    meta.manual = json::get_bool(m, "manual");
    meta.class_name = json::get_string(m, "classname");
    // Design problems also carry synthetic "params"; only function problems have real ones.
    if (meta.class_name.empty()) {
        meta.function_name = json::get_string(m, "name");
        for (const auto& p : array_or_empty(m, "params")) {
            meta.param_names.push_back(json::get_string(p, "name"));
        }
    }
    return meta;
}

Question parse_question_object(const Json& j) {
    Question q;
    q.question_id = json::get_string(j, "questionId");
    q.frontend_id = json::get_string(j, "questionFrontendId");
    q.title = json::get_string(j, "title");
    q.slug = json::get_string(j, "titleSlug");
    q.content_html = json::get_string(j, "content");
    q.difficulty = parse_difficulty(json::get_string(j, "difficulty"));
    q.paid_only = json::get_bool(j, "isPaidOnly");
    q.likes = json::get_int(j, "likes");
    q.dislikes = json::get_int(j, "dislikes");
    q.category = json::get_string(j, "categoryTitle");
    q.tags = parse_tags(j);
    for (const auto& s : array_or_empty(j, "codeSnippets")) {
        q.snippets.push_back({
            .lang = json::get_string(s, "lang"),
            .lang_slug = json::get_string(s, "langSlug"),
            .code = json::get_string(s, "code"),
        });
    }
    q.hints = json::get_strings(j, "hints");
    const Json similar = json::get_nested(j, "similarQuestions");
    if (similar.is_array()) {
        for (const auto& s : similar) {
            q.similar.push_back({
                .title = json::get_string(s, "title"),
                .slug = json::get_string(s, "titleSlug"),
                .difficulty = parse_difficulty(json::get_string(s, "difficulty")),
            });
        }
    }
    q.example_testcases = json::get_strings(j, "exampleTestcaseList");
    q.sample_testcase = json::get_string(j, "sampleTestCase");
    if (q.example_testcases.empty() && !q.sample_testcase.empty()) {
        q.example_testcases.push_back(q.sample_testcase);
    }
    q.meta = parse_meta(json::get_nested(j, "metaData"));
    const Json stats = json::get_nested(j, "stats");
    q.total_accepted = json::get_i64(stats, "totalAcceptedRaw");
    q.total_submissions = json::get_i64(stats, "totalSubmissionRaw");
    q.ac_rate = json::get_string(stats, "acRate");
    q.status = parse_status(json::get_string(j, "status"));
    return q;
}

void set_count(PerDifficulty& counts, std::string_view difficulty, int value) {
    if (str::iequals(difficulty, "all")) {
        counts.all = value;
        return;
    }
    switch (parse_difficulty(difficulty)) {
    case Difficulty::Easy: counts.easy = value; break;
    case Difficulty::Medium: counts.medium = value; break;
    case Difficulty::Hard: counts.hard = value; break;
    case Difficulty::Unknown: break;
    }
}

void parse_user_profile(const Json& user, UserStats& stats) {
    stats.username = json::get_string(user, "username");
    if (const Json* profile = json::find(user, "profile")) {
        stats.real_name = json::get_string(*profile, "realName");
        stats.ranking = json::get_int(*profile, "ranking");
        stats.reputation = json::get_int(*profile, "reputation");
    }
    if (const Json* submit = json::find(user, "submitStats")) {
        for (const auto& row : array_or_empty(*submit, "acSubmissionNum")) {
            const auto difficulty = json::get_string(row, "difficulty");
            set_count(stats.solved, difficulty, json::get_int(row, "count"));
            set_count(stats.ac_submissions, difficulty, json::get_int(row, "submissions"));
        }
        for (const auto& row : array_or_empty(*submit, "totalSubmissionNum")) {
            set_count(stats.total_submissions, json::get_string(row, "difficulty"),
                      json::get_int(row, "submissions"));
        }
    }
    if (const Json* calendar = json::find(user, "userCalendar")) {
        stats.streak = json::get_int(*calendar, "streak");
        stats.total_active_days = json::get_int(*calendar, "totalActiveDays");
        const Json days = json::get_nested(*calendar, "submissionCalendar");
        if (days.is_object()) {
            for (const auto& [day, count] : days.items()) {
                const auto ts = whole_number<std::int64_t>(day);
                const auto n = json::to_int(count);
                if (ts && n) {
                    stats.calendar[*ts] = static_cast<int>(*n);
                }
            }
        }
    }
    for (const auto& lang : array_or_empty(user, "languageProblemCount")) {
        stats.languages.emplace_back(json::get_string(lang, "languageName"),
                                     json::get_int(lang, "problemsSolved"));
    }
    if (const Json* tags = json::find(user, "tagProblemCounts")) {
        for (const char* level : {"fundamental", "intermediate", "advanced"}) {
            for (const auto& tag : array_or_empty(*tags, level)) {
                stats.skills.push_back({
                    .name = json::get_string(tag, "tagName"),
                    .slug = json::get_string(tag, "tagSlug"),
                    .level = level,
                    .solved = json::get_int(tag, "problemsSolved"),
                });
            }
        }
    }
    for (const auto& badge : array_or_empty(user, "badges")) {
        stats.badges.push_back(json::get_string(badge, "name"));
    }
}

/// `std_output_list` and friends carry a trailing "" (and sometimes more) beyond the cases.
void trim_to_cases(std::vector<std::string>& values, std::size_t cases) {
    while (values.size() > cases && values.back().empty()) {
        values.pop_back();
    }
}

/// Strings pass through; arrays of lines are joined.
std::string text_or_lines(const Json& j, const char* key) {
    const Json* v = json::find(j, key);
    if (v == nullptr) {
        return {};
    }
    if (!v->is_array()) {
        return json::to_text(*v);
    }
    const auto lines = json::get_strings(j, key);
    return str::join(lines, "\n");
}

std::string first_non_empty(std::string a, std::string b) {
    return a.empty() ? std::move(b) : std::move(a);
}

Distribution parse_distribution(const Json& j) {
    Distribution d;
    const Json& rows = j.is_object() ? array_or_empty(j, "distribution") : j;
    if (!rows.is_array()) {
        return d;
    }
    for (const auto& row : rows) {
        if (!row.is_array() || row.size() < 2) {
            continue;
        }
        d.buckets.emplace_back(json::to_text(row[0]), json::to_double(row[1]).value_or(0.0));
    }
    return d;
}

} // namespace

Result<ProblemPage> parse_problem_page(std::string_view body) {
    return guarded<ProblemPage>([&] -> Result<ProblemPage> {
        const Error missing{.kind = Error::Kind::Api, .message = "Problem list unavailable"};
        auto list = graphql_field(body, "problemsetQuestionList", missing);
        if (!list) {
            return std::unexpected(list.error());
        }
        const Json* questions = json::find(*list, "questions");
        if (questions == nullptr || !questions->is_array()) {
            return shape_error("Problem list has no questions");
        }
        ProblemPage page;
        page.total = json::get_int(*list, "total");
        page.problems.reserve(questions->size());
        for (const auto& q : *questions) {
            auto summary = parse_summary(q);
            if (!summary.slug.empty()) {
                page.problems.push_back(std::move(summary));
            }
        }
        return page;
    });
}

Result<Question> parse_question(std::string_view body) {
    return guarded<Question>([&] -> Result<Question> {
        const Error missing{.kind = Error::Kind::Api, .message = "Problem not found"};
        auto q = graphql_field(body, "question", missing);
        if (!q) {
            return std::unexpected(q.error());
        }
        auto question = parse_question_object(*q);
        if (question.slug.empty()) {
            return shape_error("Question has no slug");
        }
        return question;
    });
}

Result<DailyChallenge> parse_daily(std::string_view body) {
    return guarded<DailyChallenge>([&] -> Result<DailyChallenge> {
        const Error missing{.kind = Error::Kind::Api, .message = "No daily challenge available"};
        auto d = graphql_field(body, "activeDailyCodingChallengeQuestion", missing);
        if (!d) {
            return std::unexpected(d.error());
        }
        const Json* question = json::find(*d, "question");
        if (question == nullptr) {
            return shape_error("Daily challenge has no question");
        }
        DailyChallenge daily;
        daily.date = json::get_string(*d, "date");
        daily.completed = str::iequals(json::get_string(*d, "userStatus"), "Finish");
        daily.problem = parse_summary(*question);
        if (daily.problem.slug.empty()) {
            return shape_error("Daily challenge has no slug");
        }
        return daily;
    });
}

Result<UserStatus> parse_user_status(std::string_view body) {
    return guarded<UserStatus>([&] -> Result<UserStatus> {
        const Error missing{.kind = Error::Kind::Api, .message = "User status unavailable"};
        auto s = graphql_field(body, "userStatus", missing);
        if (!s) {
            return std::unexpected(s.error());
        }
        return UserStatus{
            .signed_in = json::get_bool(*s, "isSignedIn"),
            .username = json::get_string(*s, "username"),
            .premium = json::get_bool(*s, "isPremium"),
            .avatar = json::get_string(*s, "avatar"),
        };
    });
}

Result<UserStats> parse_user_stats(std::string_view body) {
    return guarded<UserStats>([&] -> Result<UserStats> {
        const Error missing{.kind = Error::Kind::Api, .message = "That user does not exist"};
        auto data = graphql_data(body, "matchedUser", missing);
        if (!data) {
            return std::unexpected(data.error());
        }
        UserStats stats;
        for (const auto& row : array_or_empty(*data, "allQuestionsCount")) {
            set_count(stats.questions, json::get_string(row, "difficulty"),
                      json::get_int(row, "count"));
        }
        parse_user_profile((*data)["matchedUser"], stats);
        if (const Json* contest = json::find(*data, "userContestRanking")) {
            stats.contest = ContestRanking{
                .attended = json::get_int(*contest, "attendedContestsCount"),
                .rating = json::get_double(*contest, "rating").value_or(0.0),
                .global_ranking = json::get_int(*contest, "globalRanking"),
                .total_participants = json::get_int(*contest, "totalParticipants"),
                .top_percentage = json::get_double(*contest, "topPercentage").value_or(0.0),
            };
        }
        for (const auto& s : array_or_empty(*data, "recentAcSubmissionList")) {
            stats.recent_accepted.push_back({
                .id = json::get_string(s, "id"),
                .title = json::get_string(s, "title"),
                .slug = json::get_string(s, "titleSlug"),
                .lang = json::get_string(s, "lang"),
                .timestamp = json::get_i64(s, "timestamp"),
            });
        }
        return stats;
    });
}

Result<StreakInfo> parse_streak(std::string_view body) {
    return guarded<StreakInfo>([&] -> Result<StreakInfo> {
        const Error missing{.kind = Error::Kind::Auth, .message = "Not signed in to LeetCode"};
        auto s = graphql_field(body, "streakCounter", missing);
        if (!s) {
            return std::unexpected(s.error());
        }
        return StreakInfo{
            .count = json::get_int(*s, "streakCount"),
            .today_completed = json::get_bool(*s, "currentDayCompleted"),
        };
    });
}

Result<std::string> parse_run_id(std::string_view body) {
    return guarded<std::string>([&] -> Result<std::string> {
        auto doc = parse_object(body);
        if (!doc) {
            return std::unexpected(doc.error());
        }
        for (const char* key : {"interpret_id", "submission_id"}) {
            if (auto id = json::get_string(*doc, key); !id.empty()) {
                return id;
            }
        }
        if (auto error = rest_error(*doc)) {
            return std::unexpected(std::move(*error));
        }
        return shape_error("LeetCode did not return a run or submission id");
    });
}

Result<RunResult> parse_check(std::string_view body) {
    return guarded<RunResult>([&] -> Result<RunResult> {
        auto parsed = parse_object(body);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        const Json& doc = *parsed;
        RunResult r;
        r.state = json::get_string(doc, "state");
        if (r.state.empty()) {
            if (auto error = rest_error(doc)) {
                return std::unexpected(std::move(*error));
            }
            return shape_error("Check response has no state");
        }
        r.submission_id = json::get_string(doc, "submission_id");
        r.finished = r.state == "SUCCESS";
        if (!r.finished) {
            return r;
        }

        const auto task = json::get_string(doc, "task_name");
        r.is_submission = task.empty() ? whole_number<std::int64_t>(r.submission_id).has_value()
                                       : task.contains("judgetask");
        r.verdict = to_verdict(json::get_i64(doc, "status_code"));
        r.status_msg = json::get_string(doc, "status_msg");
        r.run_success = json::get_bool(doc, "run_success");
        r.lang = json::get_string(doc, "lang");
        r.runtime = json::get_string(doc, "status_runtime");
        r.memory = json::get_string(doc, "status_memory");
        r.runtime_percentile = json::get_double(doc, "runtime_percentile");
        r.memory_percentile = json::get_double(doc, "memory_percentile");
        r.total_correct = json::get_int(doc, "total_correct", -1);
        r.total_testcases = json::get_int(doc, "total_testcases", -1);
        r.compare_result = json::get_string(doc, "compare_result");
        r.answers = json::get_strings(doc, "code_answer");
        r.expected_answers = json::get_strings(doc, "expected_code_answer");
        r.stdout_per_case = json::get_strings(doc, "std_output_list");
        r.correct_answer = json::get_bool(doc, "correct_answer");
        r.compile_error = first_non_empty(json::get_string(doc, "full_compile_error"),
                                          json::get_string(doc, "compile_error"));
        r.runtime_error = first_non_empty(json::get_string(doc, "full_runtime_error"),
                                          json::get_string(doc, "runtime_error"));
        r.last_testcase = text_or_lines(doc, "last_testcase");
        r.expected_output = text_or_lines(doc, "expected_output");
        r.code_output = text_or_lines(doc, "code_output");
        r.std_output = text_or_lines(doc, "std_output");

        const std::size_t cases = r.total_testcases > 0
                                      ? static_cast<std::size_t>(r.total_testcases)
                                      : r.compare_result.size();
        trim_to_cases(r.answers, cases);
        trim_to_cases(r.expected_answers, cases);
        trim_to_cases(r.stdout_per_case, cases);

        // A run that executes cleanly reports "Accepted" even when its output differs from
        // the expected output; the website shows that as a wrong answer.
        if (!r.is_submission && r.verdict == Verdict::Accepted && json::find(doc, "correct_answer")
            && !r.correct_answer) {
            r.verdict = Verdict::WrongAnswer;
            r.status_msg = "Wrong Answer";
        }
        return r;
    });
}

Result<SubmissionDetail> parse_submission_detail(std::string_view body) {
    return guarded<SubmissionDetail>([&] -> Result<SubmissionDetail> {
        const Error missing{
            .kind = Error::Kind::Api,
            .message = "Submission not found (or not visible to this account)",
        };
        auto s = graphql_field(body, "submissionDetails", missing);
        if (!s) {
            return std::unexpected(s.error());
        }
        SubmissionDetail d;
        d.runtime_display = json::get_string(*s, "runtimeDisplay");
        d.runtime_percentile = json::get_double(*s, "runtimePercentile");
        d.runtime_distribution = parse_distribution(json::get_nested(*s, "runtimeDistribution"));
        d.memory_display = json::get_string(*s, "memoryDisplay");
        d.memory_percentile = json::get_double(*s, "memoryPercentile");
        d.memory_distribution = parse_distribution(json::get_nested(*s, "memoryDistribution"));
        d.code = json::get_string(*s, "code");
        d.timestamp = json::get_i64(*s, "timestamp");
        d.verdict = to_verdict(json::get_i64(*s, "statusCode"));
        if (const Json* lang = json::find(*s, "lang"); lang != nullptr && lang->is_object()) {
            d.lang = first_non_empty(json::get_string(*lang, "verboseName"),
                                     json::get_string(*lang, "name"));
        } else {
            d.lang = json::get_string(*s, "lang");
        }
        return d;
    });
}

Result<std::vector<SubmissionSummary>> parse_submission_list(std::string_view body) {
    using List = std::vector<SubmissionSummary>;
    return guarded<List>([&] -> Result<List> {
        const Error missing{.kind = Error::Kind::Api, .message = "Submission list unavailable"};
        auto list = graphql_field(body, "questionSubmissionList", missing);
        if (!list) {
            return std::unexpected(list.error());
        }
        const Json* submissions = json::find(*list, "submissions");
        if (submissions == nullptr) {
            return failure(Error::Kind::Auth, "Sign in to LeetCode to see your submissions");
        }
        if (!submissions->is_array()) {
            return shape_error("Submission list is not an array");
        }
        List out;
        for (const auto& s : *submissions) {
            SubmissionSummary summary;
            summary.id = json::get_string(s, "id");
            summary.status = json::get_string(s, "statusDisplay");
            summary.lang =
                first_non_empty(json::get_string(s, "langName"), json::get_string(s, "lang"));
            summary.runtime = json::get_string(s, "runtime");
            summary.memory = json::get_string(s, "memory");
            summary.timestamp = json::get_i64(s, "timestamp");
            summary.accepted =
                json::get_i64(s, "status") == 10 || str::iequals(summary.status, "Accepted");
            out.push_back(std::move(summary));
        }
        return out;
    });
}

Result<StatusSnapshot> parse_statuses(std::string_view body) {
    return guarded<StatusSnapshot>([&] -> Result<StatusSnapshot> {
        auto doc = parse_object(body);
        if (!doc) return std::unexpected(doc.error());
        if (auto error = rest_error(*doc)) return std::unexpected(*error);
        const Json* pairs = json::find(*doc, "stat_status_pairs");
        if (pairs == nullptr || !pairs->is_array()) {
            return shape_error("Problem statuses are missing from the response");
        }
        StatusSnapshot out;
        out.username = json::get_string(*doc, "user_name");
        out.statuses.reserve(pairs->size());
        for (const auto& pair : *pairs) {
            const Json* stat = json::find(pair, "stat");
            if (stat == nullptr) continue;
            std::string slug = json::get_string(*stat, "question__title_slug");
            if (slug.empty()) continue;
            out.statuses.emplace_back(std::move(slug),
                                      parse_status(json::get_string(pair, "status")));
        }
        return out;
    });
}

} // namespace lv::leetcode
