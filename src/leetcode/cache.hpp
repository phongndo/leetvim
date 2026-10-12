#pragma once

#include "leetcode/models.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace lv::leetcode {

/// On-disk cache of the problem list and question details (JSON files under `dir`).
/// Reads return nullopt on missing or corrupt files; writes are atomic.
class Cache {
public:
    explicit Cache(std::filesystem::path dir); // usually paths::cache_dir()

    struct Problems {
        std::vector<ProblemSummary> problems;
        std::chrono::system_clock::time_point fetched_at;
    };

    [[nodiscard]] std::optional<Problems> load_problems() const;
    [[nodiscard]] bool save_problems(const std::vector<ProblemSummary>& problems) const;

    [[nodiscard]] std::optional<Question> load_question(std::string_view slug) const;
    [[nodiscard]] bool save_question(const Question& q) const;

    [[nodiscard]] const std::filesystem::path& dir() const { return dir_; }

private:
    std::filesystem::path dir_;
};

} // namespace lv::leetcode
