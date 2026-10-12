#include "content/markdown.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lv::content {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
// Letters, digits and any non-ASCII byte: characters that make '_' intraword.
bool is_word(char c) { return is_alpha(c) || is_digit(c) || static_cast<unsigned char>(c) >= 0x80; }

std::size_t longest_run(std::string_view s, char c) {
    std::size_t longest = 0;
    std::size_t run = 0;
    for (const char ch : s) {
        run = ch == c ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    return longest;
}

std::string code_span(std::string_view text) {
    std::string body(text);
    std::ranges::replace(body, '\n', ' ');
    const std::string fence(longest_run(body, '`') + 1, '`');
    const bool pad = body.starts_with('`') || body.ends_with('`')
                     || (body.starts_with(' ') && body.ends_with(' ')
                         && body.find_first_not_of(' ') != std::string::npos);
    const std::string_view space = pad ? " " : "";
    return fence + std::string(space) + body + std::string(space) + fence;
}

std::string destination(std::string_view url) {
    if (url.find_first_of(" ()<>\n") == std::string_view::npos) {
        return std::string(url);
    }
    std::string out = "<";
    for (const char c : url) {
        if (c == '<') {
            out += "%3C";
        } else if (c == '>') {
            out += "%3E";
        } else if (c != '\n') {
            out += c;
        }
    }
    return out + ">";
}

// At the start of a line, text that would begin a block ("# ", "> ", "- ", "1. ") needs one
// escaped character. Returns its index in `s`, or npos.
std::size_t block_start_escape(std::string_view s) {
    const auto after = [&](std::size_t i) { return i < s.size() ? s[i] : ' '; };
    switch (s.front()) {
    case '>': return 0;
    case '#': {
        const std::size_t run = std::min(s.find_first_not_of('#'), s.size());
        return run <= 6 && after(run) == ' ' ? 0 : std::string_view::npos;
    }
    case '-': return after(1) == ' ' || after(1) == '-' ? 0 : std::string_view::npos;
    case '+':
    case '*': return after(1) == ' ' ? 0 : std::string_view::npos;
    default: break;
    }
    std::size_t digits = 0;
    while (digits < s.size() && is_digit(s[digits])) {
        ++digits;
    }
    if (digits == 0 || digits > 9) {
        return std::string_view::npos;
    }
    const char punct = after(digits);
    if ((punct == '.' || punct == ')') && after(digits + 1) == ' ') {
        return digits;
    }
    return std::string_view::npos;
}

bool needs_escape(char c, char prev, char next) {
    switch (c) {
    case '\\':
    case '`': return true;
    case '*': return prev != ' ' || next != ' ';
    case '_': return !is_word(prev) || !is_word(next);
    case '<': return is_alpha(next) || next == '/' || next == '!' || next == '?';
    case '&': return is_alpha(next) || next == '#';
    case ']': return next == '(';
    case '~': return prev == '~' || next == '~';
    default: return false;
    }
}

// Renders spans as inline Markdown. Marks are opened and closed like a stack so that
// "**a *b***" nests properly when adjacent spans share marks.
class Inline {
public:
    // `indent` prefixes the lines that follow a hard line break (list item continuation).
    explicit Inline(std::string indent) : indent_(std::move(indent)) {}

    std::string render(const std::vector<Span>& spans) && {
        for (const Span& span : spans) {
            add(span);
        }
        while (!stack_.empty()) {
            close();
        }
        return std::move(out_);
    }

private:
    struct Open {
        std::uint8_t mark;
        std::string href;
    };

    static constexpr std::array nesting{MarkLink, MarkBold, MarkItalic, MarkStrike};

    void add(const Span& span) {
        std::uint8_t wanted = MarkNone;
        for (const Mark mark : nesting) {
            if ((span.marks & mark) != 0) {
                wanted |= mark;
            }
        }
        std::size_t keep = 0;
        while (keep < stack_.size() && (stack_[keep].mark & wanted) != 0
               && (stack_[keep].mark != MarkLink || stack_[keep].href == span.href)) {
            ++keep;
        }
        while (stack_.size() > keep) {
            close();
        }

        std::string_view body = span.text;
        const auto missing = [&](Mark mark) {
            return (wanted & mark) != 0
                   && std::ranges::none_of(stack_, [&](const Open& o) { return o.mark == mark; });
        };
        if (std::ranges::any_of(nesting, missing)) {
            // Delimiters must hug the text, so leading spaces go before them.
            while (body.starts_with(' ')) {
                out_ += ' ';
                body.remove_prefix(1);
            }
            if (body.empty()) {
                return;
            }
            for (const Mark mark : nesting) {
                if (missing(mark)) {
                    open(mark, span.href);
                }
            }
        }
        if ((span.marks & MarkCode) != 0) {
            out_ += code_span(body);
            line_start_ = false;
        } else {
            text(body);
        }
    }

    void open(Mark mark, const std::string& href) {
        switch (mark) {
        case MarkLink: out_ += '['; break;
        case MarkBold: out_ += "**"; break;
        case MarkItalic: out_ += '*'; break;
        case MarkStrike: out_ += "~~"; break;
        default: break;
        }
        stack_.push_back({.mark = mark, .href = mark == MarkLink ? href : std::string()});
        line_start_ = false;
    }

    void close() {
        // Trailing spaces move outside the closing delimiter.
        const std::size_t kept = out_.find_last_not_of(' ') + 1;
        const std::size_t spaces = out_.size() - kept;
        out_.resize(kept);
        const Open& top = stack_.back();
        switch (top.mark) {
        case MarkLink: out_ += "](" + destination(top.href) + ")"; break;
        case MarkBold: out_ += "**"; break;
        case MarkItalic: out_ += '*'; break;
        case MarkStrike: out_ += "~~"; break;
        default: break;
        }
        out_.append(spaces, ' ');
        stack_.pop_back();
    }

    void text(std::string_view s) {
        std::size_t escape_at = std::string_view::npos;
        for (std::size_t i = 0; i < s.size(); ++i) {
            const char c = s[i];
            if (c == '\n') {
                out_ += "\\\n";
                out_ += indent_;
                line_start_ = true;
                continue;
            }
            if (line_start_) {
                const std::size_t at = block_start_escape(s.substr(i));
                escape_at = at == std::string_view::npos ? at : i + at;
                line_start_ = false;
            }
            const char prev = out_.empty() || out_.back() == '\n' ? ' ' : out_.back();
            const char next = i + 1 < s.size() ? s[i + 1] : '\0';
            if (i == escape_at || needs_escape(c, prev, next)) {
                out_ += '\\';
            }
            out_ += c;
        }
    }

    std::string out_;
    std::string indent_;
    bool line_start_ = true;
    std::vector<Open> stack_;
};

bool is_ordered_marker(std::string_view marker) {
    return marker.size() >= 2 && (marker.back() == '.' || marker.back() == ')')
           && std::ranges::all_of(marker.substr(0, marker.size() - 1), is_digit);
}

bool is_continuation(const Block& block) {
    return block.kind == BlockKind::ListItem
           && block.marker.find_first_not_of(' ') == std::string::npos;
}

std::string plain(const Block& block) {
    std::string out;
    for (const Span& span : block.spans) {
        out += span.text;
    }
    return out;
}

class Renderer {
public:
    std::string render(const Block& block) {
        if (block.kind != BlockKind::ListItem) {
            offsets_.clear();
        }
        switch (block.kind) {
        case BlockKind::Paragraph: return Inline("").render(block.spans);
        case BlockKind::Heading: {
            std::vector<Span> spans = block.spans;
            for (Span& span : spans) {
                std::ranges::replace(span.text, '\n', ' ');
            }
            const std::string text = Inline("").render(spans);
            if (text.empty()) {
                return {};
            }
            const auto level = static_cast<std::size_t>(std::clamp(block.level, 1, 6));
            return std::string(level, '#') + " " + text;
        }
        case BlockKind::ListItem: return list_item(block);
        case BlockKind::Preformatted: {
            const std::string text = plain(block);
            const std::string fence(std::max<std::size_t>(3, longest_run(text, '`') + 1), '`');
            return fence + "\n" + text + "\n" + fence;
        }
        case BlockKind::Rule: return "---";
        case BlockKind::Image: {
            std::string alt;
            for (const char c : plain(block)) {
                if (c == '[' || c == ']' || c == '\\') {
                    alt += '\\';
                }
                alt += c;
            }
            return "![" + alt + "](" + destination(block.src) + ")";
        }
        }
        return {};
    }

private:
    // Nested items are indented to the content column of their parent ("- " -> 2, "1. " -> 3).
    std::string list_item(const Block& block) {
        const auto level = static_cast<std::size_t>(std::max(block.level, 1));
        const auto parent = [&](std::size_t l) {
            return l == 0 ? 0 : l <= offsets_.size() ? offsets_[l - 1] : 2 * l;
        };
        if (is_continuation(block)) {
            const std::string indent(parent(level), ' ');
            const std::string text = Inline(indent).render(block.spans);
            return text.empty() ? text : indent + text;
        }
        const std::string indent(parent(level - 1), ' ');
        const std::string marker = is_ordered_marker(block.marker) ? block.marker : "-";
        offsets_.resize(level, 0);
        offsets_[level - 1] = indent.size() + marker.size() + 1;
        const std::string text = Inline(std::string(offsets_[level - 1], ' ')).render(block.spans);
        return indent + marker + " " + text;
    }

    std::vector<std::size_t> offsets_; // content column of the current item at each level
};

} // namespace

std::string to_markdown(const Document& doc) {
    std::string out;
    Renderer renderer;
    const Block* prev = nullptr;
    for (const Block& block : doc.blocks) {
        const std::string text = renderer.render(block);
        if (text.empty()) {
            continue;
        }
        if (prev != nullptr) {
            const bool tight = prev->kind == BlockKind::ListItem
                               && block.kind == BlockKind::ListItem && !is_continuation(block);
            out += tight ? "\n" : "\n\n";
        }
        out += text;
        prev = &block;
    }
    if (!out.empty()) {
        out += '\n';
    }
    return out;
}

} // namespace lv::content
