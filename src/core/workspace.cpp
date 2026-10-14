#include "core/workspace.hpp"

#include "util/paths.hpp"
#include "util/strings.hpp"

#include <algorithm>
#include <format>
#include <span>
#include <system_error>

namespace lv::core {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view start_marker = "@lc code=start";
constexpr std::string_view end_marker = "@lc code=end";
constexpr std::string_view alt_start_marker = "@leet start";
constexpr std::string_view alt_end_marker = "@leet end";

/// Position just past the line containing `marker`, or npos.
std::size_t after_marker_line(std::string_view content, std::string_view marker) {
    const auto hit = content.find(marker);
    if (hit == std::string_view::npos) return hit;
    const auto eol = content.find('\n', hit);
    return eol == std::string_view::npos ? content.size() : eol + 1;
}

/// Start of the line containing `marker` at or after `from`, or npos.
std::size_t marker_line_start(std::string_view content, std::string_view marker, std::size_t from) {
    const auto hit = content.find(marker, from);
    if (hit == std::string_view::npos) return hit;
    const auto bol = content.rfind('\n', hit);
    return bol == std::string_view::npos ? 0 : bol + 1;
}

/// Whether `path` exists and holds `section` (anything, when empty).
bool holds(const fs::path& path, std::string_view section) {
    std::string text;
    return paths::read_file(path, text) && text.contains(section);
}

std::string with_trailing_newline(std::string_view s) {
    std::string out(s);
    if (!out.empty() && !out.ends_with('\n')) out += '\n';
    return out;
}

constexpr std::string_view definition_marker = "Definition for";

/// The line's text without leading spaces and tabs.
std::string_view trim_left(std::string_view line) {
    const auto first = line.find_first_not_of(" \t");
    return first == std::string_view::npos ? std::string_view() : line.substr(first);
}

/// A line inside a /* ... */ comment without its leading " * ", if it has one.
std::string_view strip_star(std::string_view line) {
    const auto text = trim_left(line);
    if (!text.starts_with('*') || text.starts_with("*/")) return line;
    const auto rest = text.substr(1);
    return rest.starts_with(' ') ? rest.substr(1) : rest;
}

/// A line comment's text: `prefix` (repeated, as in "%%") and one space removed.
std::string_view strip_line_comment(std::string_view line, std::string_view prefix) {
    auto text = trim_left(line);
    while (text.starts_with(prefix)) text.remove_prefix(prefix.size());
    return text.starts_with(' ') ? text.substr(1) : text;
}

/// Appends the code after each "Definition for ..." line of one comment, given as its lines
/// without the comment syntax, under that line kept as a comment.
void add_definitions(std::span<const std::string_view> body, const Language& lang,
                     std::string& out) {
    std::string_view heading;
    std::vector<std::string_view> code;
    const auto flush = [&] {
        const auto blank = [](std::string_view line) { return str::trim(line).empty(); };
        while (!code.empty() && blank(code.back())) code.pop_back();
        const auto first = std::ranges::find_if_not(code, blank);
        if (first != code.end()) {
            if (!out.empty()) out += '\n';
            out += std::format("{} {}\n", lang.comment, heading);
        }
        for (auto line : std::span(first, code.end())) {
            // Java allows one public class per file, named after it.
            if (lang.slug == "java" && line.starts_with("public ")) line.remove_prefix(7);
            out += blank(line) ? std::string_view() : line;
            // LeetCode's Python types give typed parameters None defaults: `next: 'Node' = None`.
            if (lang.ext == "py" && trim_left(line).starts_with("def ")
                && line.contains("= None")) {
                out += "  # pyright: ignore[reportArgumentType]";
            }
            out += '\n';
        }
        code.clear();
    };
    for (const auto line : body) {
        if (const auto at = line.find(definition_marker); at != std::string_view::npos) {
            flush();
            heading = str::trim(line.substr(at));
        } else if (!heading.empty()) {
            code.push_back(line);
        }
    }
    flush();
}

} // namespace

std::string provided_definitions(std::string_view snippet, const Language& lang) {
    const auto lines = str::lines(snippet);
    std::string out;
    std::vector<std::string_view> body;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto text = trim_left(lines[i]);
        body.clear();
        if (lang.comment == "//" && text.starts_with("/*")) {
            // "/**", " * ..." lines and " */"; or "/*", bare lines and "*/".
            for (auto line = text.substr(2);; line = lines[++i]) {
                const auto end = line.find("*/");
                body.push_back(strip_star(line.substr(0, end)));
                if (end != std::string_view::npos || i + 1 == lines.size()) break;
            }
        } else if (lang.ext == "py" && lines[i].starts_with('"')
                   && str::trim(lines[i]) == R"(""")") {
            // A module-level string holding commented-out classes.
            while (++i < lines.size() && str::trim(lines[i]) != R"(""")") body.push_back(lines[i]);
        } else if (text.starts_with(lang.comment)) {
            for (; i < lines.size() && trim_left(lines[i]).starts_with(lang.comment); ++i) {
                body.push_back(strip_line_comment(lines[i], lang.comment));
            }
            --i;
        }
        add_definitions(body, lang, out);
    }
    return out;
}

std::string render_solution(const leetcode::Question& q, const Language& lang, std::string_view url,
                            std::string_view inject_before, std::string_view inject_after) {
    const std::string_view c = lang.comment;
    std::string out = lang.opening.empty() ? "" : with_trailing_newline(lang.opening);
    out += std::format("{} @leetvim id={} slug={} lang={}\n", c, q.frontend_id, q.slug, lang.slug);
    out += std::format("{} {}. {} ({})\n", c, q.frontend_id, q.title,
                       leetcode::to_string(q.difficulty));
    out += std::format("{} {}\n", c, url);
    if (!inject_before.empty()) out += "\n" + with_trailing_newline(inject_before);
    const auto* snippet = q.snippet(lang.slug);
    if (snippet != nullptr) {
        if (const auto types = provided_definitions(snippet->code, lang); !types.empty()) {
            out += "\n" + types;
        }
    }
    out += std::format("\n{} {}\n", c, start_marker);
    out += with_trailing_newline(snippet != nullptr ? snippet->code : "");
    out += std::format("{} {}\n", c, end_marker);
    if (!inject_after.empty()) out += "\n" + with_trailing_newline(inject_after);
    return out;
}

std::string extract_code(std::string_view content) {
    for (const auto& [start, end] :
         {std::pair{start_marker, end_marker}, std::pair{alt_start_marker, alt_end_marker}}) {
        const auto begin = after_marker_line(content, start);
        if (begin == std::string_view::npos) continue;
        const auto stop = marker_line_start(content, end, begin);
        const auto code = content.substr(
            begin, stop == std::string_view::npos ? std::string_view::npos : stop - begin);
        return std::string(code);
    }
    return std::string(content);
}

std::optional<SolutionMeta> parse_header(std::string_view content) {
    const auto hit = content.find("@leetvim ");
    if (hit == std::string_view::npos) return std::nullopt;
    auto line = content.substr(hit + 9);
    line = line.substr(0, line.find('\n'));
    SolutionMeta meta;
    for (const auto field : str::split(str::trim(line), ' ')) {
        const auto eq = field.find('=');
        if (eq == std::string_view::npos) continue;
        const auto key = field.substr(0, eq);
        const auto value = std::string(str::trim(field.substr(eq + 1)));
        if (key == "id")
            meta.frontend_id = value;
        else if (key == "slug")
            meta.slug = value;
        else if (key == "lang")
            meta.lang = value;
    }
    if (meta.slug.empty()) return std::nullopt;
    return meta;
}

std::optional<SolutionMeta> parse_filename(const fs::path& path) {
    // "<id>.<slug>.<ext>"
    const std::string name = path.filename().string();
    const auto first = name.find('.');
    const auto last = name.rfind('.');
    if (first == std::string::npos || last == first) return std::nullopt;
    SolutionMeta meta;
    meta.frontend_id = name.substr(0, first);
    meta.slug = name.substr(first + 1, last - first - 1);
    if (const auto* lang = language_for_extension(name.substr(last + 1))) {
        meta.lang = lang->slug;
    }
    if (meta.slug.empty() || meta.frontend_id.empty()) return std::nullopt;
    return meta;
}

std::string check_filename_pattern(std::string_view pattern) {
    bool problem = false;
    bool language = false;
    for (std::size_t open = pattern.find('{'); open != std::string_view::npos;
         open = pattern.find('{', open + 1)) {
        const auto close = pattern.find('}', open);
        if (close == std::string_view::npos) return "unclosed {";
        const auto name = pattern.substr(open + 1, close - open - 1);
        if (name == "id" || name == "slug") {
            problem = true;
        } else if (name == "lang" || name == "ext") {
            language = true;
        } else {
            return std::format("unknown placeholder {{{}}}; use {{id}} {{slug}} {{lang}} {{ext}}",
                               name);
        }
    }
    if (!problem) return "needs {id} or {slug} to tell problems apart";
    if (!language) return "needs {ext} or {lang} to tell languages apart";
    if (pattern.starts_with('/') || pattern.starts_with('~')) {
        return "must be relative to the workspace";
    }
    for (const auto part : str::split(pattern, '/')) {
        if (part.empty() || part == "." || part == "..") return "has an empty, . or .. part";
    }
    return {};
}

std::string solution_filename(std::string_view pattern, const leetcode::Question& q,
                              const Language& lang) {
    std::string out(pattern);
    out = str::replace_all(out, "{id}", q.frontend_id);
    out = str::replace_all(out, "{slug}", q.slug);
    out = str::replace_all(out, "{lang}", lang.slug);
    out = str::replace_all(out, "{ext}", lang.ext);
    return out;
}

std::vector<std::string> group_testcases(std::string_view input, std::size_t lines_per_case) {
    std::vector<std::string> cases;
    auto lines = str::lines(input);
    while (!lines.empty() && str::trim(lines.back()).empty()) lines.pop_back();
    if (lines.empty()) return cases;
    if (lines_per_case == 0) {
        cases.emplace_back(input.substr(0, input.find_last_not_of("\r\n") + 1));
        return cases;
    }
    for (std::size_t i = 0; i < lines.size(); i += lines_per_case) {
        std::string one;
        for (std::size_t j = i; j < std::min(lines.size(), i + lines_per_case); ++j) {
            if (j != i) one += '\n';
            one += lines[j];
        }
        cases.push_back(std::move(one));
    }
    return cases;
}

fs::path Workspace::solution_path(const leetcode::Question& q, const Language& lang) const {
    return root_ / solution_filename(pattern_, q, lang);
}

fs::path Workspace::testcases_path(std::string_view slug) const {
    return root_ / "testcases" / std::format("{}.txt", slug);
}

std::expected<fs::path, std::string>
Workspace::ensure_solution(const leetcode::Question& q, const Language& lang, std::string_view url,
                           std::string_view inject_before, std::string_view inject_after) const {
    fs::path path = solution_path(q, lang);
    std::error_code ec;
    if (fs::exists(path, ec)) return path;
    if (q.snippet(lang.slug) == nullptr && !q.snippets.empty()) {
        return std::unexpected(std::format("{} is not available for this problem", lang.name));
    }
    if (!paths::write_file(path, render_solution(q, lang, url, inject_before, inject_after))) {
        return std::unexpected(std::format("cannot write {}", path.string()));
    }
    return path;
}

std::string Workspace::ensure_support(const Language& lang) {
    if (supported_.contains(lang.slug)) return {};
    const fs::path dir = pattern_.starts_with("{lang}/") ? root_ / lang.slug : root_;
    std::vector<std::string> failed;
    for (const auto& file : lang.support) {
        if (std::ranges::any_of(
                file.unless, [&](const Precedence& p) { return holds(dir / p.path, p.section); })) {
            continue;
        }
        const auto path = dir / file.path;
        const auto content = file.content();
        std::error_code ec;
        std::string current;
        if (fs::is_regular_file(path, ec) && paths::read_file(path, current)) {
            const bool ours = current.substr(0, current.find('\n')).contains(generated_marker);
            if (!ours || current == content) continue;
        }
        if (!paths::write_file(path, content)) failed.push_back(path.string());
    }
    if (failed.empty()) {
        supported_.emplace(lang.slug);
        return {};
    }
    return std::format("cannot write {}, so your LSP may not know what LeetCode provides",
                       str::join(failed, ", "));
}

std::vector<std::string> Workspace::testcases(const leetcode::Question& q) const {
    std::string text;
    if (paths::read_file(testcases_path(q.slug), text) && !str::trim(text).empty()) {
        return group_testcases(text, q.lines_per_case());
    }
    if (!q.example_testcases.empty()) return q.example_testcases;
    return group_testcases(q.sample_testcase, q.lines_per_case());
}

std::expected<fs::path, std::string>
Workspace::ensure_testcases(const leetcode::Question& q) const {
    fs::path path = testcases_path(q.slug);
    std::error_code ec;
    if (fs::exists(path, ec)) return path;
    std::string text;
    for (const auto& c : testcases(q)) text += with_trailing_newline(c);
    if (!paths::write_file(path, text)) {
        return std::unexpected(std::format("cannot write {}", path.string()));
    }
    return path;
}

} // namespace lv::core
