#include "core/definitions.hpp"

#include "util/strings.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>

namespace lv::core {

namespace {

using Lines = std::vector<std::string>;
using Words = std::span<const std::string_view>;

constexpr std::string_view definition_marker = "Definition for";

/// The line's text without leading spaces and tabs.
std::string_view trim_left(std::string_view line) {
    const auto first = line.find_first_not_of(" \t");
    return first == std::string_view::npos ? std::string_view() : line.substr(first);
}

std::string_view trim_right(std::string_view line) {
    const auto last = line.find_last_not_of(" \t");
    return last == std::string_view::npos ? std::string_view() : line.substr(0, last + 1);
}

bool blank(std::string_view line) { return str::trim(line).empty(); }

std::size_t indent(std::string_view line) { return line.size() - trim_left(line).size(); }

std::string_view leading_space(std::string_view line) { return line.substr(0, indent(line)); }

bool word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

/// The identifier `text` starts with, or "".
std::string_view leading_word(std::string_view text) {
    std::size_t end = 0;
    while (end < text.size() && word_char(text[end])) ++end;
    return text.substr(0, end);
}

bool contains(Words words, std::string_view word) {
    return std::ranges::find(words, word) != words.end();
}

/// Whether `line` holds `word` on its own rather than inside a longer word.
bool has_word(std::string_view line, std::string_view word) {
    const auto letter = [&](std::size_t i) { return i < line.size() && word_char(line[i]); };
    for (auto at = line.find(word); at != std::string_view::npos; at = line.find(word, at + 1)) {
        if ((at == 0 || !letter(at - 1)) && !letter(at + word.size())) return true;
    }
    return false;
}

/// `line` with `word` replaced by `with` wherever it stands on its own.
std::string replace_word(std::string_view line, std::string_view word, std::string_view with) {
    std::string out;
    for (std::size_t i = 0; i < line.size();) {
        const bool here = line.substr(i).starts_with(word) && (i == 0 || !word_char(line[i - 1]))
                          && (i + word.size() == line.size() || !word_char(line[i + word.size()]));
        out += here ? with : line.substr(i, 1);
        i += here ? word.size() : 1;
    }
    return out;
}

/// A line of code without the comment after it: `int next(); // ...` -> `int next();`.
std::string_view code_of(std::string_view text) {
    const auto comment = text.find(" //");
    return comment == std::string_view::npos ? text : trim_right(text.substr(0, comment));
}

std::ptrdiff_t opened_braces(std::string_view text) {
    const auto code = code_of(text);
    return std::ranges::count(code, '{') - std::ranges::count(code, '}');
}

/// The column a line's text starts at, a tab going to the next multiple of 8 as in Python:
/// LeetCode's Ruby comments indent with both.
std::size_t column(std::string_view line) {
    std::size_t col = 0;
    for (const char c : line.substr(0, indent(line))) col = c == '\t' ? (col / 8 + 1) * 8 : col + 1;
    return col;
}

constexpr auto modifiers = std::to_array<std::string_view>(
    {"abstract", "async", "final", "internal", "open", "override", "private", "protected", "pub",
     "public", "static", "unsafe", "virtual"});

/// A declaration without the modifiers in front of it: "public static int f()" -> "int f()".
std::string_view without_modifiers(std::string_view text) {
    for (auto word = leading_word(text);
         contains(modifiers, word) && text.size() > word.size() && text[word.size()] == ' ';
         word = leading_word(text)) {
        text = trim_left(text.substr(word.size()));
    }
    return text;
}

constexpr auto type_words = std::to_array<std::string_view>(
    {"class", "enum", "impl", "interface", "object", "struct", "trait", "type"});

/// Whether `text` opens a type rather than declaring a function.
bool declares_type(std::string_view text) {
    return contains(type_words, leading_word(without_modifiers(text)));
}

/// A line inside a /* ... */ comment without its leading " * ", if it has one.
std::string_view strip_star(std::string_view line) {
    const auto text = trim_left(line);
    if (!text.starts_with('*') || text.starts_with("*/")) return line;
    const auto rest = text.substr(1);
    return rest.starts_with(' ') ? rest.substr(1) : rest;
}

/// A line comment's text: `prefix` (repeated, as in "%%") removed.
std::string_view strip_line_comment(std::string_view line, std::string_view prefix) {
    auto text = trim_left(line);
    while (text.starts_with(prefix)) text.remove_prefix(prefix.size());
    return text;
}

/// Whether `line` is a doc comment's tag, like "@param {ListNode} l1".
bool doc_tag(std::string_view line) {
    const auto text = str::trim(line);
    const auto word = text.starts_with('@') ? leading_word(text.substr(1)) : std::string_view();
    return word == "param" || word == "return";
}

/// What follows a heading in a comment: the code of the types LeetCode defines ("Definition
/// for singly-linked list."), or the API it defines, described in prose around its
/// declarations ("The API isBadVersion is defined for you.", "This is the MountainArray's
/// API interface.").
enum class Section : std::uint8_t { none, definition, api };

Section heading_kind(std::string_view line) {
    const bool api = has_word(line, "API") || has_word(line, "interface");
    if (line.contains(definition_marker)) {
        // JavaScript's "Definition for isBadVersion()" describes a function.
        return api || str::trim(line).ends_with("()") ? Section::api : Section::definition;
    }
    return api ? Section::api : Section::none;
}

// Brace languages. A function declaration is a line with parameters that ends in `;` or an
// empty body (`{}`), or nothing at all after a keyword that opens one (Go's `func`).

/// The lines of code in an API's description in a brace language: those ending in `;`, `{`
/// or `}`, those opening a declaration with one of `keywords`, access specifiers and
/// comments, without the prose around them, its doc tags ("@param num your guess") or a
/// closing brace with nothing to close (LeetCode's C NestedInteger has one). Empty when that
/// leaves only comments, as in C's "customFunction API".
Lines braced_code(ApiDescription description, Words keywords = {}) {
    Lines out;
    bool code = false;
    std::ptrdiff_t depth = 0;
    for (const auto line : description) {
        const auto text = str::trim(line);
        if (text.empty() || text.starts_with("//")) {
            out.emplace_back(line);
            continue;
        }
        const auto code_part = code_of(text);
        const bool access = text == "public:" || text == "protected:" || text == "private:";
        const bool ends =
            code_part.ends_with(';') || code_part.ends_with('{') || code_part.ends_with('}');
        const bool keyword = contains(keywords, leading_word(without_modifiers(text)));
        if (text.starts_with('@') || (!access && !ends && !keyword)) continue;
        const auto opened = opened_braces(text);
        if (depth + opened < 0) continue;
        depth += opened;
        out.emplace_back(line);
        code = true;
    }
    return code ? out : Lines{};
}

/// Joins a line ending in `{` with the `}` or `};` that closes it after nothing but blank
/// lines and comments, which go above it: an empty body, once the prose in it is gone.
void join_empty_bodies(Lines& lines) {
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto text = str::trim(lines[i]);
        const auto code = code_of(text);
        if (text.starts_with("//") || !code.ends_with('{')) continue;
        auto close = i + 1;
        while (close < lines.size()
               && (blank(lines[close]) || str::trim(lines[close]).starts_with("//"))) {
            ++close;
        }
        if (close == lines.size()) break;
        if (const auto end = str::trim(lines[close]); end != "}" && end != "};") continue;
        Lines joined;
        for (auto j = i + 1; j < close; ++j) {
            if (!blank(lines[j])) {
                joined.push_back(std::format("{}{}", leading_space(lines[i]), str::trim(lines[j])));
            }
        }
        joined.push_back(
            std::format("{}{}}}{}", leading_space(lines[i]), code, text.substr(code.size())));
        const auto at = lines.begin() + static_cast<std::ptrdiff_t>(i);
        lines.erase(at, lines.begin() + static_cast<std::ptrdiff_t>(close) + 1);
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(i), joined.begin(), joined.end());
        i += joined.size() - 1;
    }
}

/// `};` as `}`: only C and C++ need the semicolon after a type.
void drop_semicolons_after_braces(Lines& lines) {
    for (auto& line : lines) {
        if (str::trim(line) == "};") line = std::string(leading_space(line)) + "}";
    }
}

/// A function declaration without its terminator or the comment after it, or nullopt for a
/// line that isn't one.
std::optional<std::string_view> function_head(std::string_view text, Words keywords) {
    if (text.starts_with("//") || declares_type(text)) return std::nullopt;
    text = code_of(text);
    const bool keyword = contains(keywords, leading_word(without_modifiers(text)));
    for (const std::string_view end : {"{ }", "{}", ";"}) {
        if (text.ends_with(end)) {
            const auto head = trim_right(text.substr(0, text.size() - end.size()));
            return keyword || head.contains('(') ? std::optional(head) : std::nullopt;
        }
    }
    return keyword && !text.ends_with('{') && !text.ends_with('}') ? std::optional(text)
                                                                   : std::nullopt;
}

/// Gives each function declared among `lines` the body `stub`.
void stub_functions(Lines& lines, Words keywords, std::string_view stub) {
    for (auto& line : lines) {
        const auto text = str::trim(line);
        if (const auto head = function_head(text, keywords)) {
            line = std::format("{}{} {}{}", leading_space(line), *head, stub,
                               text.substr(code_of(text).size()));
        }
    }
}

/// Moves the functions declared at the top level that `pick` accepts into a block `header {
/// ... }` where the first of them was.
template <typename Pick>
void wrap_functions(Lines& lines, Words keywords, std::string_view header, Pick pick) {
    Lines rest;
    Lines members;
    std::size_t at = 0;
    std::ptrdiff_t depth = 0;
    for (auto& line : lines) {
        const auto text = str::trim(line);
        const auto opened = opened_braces(text);
        if (depth == 0 && function_head(text, keywords) && pick(text)) {
            if (members.empty()) at = rest.size();
            members.push_back(std::format("    {}", text));
        } else {
            rest.push_back(std::move(line));
        }
        depth += opened;
    }
    if (!members.empty()) {
        members.insert(members.begin(), std::format("{} {{", header));
        members.emplace_back("}");
        rest.insert(rest.begin() + static_cast<std::ptrdiff_t>(at), members.begin(), members.end());
    }
    lines = std::move(rest);
}

/// The class the snippet's Solution extends (`class Solution extends X`), or "".
std::string_view solution_parent(std::string_view snippet) {
    for (const auto line : str::lines(snippet)) {
        auto text = without_modifiers(str::trim(line));
        if (leading_word(text) != "class") continue;
        text = trim_left(text.substr(5));
        if (leading_word(text) != "Solution") continue;
        text = trim_left(text.substr(8));
        if (leading_word(text) != "extends") return {};
        return leading_word(trim_left(text.substr(7)));
    }
    return {};
}

/// Puts the functions declared outside any type into the class the solution inherits them
/// from, which LeetCode only names.
void in_parent_class(Lines& lines, std::string_view snippet, Words keywords) {
    const auto parent = solution_parent(snippet);
    if (parent.empty()) return;
    for (const auto& line : lines) {
        const auto text = without_modifiers(str::trim(line));
        if (declares_type(text)
            && leading_word(trim_left(text.substr(leading_word(text).size()))) == parent) {
            return; // the description declares it
        }
    }
    wrap_functions(lines, keywords, std::format("class {}", parent),
                   [](std::string_view) { return true; });
}

/// LeetCode's `interface X` for an API whose methods it shows with bodies, or whose objects
/// a solution creates (Java's NestedInteger): the class it is.
void interfaces_as_classes(Lines& lines) {
    for (auto& line : lines) {
        const auto text = str::trim(line);
        const auto bare = without_modifiers(text);
        if (leading_word(bare) == "interface") {
            line = std::format("{}{}class{}", leading_space(line),
                               text.substr(0, text.size() - bare.size()), bare.substr(9));
        }
    }
}

/// The parameters between the parentheses of `head` that start at `open`, split at the top
/// level, and where they end.
std::pair<std::vector<std::string_view>, std::size_t> parameters(std::string_view head,
                                                                 std::size_t open) {
    std::vector<std::string_view> params;
    int depth = 0;
    std::size_t start = open + 1;
    for (auto i = open; i < head.size(); ++i) {
        const char c = head[i];
        if (c == '(' || c == '[' || c == '<' || c == '{') ++depth;
        if (c == ')' || c == ']' || c == '>' || c == '}') --depth;
        if ((c == ',' && depth == 1) || depth == 0) {
            if (const auto param = str::trim(head.substr(start, i - start)); !param.empty()) {
                params.push_back(param);
            }
            start = i + 1;
        }
        if (depth == 0) return {params, i};
    }
    return {params, std::string_view::npos};
}

/// A TypeScript declaration of the function LeetCode shows as `head`: parameters and return
/// types it leaves out as `any`, which servers check with noImplicitAny; JavaScript's `var f
/// = function(x)` as `function f(x)`; at the top level, `declare`d.
std::string typescript_declaration(std::string_view head, bool top_level) {
    const auto open = head.find('(');
    if (open == std::string_view::npos) return std::format("{};", head);
    const auto [params, close] = parameters(head, open);
    if (close == std::string_view::npos) return std::format("{};", head);
    std::string name(trim_right(head.substr(0, open)));
    if (const auto word = leading_word(name); word == "var" || word == "let" || word == "const") {
        auto rest = trim_left(std::string_view(name).substr(word.size()));
        const auto var = leading_word(rest);
        rest = trim_left(rest.substr(var.size()));
        if (rest.starts_with('=') && str::trim(rest.substr(1)) == "function") {
            name = std::format("function {}", var);
        }
    }
    if (top_level && leading_word(name) != "function") name = "function " + name;
    std::string typed;
    for (const auto param : params) {
        if (!typed.empty()) typed += ", ";
        if (param.contains(':')) {
            typed += param;
        } else if (const auto eq = param.find('='); eq != std::string_view::npos) {
            typed += std::format("{}: any {}", trim_right(param.substr(0, eq)), param.substr(eq));
        } else {
            typed += std::format("{}: any", param);
        }
    }
    auto returns = str::trim(head.substr(close + 1));
    const bool constructor = leading_word(without_modifiers(name)) == "constructor";
    return std::format("{}{}({}){}{};", top_level ? "declare " : "", name, typed,
                       returns.empty() && !constructor ? ": any" : "",
                       returns.empty() ? "" : returns);
}

/// Adds the structs a line of C names, `struct X` or a capitalized `X *`, to `names`.
void add_struct_names(std::string_view line, std::vector<std::string>& names) {
    const auto at_word = [&](std::size_t i) { return i < line.size() && word_char(line[i]); };
    bool after_struct = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (!at_word(i) || std::isdigit(static_cast<unsigned char>(line[i])) != 0) continue;
        auto end = i;
        while (at_word(end)) ++end;
        const auto name = line.substr(i, end - i);
        const auto next = line.find_first_not_of(' ', end);
        const bool pointer = next != std::string_view::npos && line[next] == '*';
        if ((after_struct || (pointer && std::isupper(static_cast<unsigned char>(name[0])) != 0))
            && std::ranges::find(names, name) == names.end()) {
            names.emplace_back(name);
        }
        after_struct = name == "struct";
        i = end - 1;
    }
}

// Indented languages.

bool opens_block(std::string_view line) {
    const auto text = trim_left(line);
    return text.starts_with("def ") || text.starts_with("class ");
}

/// A Python API's `def` line as a declaration's. Without a return type it returns Any, so
/// pyright takes neither the body it gets for the result nor its `raise` for one that never
/// returns (`__init__` must return None), and LeetCode's `-> [T]` for a list of T becomes a
/// type pyright and ruff accept.
std::string declare_def(std::string_view line) {
    std::string out(line);
    const auto text = str::trim(line);
    if (!text.starts_with("def ") || !text.ends_with(':')) return out;
    const auto colon = out.find_last_not_of(" \t");
    const auto arrow = out.find("->");
    if (arrow == std::string::npos) {
        if (!text.starts_with("def __init__(")) out.insert(colon, " -> Any");
        return out;
    }
    const auto type = str::trim(std::string_view(out).substr(arrow + 2, colon - arrow - 2));
    if (type.starts_with('[') && type.ends_with(']')) {
        // Quoted, since it names the class the method is in.
        return std::format("{}-> \"list{}\":", out.substr(0, arrow), type);
    }
    return out;
}

/// `lines` without blank lines at either end or in a row, trailing spaces, or the indentation
/// they all share, counted in columns: a line indented with tabs (LeetCode mixes them in)
/// keeps a tab that goes past it.
void tidy(Lines& lines) {
    Lines kept;
    for (const auto& line : lines) {
        if (blank(line) && (kept.empty() || kept.back().empty())) continue;
        kept.emplace_back(trim_right(line));
    }
    if (!kept.empty() && kept.back().empty()) kept.pop_back();
    std::size_t shared = std::string_view::npos;
    for (const auto& line : kept) {
        if (!line.empty()) shared = std::min(shared, column(line));
    }
    for (auto& line : kept) {
        std::size_t col = 0;
        std::size_t cut = 0;
        for (; cut < indent(line); ++cut) {
            const auto next = line[cut] == '\t' ? (col / 8 + 1) * 8 : col + 1;
            if (next > shared) break;
            col = next;
        }
        line.erase(0, cut);
    }
    lines = std::move(kept);
}

/// Appends each section of one comment, given as its lines without the comment syntax, under
/// its heading kept as a comment: the code after "Definition for ...", and the declarations
/// in an API's description. Only a comment at the top level of the snippet describes an API;
/// one in the code may mention one in passing.
void add_definitions(std::span<const std::string_view> body, const Language& lang, bool top_level,
                     std::string_view snippet, std::string& out) {
    auto section = Section::none;
    std::string_view heading;
    std::vector<std::string_view> code;
    const auto flush = [&] {
        Lines lines;
        if (section == Section::definition) {
            lines.reserve(code.size());
            for (const auto line : code) lines.emplace_back(line);
        } else if (section == Section::api && lang.declare_api != nullptr) {
            lines = lang.declare_api(code, snippet);
        }
        tidy(lines);
        if (!lines.empty()) {
            if (!out.empty()) out += '\n';
            out += std::format("{} {}\n", lang.comment, heading);
        }
        for (std::string_view line : lines) {
            // Java allows one public class per file, named after it.
            if (lang.slug == "java" && line.starts_with("public ")) line.remove_prefix(7);
            out += line;
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
        const auto kind = heading_kind(line);
        if (kind == Section::definition) {
            flush();
            section = kind;
            heading = str::trim(line.substr(line.find(definition_marker)));
        } else if (kind == Section::api && section != Section::api && top_level) {
            flush();
            section = kind;
            heading = str::trim(line);
            while (heading.starts_with(lang.comment)) {
                heading = str::trim(heading.substr(lang.comment.size()));
            }
        } else if (section == Section::definition && doc_tag(line)) {
            // The solution's docs, after the types in the same comment (Ruby's).
            flush();
            section = Section::none;
        } else if (section != Section::none) {
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
        const bool top_level = text.size() == lines[i].size();
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
        add_definitions(body, lang, top_level, snippet, out);
    }
    return out;
}

namespace api {

namespace {

constexpr auto go_words = std::to_array<std::string_view>({"func"});
constexpr auto php_words = std::to_array<std::string_view>({"function"});
constexpr auto rust_words = std::to_array<std::string_view>({"fn"});

} // namespace

Lines cpp(ApiDescription description, std::string_view /*snippet*/) {
    return braced_code(description);
}

Lines c(ApiDescription description, std::string_view snippet) {
    auto out = cpp(description, snippet);
    std::vector<std::string> structs;
    for (const auto& line : out) {
        if (const auto text = str::trim(line); !text.starts_with("//")) {
            add_struct_names(text.substr(0, text.find("//")), structs);
        }
    }
    if (structs.empty()) return out;
    // LeetCode leaves them undeclared: typedefs above the first declaration and its comment.
    auto at = std::ranges::find_if(out, [](std::string_view line) {
        const auto text = str::trim(line);
        return !text.empty() && !text.starts_with("//");
    });
    while (at != out.begin() && str::trim(*(at - 1)).starts_with("//")) --at;
    std::vector<std::string> typedefs;
    typedefs.reserve(structs.size());
    for (const auto& name : structs) {
        typedefs.push_back(std::format("typedef struct {0} {0};", name));
    }
    out.insert(at, typedefs.begin(), typedefs.end());
    return out;
}

Lines golang(ApiDescription description, std::string_view /*snippet*/) {
    auto lines = braced_code(description, go_words);
    join_empty_bodies(lines);
    stub_functions(lines, go_words, R"({ panic("") })");
    return lines;
}

Lines java(ApiDescription description, std::string_view snippet) {
    auto lines = braced_code(description);
    join_empty_bodies(lines);
    drop_semicolons_after_braces(lines);
    interfaces_as_classes(lines);
    in_parent_class(lines, snippet, {});
    stub_functions(lines, {}, "{ throw new UnsupportedOperationException(); }");
    return lines;
}

Lines php(ApiDescription description, std::string_view snippet) {
    auto lines = braced_code(description, php_words);
    join_empty_bodies(lines);
    drop_semicolons_after_braces(lines);
    interfaces_as_classes(lines);
    in_parent_class(lines, snippet, php_words);
    stub_functions(lines, php_words, "{}");
    return lines;
}

Lines python(ApiDescription description, std::string_view /*snippet*/) {
    // Where the block opened at `i` ends: past the last line indented under it.
    const auto block_end = [](const auto& lines, std::size_t i) {
        auto end = i + 1;
        for (auto j = i + 1; j < lines.size(); ++j) {
            if (blank(lines[j])) continue;
            if (column(lines[j]) <= column(lines[i])) break;
            end = j + 1;
        }
        return end;
    };
    // Each `class` and `def` with the lines indented under it, without the prose around them.
    Lines out;
    for (std::size_t i = 0; i < description.size(); ++i) {
        if (!opens_block(description[i])) continue;
        const auto end = block_end(description, i);
        for (const auto line : description.subspan(i, end - i)) out.push_back(declare_def(line));
        i = end - 1;
    }
    // LeetCode shows a `def` without a body or with only a docstring. Raising gives it one
    // that pyright accepts for its declared type, where `...` after a docstring is one ruff
    // flags; an empty class gets `...`. From the end, so an insertion moves only the blocks
    // already done.
    for (auto i = out.size(); i-- > 0;) {
        if (!opens_block(out[i])) continue;
        const auto end = block_end(out, i);
        const bool def = trim_left(out[i]).starts_with("def ");
        if (!def && end > i + 1) continue;
        auto first = i + 1;
        while (first < end && blank(out[first])) ++first;
        const auto pad = first < end ? std::string(leading_space(out[first]))
                                     : std::format("{}    ", leading_space(out[i]));
        out.insert(out.begin() + static_cast<std::ptrdiff_t>(end),
                   pad + (def ? "raise NotImplementedError" : "..."));
    }
    return out;
}

Lines rust(ApiDescription description, std::string_view /*snippet*/) {
    auto lines = braced_code(description, rust_words);
    join_empty_bodies(lines);
    drop_semicolons_after_braces(lines);
    for (auto& line : lines) {
        if (str::trim(line).starts_with("//")) continue;
        // LeetCode's Rust Master returns `int`.
        line = replace_word(line, "int", "i32");
        // `isBadVersion(version:i32)-> bool;`
        const auto text = str::trim(line);
        if (function_head(text, rust_words) && leading_word(without_modifiers(text)) != "fn") {
            line = std::format("{}fn {}", leading_space(line), text);
        }
    }
    // One called on self (`self.isBadVersion(version)`) is a method of Solution.
    std::vector<std::string_view> methods;
    for (const auto line : description) {
        for (auto at = line.find("self."); at != std::string_view::npos;
             at = line.find("self.", at + 1)) {
            const auto name = leading_word(line.substr(at + 5));
            if (line.substr(at + 5 + name.size()).starts_with('(')) methods.push_back(name);
        }
    }
    wrap_functions(lines, rust_words, "impl Solution", [&](std::string_view text) {
        const auto fn = without_modifiers(text);
        const auto name = leading_word(trim_left(fn.substr(std::min<std::size_t>(2, fn.size()))));
        return std::ranges::find(methods, name) != methods.end();
    });
    // A function in an impl block takes self: LeetCode leaves it out, but a solution calls
    // the API's functions as methods (`mountainArr.get(i)`).
    std::vector<bool> impls; // whether each open block is an impl
    for (auto& line : lines) {
        const auto text = without_modifiers(str::trim(line));
        const auto opened = opened_braces(text);
        const bool impl = leading_word(text) == "impl";
        if (!impls.empty() && impls.back() && leading_word(text) == "fn") {
            const auto open = line.find('(');
            const auto params = trim_left(std::string_view(line).substr(open + 1));
            if (!params.starts_with("&self") && !params.starts_with("self")
                && !params.starts_with("&mut self") && !params.starts_with("mut self")) {
                line.insert(open + 1, params.starts_with(')') ? "&self" : "&self, ");
            }
        }
        if (opened > 0) impls.push_back(impl);
        for (auto n = opened; n < 0 && !impls.empty(); ++n) impls.pop_back();
    }
    stub_functions(lines, rust_words, "{ unimplemented!() }");
    // LeetCode names some in camelCase (isBadVersion), which Rust warns about.
    Lines out;
    std::size_t item = 0; // where the top-level item being copied starts in `out`
    bool allowed = false;
    std::ptrdiff_t depth = 0;
    for (auto& line : lines) {
        const auto text = without_modifiers(str::trim(line));
        const auto opened = opened_braces(text);
        if (depth == 0) {
            item = out.size();
            allowed = false;
        }
        const auto name =
            leading_word(trim_left(text.substr(std::min<std::size_t>(2, text.size()))));
        if (!allowed && leading_word(text) == "fn" && std::ranges::any_of(name, [](char c) {
                return std::isupper(static_cast<unsigned char>(c)) != 0;
            })) {
            const auto pad = std::string(leading_space(item < out.size() ? out[item] : line));
            out.insert(out.begin() + static_cast<std::ptrdiff_t>(item),
                       pad + "#[allow(non_snake_case)]");
            allowed = true;
        }
        depth += opened;
        out.push_back(std::move(line));
    }
    return out;
}

Lines typescript(ApiDescription description, std::string_view /*snippet*/) {
    auto lines = braced_code(description);
    join_empty_bodies(lines);
    drop_semicolons_after_braces(lines);
    std::ptrdiff_t depth = 0;
    for (auto& line : lines) {
        const auto text = str::trim(line);
        const auto opened = opened_braces(text);
        if (depth == 0 && declares_type(text)) {
            line = std::format("declare {}", text);
        } else if (const auto head = function_head(text, {})) {
            line =
                std::format("{}{}", leading_space(line), typescript_declaration(*head, depth == 0));
        }
        depth += opened;
    }
    // A module's names are its own, not merged with the global ones TypeScript declares: the
    // Iterator a solution is given isn't the built-in one.
    while (!lines.empty() && blank(lines.back())) lines.pop_back();
    if (!lines.empty()) lines.emplace_back("export {}; // a module, so these names are its own");
    return lines;
}

} // namespace api

} // namespace lv::core
