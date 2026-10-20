#include "core/definitions.hpp"

#include "util/strings.hpp"

#include <algorithm>
#include <format>
#include <span>
#include <vector>

namespace lv::core {

namespace {

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

} // namespace lv::core
