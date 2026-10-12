#include "util/fuzzy.hpp"

#include <cctype>

namespace lv::fuzzy {

namespace {

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool is_word_start(std::string_view text, std::size_t i) {
    if (i == 0) return true;
    const auto prev = static_cast<unsigned char>(text[i - 1]);
    const auto cur = static_cast<unsigned char>(text[i]);
    if (std::isalnum(prev) == 0) return true;
    return std::islower(prev) != 0 && std::isupper(cur) != 0;
}

} // namespace

std::optional<int> match(std::string_view pattern, std::string_view text,
                         std::vector<std::size_t>* positions) {
    if (positions != nullptr) positions->clear();
    if (pattern.empty()) return 0;

    // Greedy forward scan, then a backward pass to tighten the match window
    // (the classic fzf v1 trick), which keeps it O(n) yet prefers compact matches.
    std::size_t pi = 0;
    std::size_t end = 0;
    for (std::size_t ti = 0; ti < text.size() && pi < pattern.size(); ++ti) {
        if (lower(text[ti]) == lower(pattern[pi])) {
            ++pi;
            end = ti;
        }
    }
    if (pi != pattern.size()) return std::nullopt;

    std::size_t start = end;
    pi = pattern.size();
    for (std::size_t ti = end + 1; ti-- > 0 && pi > 0;) {
        if (lower(text[ti]) == lower(pattern[pi - 1])) {
            --pi;
            start = ti;
        }
    }

    int score = 0;
    int run = 0;
    pi = 0;
    std::size_t last = std::string_view::npos;
    for (std::size_t ti = start; ti <= end && pi < pattern.size(); ++ti) {
        if (lower(text[ti]) != lower(pattern[pi])) continue;
        score += 16;
        if (last != std::string_view::npos && ti == last + 1) {
            ++run;
            score += 8 * run;
        } else {
            run = 0;
        }
        if (is_word_start(text, ti)) score += 12;
        if (text[ti] == pattern[pi]) score += 1;
        if (positions != nullptr) positions->push_back(ti);
        last = ti;
        ++pi;
    }
    score -= static_cast<int>(end - start + 1 - pattern.size()); // gaps
    if (start == 0) score += 10;
    return score;
}

} // namespace lv::fuzzy
