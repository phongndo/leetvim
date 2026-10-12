#include "leetcode/cache.hpp"

#include "leetcode/json.hpp"
#include "util/log.hpp"
#include "util/paths.hpp"

#include <algorithm>
#include <exception>
#include <string>
#include <utility>

namespace lv::leetcode {

namespace {

using json::Json;

// Bump when the on-disk format changes; files with another version are ignored.
constexpr int schema_version = 1;

std::string_view status_name(Status s) {
    switch (s) {
    case Status::Solved: return "solved";
    case Status::Attempted: return "attempted";
    case Status::Todo: break;
    }
    return "todo";
}

Json tags_json(const std::vector<Tag>& tags) {
    Json out = Json::array();
    for (const auto& t : tags) {
        out.push_back({t.name, t.slug});
    }
    return out;
}

std::vector<Tag> tags_from(const Json& j) {
    std::vector<Tag> tags;
    const Json* v = json::find(j, "tags");
    if (v == nullptr || !v->is_array()) {
        return tags;
    }
    for (const auto& t : *v) {
        if (t.is_array() && t.size() == 2) {
            tags.push_back({.name = json::to_text(t[0]), .slug = json::to_text(t[1])});
        }
    }
    return tags;
}

Json problem_json(const ProblemSummary& p) {
    return {
        {"id", p.frontend_id},
        {"number", p.number},
        {"title", p.title},
        {"slug", p.slug},
        {"difficulty", to_string(p.difficulty)},
        {"ac_rate", p.ac_rate},
        {"paid_only", p.paid_only},
        {"favorite", p.favorite},
        {"has_solution", p.has_solution},
        {"has_video", p.has_video},
        {"status", status_name(p.status)},
        {"tags", tags_json(p.tags)},
    };
}

ProblemSummary problem_from(const Json& j) {
    return {
        .frontend_id = json::get_string(j, "id"),
        .number = json::get_int(j, "number"),
        .title = json::get_string(j, "title"),
        .slug = json::get_string(j, "slug"),
        .difficulty = parse_difficulty(json::get_string(j, "difficulty")),
        .ac_rate = json::get_double(j, "ac_rate").value_or(0.0),
        .paid_only = json::get_bool(j, "paid_only"),
        .favorite = json::get_bool(j, "favorite"),
        .has_solution = json::get_bool(j, "has_solution"),
        .has_video = json::get_bool(j, "has_video"),
        .status = parse_status(json::get_string(j, "status")),
        .tags = tags_from(j),
    };
}

Json question_json(const Question& q) {
    Json snippets = Json::array();
    for (const auto& s : q.snippets) {
        snippets.push_back({s.lang, s.lang_slug, s.code});
    }
    Json similar = Json::array();
    for (const auto& s : q.similar) {
        similar.push_back({s.title, s.slug, to_string(s.difficulty)});
    }
    const auto& m = q.meta;
    return {
        {"version", schema_version},
        {"question_id", q.question_id},
        {"frontend_id", q.frontend_id},
        {"title", q.title},
        {"slug", q.slug},
        {"content_html", q.content_html},
        {"difficulty", to_string(q.difficulty)},
        {"paid_only", q.paid_only},
        {"likes", q.likes},
        {"dislikes", q.dislikes},
        {"category", q.category},
        {"tags", tags_json(q.tags)},
        {"snippets", std::move(snippets)},
        {"hints", q.hints},
        {"similar", std::move(similar)},
        {"example_testcases", q.example_testcases},
        {"sample_testcase", q.sample_testcase},
        {
            "meta",
            {
                {"function_name", m.function_name},
                {"param_names", m.param_names},
                {"class_name", m.class_name},
                {"database", m.database},
                {"manual", m.manual},
            },
        },
        {"total_accepted", q.total_accepted},
        {"total_submissions", q.total_submissions},
        {"ac_rate", q.ac_rate},
        {"status", status_name(q.status)},
    };
}

/// Rows stored as JSON arrays of strings, e.g. [lang, slug, code].
std::vector<std::vector<std::string>> rows(const Json& j, const char* key, std::size_t width) {
    std::vector<std::vector<std::string>> out;
    const Json* v = json::find(j, key);
    if (v == nullptr || !v->is_array()) {
        return out;
    }
    for (const auto& row : *v) {
        if (!row.is_array() || row.size() != width) {
            continue;
        }
        auto& fields = out.emplace_back();
        for (const auto& field : row) {
            fields.push_back(json::to_text(field));
        }
    }
    return out;
}

Question question_from(const Json& j) {
    Question q;
    q.question_id = json::get_string(j, "question_id");
    q.frontend_id = json::get_string(j, "frontend_id");
    q.title = json::get_string(j, "title");
    q.slug = json::get_string(j, "slug");
    q.content_html = json::get_string(j, "content_html");
    q.difficulty = parse_difficulty(json::get_string(j, "difficulty"));
    q.paid_only = json::get_bool(j, "paid_only");
    q.likes = json::get_int(j, "likes");
    q.dislikes = json::get_int(j, "dislikes");
    q.category = json::get_string(j, "category");
    q.tags = tags_from(j);
    for (auto& r : rows(j, "snippets", 3)) {
        q.snippets.push_back(
            {.lang = std::move(r[0]), .lang_slug = std::move(r[1]), .code = std::move(r[2])});
    }
    q.hints = json::get_strings(j, "hints");
    for (auto& r : rows(j, "similar", 3)) {
        q.similar.push_back({
            .title = std::move(r[0]),
            .slug = std::move(r[1]),
            .difficulty = parse_difficulty(r[2]),
        });
    }
    q.example_testcases = json::get_strings(j, "example_testcases");
    q.sample_testcase = json::get_string(j, "sample_testcase");
    if (const Json* m = json::find(j, "meta")) {
        q.meta = {
            .function_name = json::get_string(*m, "function_name"),
            .param_names = json::get_strings(*m, "param_names"),
            .class_name = json::get_string(*m, "class_name"),
            .database = json::get_bool(*m, "database"),
            .manual = json::get_bool(*m, "manual"),
        };
    }
    q.total_accepted = json::get_i64(j, "total_accepted");
    q.total_submissions = json::get_i64(j, "total_submissions");
    q.ac_rate = json::get_string(j, "ac_rate");
    q.status = parse_status(json::get_string(j, "status"));
    return q;
}

/// Slugs become file names, so only accept the characters LeetCode uses in them.
bool valid_slug(std::string_view slug) {
    return !slug.empty() && slug.size() <= 200 && std::ranges::all_of(slug, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

/// Reads and parses a cache file of the current schema version; null otherwise.
Json read_versioned(const std::filesystem::path& path) {
    std::string text;
    if (!paths::read_file(path, text)) {
        return nullptr;
    }
    Json doc = json::parse(text);
    if (doc.is_discarded() || !doc.is_object() || json::get_int(doc, "version") != schema_version) {
        log::debug("cache: ignoring {}", path.string());
        return nullptr;
    }
    return doc;
}

bool write_json(const std::filesystem::path& path, const Json& doc) {
    return paths::write_file(path, doc.dump(-1, ' ', false, Json::error_handler_t::replace));
}

} // namespace

Cache::Cache(std::filesystem::path dir) : dir_(std::move(dir)) {}

std::optional<Cache::Problems> Cache::load_problems() const {
    try {
        const Json doc = read_versioned(dir_ / "problems.json");
        const Json* list = json::find(doc, "problems");
        if (list == nullptr || !list->is_array()) {
            return std::nullopt;
        }
        Problems out;
        out.fetched_at = std::chrono::system_clock::time_point(
            std::chrono::seconds(json::get_i64(doc, "fetched_at")));
        out.problems.reserve(list->size());
        for (const auto& p : *list) {
            auto problem = problem_from(p);
            if (problem.slug.empty()) {
                return std::nullopt;
            }
            out.problems.push_back(std::move(problem));
        }
        return out;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool Cache::save_problems(const std::vector<ProblemSummary>& problems) const {
    try {
        Json list = Json::array();
        for (const auto& p : problems) {
            list.push_back(problem_json(p));
        }
        const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        const Json doc = {
            {"version", schema_version},
            {"fetched_at", now.time_since_epoch().count()},
            {"problems", std::move(list)},
        };
        return write_json(dir_ / "problems.json", doc);
    } catch (const std::exception&) {
        return false;
    }
}

std::optional<Question> Cache::load_question(std::string_view slug) const {
    if (!valid_slug(slug)) {
        return std::nullopt;
    }
    try {
        const Json doc = read_versioned(dir_ / "questions" / (std::string(slug) + ".json"));
        if (doc.is_null()) {
            return std::nullopt;
        }
        auto q = question_from(doc);
        if (q.slug != slug) {
            return std::nullopt;
        }
        return q;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool Cache::save_question(const Question& q) const {
    if (!valid_slug(q.slug)) {
        return false;
    }
    try {
        return write_json(dir_ / "questions" / (q.slug + ".json"), question_json(q));
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace lv::leetcode
