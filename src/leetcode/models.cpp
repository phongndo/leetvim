#include "leetcode/models.hpp"

#include "util/strings.hpp"

#include <algorithm>

namespace lv::leetcode {

std::string_view to_string(Difficulty d) {
    switch (d) {
    case Difficulty::Easy: return "Easy";
    case Difficulty::Medium: return "Medium";
    case Difficulty::Hard: return "Hard";
    case Difficulty::Unknown: break;
    }
    return "?";
}

Difficulty parse_difficulty(std::string_view s) {
    if (str::iequals(s, "easy")) {
        return Difficulty::Easy;
    }
    if (str::iequals(s, "medium")) {
        return Difficulty::Medium;
    }
    if (str::iequals(s, "hard")) {
        return Difficulty::Hard;
    }
    return Difficulty::Unknown;
}

Status parse_status(std::string_view s) {
    if (str::iequals(s, "ac") || str::iequals(s, "solved")) {
        return Status::Solved;
    }
    if (str::iequals(s, "notac") || str::iequals(s, "tried") || str::iequals(s, "attempted")) {
        return Status::Attempted;
    }
    return Status::Todo;
}

const CodeSnippet* Question::snippet(std::string_view lang_slug) const {
    const auto it = std::ranges::find(snippets, lang_slug, &CodeSnippet::lang_slug);
    return it == snippets.end() ? nullptr : &*it;
}

std::size_t Question::lines_per_case() const {
    if (!meta.class_name.empty()) {
        return 2; // method names, then argument lists
    }
    if (meta.database) {
        return 1; // one JSON table description per case
    }
    return meta.param_names.size();
}

int PerDifficulty::get(Difficulty d) const {
    switch (d) {
    case Difficulty::Easy: return easy;
    case Difficulty::Medium: return medium;
    case Difficulty::Hard: return hard;
    case Difficulty::Unknown: break;
    }
    return all;
}

} // namespace lv::leetcode
