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

/// Builds a new solution file: a header comment, optional injected text, the types the
/// snippet relies on (provided_definitions) and the starter snippet between
/// `@lc code=start` / `@lc code=end` markers.
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

constexpr std::string_view default_filename_pattern = "{id}.{slug}.{ext}";
/// Checks a solution path pattern: relative, naming the problem ({id} or {slug}) and the
/// language ({lang} or {ext}), with no other placeholders. Returns an error, or "".
std::string check_filename_pattern(std::string_view pattern);
/// The pattern filled in: "{id}.{slug}.{ext}" -> "1.two-sum.cpp".
std::string solution_filename(std::string_view pattern, const leetcode::Question& q,
                              const Language& lang);

/// Groups raw testcase lines into cases of `lines_per_case` lines (0: one case).
std::vector<std::string> group_testcases(std::string_view input, std::size_t lines_per_case);

/// Solution files and custom testcases on disk.
class Workspace {
public:
    explicit Workspace(std::filesystem::path root,
                       std::string filename_pattern = std::string(default_filename_pattern))
        : root_(std::move(root)), pattern_(std::move(filename_pattern)) {}

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }
    [[nodiscard]] std::filesystem::path solution_path(const leetcode::Question& q,
                                                      const Language& lang) const;
    [[nodiscard]] std::filesystem::path testcases_path(std::string_view slug) const;

    /// Creates the solution file if it does not exist yet.
    [[nodiscard]] std::expected<std::filesystem::path, std::string>
    ensure_solution(const leetcode::Question& q, const Language& lang, std::string_view url,
                    std::string_view inject_before = {}, std::string_view inject_after = {}) const;

    /// Brings `lang`'s support files up to date. They go in the language's directory: the
    /// workspace, unless the filename pattern starts with {lang}/ (keeping Python3's away from
    /// Python 2 and Pandas files). A file is written where it's missing or still leetvim's and
    /// out of date (Rust's lists the solutions), and not while a config file of the user's
    /// takes precedence. Returns what couldn't be written, or "".
    [[nodiscard]] std::string ensure_support(const Language& lang) const;

    /// Custom testcases if saved, otherwise the question's examples; one entry per case.
    [[nodiscard]] std::vector<std::string> testcases(const leetcode::Question& q) const;
    /// Writes the testcase file (seeding it from the examples) and returns its path.
    [[nodiscard]] std::expected<std::filesystem::path, std::string>
    ensure_testcases(const leetcode::Question& q) const;

private:
    std::filesystem::path root_;
    std::string pattern_;
};

} // namespace lv::core
