#pragma once

#include "core/languages.hpp"
#include "leetcode/models.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lv::core {

/// Identity recorded in a solution file, so `leetvim test <file>` knows what it is.
struct SolutionMeta {
    std::string frontend_id;
    std::string slug;
    std::string lang;
};

/// Builds a new solution file: a header comment, optional injected text, and the
/// starter snippet between `@lc code=start` / `@lc code=end` markers.
std::string render_solution(const leetcode::Question& q, const Language& lang, std::string_view url,
                            std::string_view inject_before = {},
                            std::string_view inject_after = {});

/// Code that gets submitted: the text between the markers (also accepts leetcode.nvim's
/// `@leet start`/`@leet end`), or the whole file when there are no markers.
std::string extract_code(std::string_view content);

/// Reads the `@leetvim id=… slug=… lang=…` header.
std::optional<SolutionMeta> parse_header(std::string_view content);
/// Infers identity from a name like "1.two-sum.cpp".
std::optional<SolutionMeta> parse_filename(const std::filesystem::path& path);

/// Groups raw testcase lines into cases of `lines_per_case` lines (0: one case).
std::vector<std::string> group_testcases(std::string_view input, std::size_t lines_per_case);

/// Solution files and custom testcases on disk.
class Workspace {
public:
    explicit Workspace(std::filesystem::path root) : root_(std::move(root)) {}

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }
    [[nodiscard]] std::filesystem::path solution_path(const leetcode::Question& q,
                                                      const Language& lang) const;
    [[nodiscard]] std::filesystem::path testcases_path(std::string_view slug) const;

    /// Creates the solution file if it does not exist yet.
    [[nodiscard]] std::expected<std::filesystem::path, std::string>
    ensure_solution(const leetcode::Question& q, const Language& lang, std::string_view url,
                    std::string_view inject_before = {}, std::string_view inject_after = {}) const;

    /// Custom testcases if saved, otherwise the question's examples; one entry per case.
    [[nodiscard]] std::vector<std::string> testcases(const leetcode::Question& q) const;
    /// Writes the testcase file (seeding it from the examples) and returns its path.
    [[nodiscard]] std::expected<std::filesystem::path, std::string>
    ensure_testcases(const leetcode::Question& q) const;

    /// Existing solution files for a problem, any language.
    [[nodiscard]] std::vector<std::filesystem::path> solutions_for(std::string_view slug) const;

private:
    std::filesystem::path root_;
};

} // namespace lv::core
