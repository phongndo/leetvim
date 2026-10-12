#include "content/layout.hpp"

#include "util/unicode.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lv::content {

namespace {

constexpr std::string_view gutter = "▎ ";
constexpr int tab_width = 4;
// Prefixes (gutters, indentation, list markers) are dropped when they would leave less than
// this many columns for text.
constexpr int min_text_width = 4;

// One grapheme of block text, or a (collapsed) space or line break.
struct Atom {
    std::string_view text;
    std::uint8_t marks = MarkNone;
    int width = 0;
    bool space = false;
    bool newline = false;
};

std::vector<Atom> atomize(const std::vector<Span>& spans, bool preformatted) {
    std::vector<Atom> atoms;
    for (const Span& span : spans) {
        const std::string_view text = span.text;
        std::size_t pos = 0;
        while (pos < text.size()) {
            const unicode::Grapheme g = unicode::next_grapheme(text, pos);
            const char first = g.bytes.front();
            if (first == '\n') {
                atoms.push_back({.newline = true});
            } else if (first == ' ' || first == '\t') {
                if (!preformatted && !atoms.empty() && atoms.back().space) {
                    continue;
                }
                const int count = preformatted && first == '\t' ? tab_width : 1;
                for (int i = 0; i < count; ++i) {
                    atoms.push_back({.text = " ", .marks = span.marks, .width = 1, .space = true});
                }
            } else if (static_cast<unsigned char>(first) >= 0x20) {
                atoms.push_back({.text = g.bytes, .marks = span.marks, .width = g.width});
            }
        }
    }
    return atoms;
}

struct Prefix {
    std::vector<LineSpan> spans;
    int width = 0;

    void add(std::string text, Tone tone) {
        if (text.empty()) {
            return;
        }
        width += unicode::display_width(text);
        spans.push_back({.text = std::move(text), .tone = tone});
    }

    [[nodiscard]] bool fits(int line_width) const { return line_width - width >= min_text_width; }
};

struct Style {
    Tone tone = Tone::Normal;
    std::uint8_t marks = MarkNone; // added to every text span
    bool code_block = false;
    bool keep_indent = false; // keep leading spaces of source lines (not of wrapped ones)
};

bool is_blank(const Line& line) {
    return std::ranges::all_of(line.spans, [](const LineSpan& span) {
        return span.text.find_first_not_of(' ') == std::string::npos;
    });
}

Line make_line(const Prefix* prefix, std::span<const Atom> atoms, const Style& style) {
    Line line{.code_block = style.code_block};
    if (prefix != nullptr) {
        line.spans = prefix->spans;
    }
    if (atoms.empty() && is_blank(line)) {
        return {.code_block = style.code_block};
    }
    const std::size_t text_start = line.spans.size();
    for (const Atom& atom : atoms) {
        const auto marks = static_cast<std::uint8_t>(atom.marks | style.marks);
        if (line.spans.size() > text_start && line.spans.back().marks == marks) {
            line.spans.back().text.append(atom.text);
        } else {
            line.spans.push_back(
                {.text = std::string(atom.text), .marks = marks, .tone = style.tone});
        }
    }
    return line;
}

int total_width(std::span<const Atom> atoms) {
    int width = 0;
    for (const Atom& atom : atoms) {
        width += atom.width;
    }
    return width;
}

// Greedy word wrap. `first` and `rest` (the prefix of continuation lines) have equal widths.
void wrap(std::span<const Atom> atoms, int width, const Prefix& first, const Prefix& rest,
          std::vector<Line>& out, const Style& style = {}) {
    const bool prefixed = first.fits(width);
    const int limit = prefixed ? width - first.width : width;
    std::vector<Atom> line;
    int line_width = 0;
    bool first_line = true;
    bool wrapped = false;      // the current line continues a wrapped one
    bool can_break = false;    // the line has a space after some text
    std::size_t break_at = 0;  // start of the last space run (where the line would end)
    std::size_t resume_at = 0; // end of that run (where the next line would start)

    const auto emit = [&] {
        while (!line.empty() && line.back().space) {
            line_width -= line.back().width;
            line.pop_back();
        }
        const Prefix* prefix = !prefixed ? nullptr : first_line ? &first : &rest;
        out.push_back(make_line(prefix, line, style));
        line.clear();
        line_width = 0;
        first_line = false;
        can_break = false;
    };

    for (const Atom& source : atoms) {
        if (source.newline) {
            emit();
            wrapped = false;
            continue;
        }
        Atom atom = source;
        if (atom.width > limit) {
            atom = {.text = "…", .marks = atom.marks, .width = 1};
        }
        if (atom.space && line.empty() && (wrapped || !style.keep_indent)) {
            continue;
        }
        if (line_width + atom.width > limit) {
            wrapped = true;
            if (atom.space) {
                emit();
                continue;
            }
            if (can_break) {
                std::vector<Atom> carry(line.begin() + static_cast<std::ptrdiff_t>(resume_at),
                                        line.end());
                line.resize(break_at);
                emit();
                line = std::move(carry);
                line_width = total_width(line);
            }
            // Hard split: no break opportunity, or the carried word still leaves no room.
            if (line_width + atom.width > limit) {
                emit();
            }
        }
        if (atom.space) {
            if (!line.empty() && !line.back().space) {
                can_break = true;
                break_at = line.size();
            }
            resume_at = line.size() + 1;
        }
        line.push_back(atom);
        line_width += atom.width;
    }
    if (!line.empty()) {
        emit();
    }
}

Prefix example_prefix(const Block& block) {
    Prefix prefix;
    if (block.in_example) {
        prefix.add(std::string(gutter), Tone::Gutter);
    }
    return prefix;
}

std::vector<Line> layout_block(const Block& block, int width) {
    std::vector<Line> lines;
    const Prefix lead = example_prefix(block);
    switch (block.kind) {
    case BlockKind::Paragraph: wrap(atomize(block.spans, false), width, lead, lead, lines); break;
    case BlockKind::Heading:
        wrap(atomize(block.spans, false), width, lead, lead, lines,
             {.tone = Tone::Heading, .marks = MarkBold});
        break;
    case BlockKind::ListItem: {
        Prefix first = lead;
        first.add(std::string(static_cast<std::size_t>(2 * (std::max(block.level, 1) - 1)), ' '),
                  Tone::Normal);
        first.add(block.marker, Tone::Bullet);
        first.add(" ", Tone::Normal);
        Prefix rest = lead;
        rest.add(std::string(static_cast<std::size_t>(first.width - lead.width), ' '),
                 Tone::Normal);
        wrap(atomize(block.spans, false), width, first, rest, lines);
        break;
    }
    case BlockKind::Preformatted: {
        Prefix bar;
        bar.add(std::string(gutter), Tone::Gutter);
        wrap(atomize(block.spans, true), width, bar, bar, lines,
             {.tone = Tone::CodeBlock, .code_block = true, .keep_indent = true});
        break;
    }
    case BlockKind::Rule:
    case BlockKind::Image: {
        Line line;
        int avail = width;
        if (lead.fits(width)) {
            line.spans = lead.spans;
            avail -= lead.width;
        }
        if (block.kind == BlockKind::Rule) {
            std::string rule;
            for (int i = 0; i < avail; ++i) {
                rule += "─";
            }
            line.spans.push_back({.text = std::move(rule), .tone = Tone::Rule});
        } else {
            std::string alt;
            for (const Span& span : block.spans) {
                alt += span.text;
            }
            const std::string label = "🖼 image: " + (block.src.empty() ? alt : block.src);
            line.spans.push_back({.text = unicode::truncate(label, avail), .tone = Tone::Image});
        }
        lines.push_back(std::move(line));
        break;
    }
    }
    return lines;
}

// Blocks are separated by a blank line, except items of a list (kept tight) and consecutive
// paragraphs of one example, which are joined by a bare gutter so the example reads as a unit.
std::vector<Line> separator(const Block& prev, const Block& next, int width) {
    if (prev.kind == BlockKind::ListItem && next.kind == BlockKind::ListItem) {
        return {};
    }
    const bool example = prev.in_example && next.in_example && prev.kind != BlockKind::Preformatted
                         && next.kind != BlockKind::Preformatted;
    const Prefix lead = example_prefix(next);
    if (example && lead.fits(width)) {
        return {Line{.spans = lead.spans}};
    }
    return {Line{}};
}

} // namespace

std::vector<Line> layout(const Document& doc, int width) {
    width = std::max(width, 1);
    std::vector<Line> lines;
    const Block* prev = nullptr;
    for (const Block& block : doc.blocks) {
        std::vector<Line> block_lines = layout_block(block, width);
        if (block_lines.empty()) {
            continue;
        }
        if (prev != nullptr) {
            std::ranges::move(separator(*prev, block, width), std::back_inserter(lines));
        }
        std::ranges::move(block_lines, std::back_inserter(lines));
        prev = &block;
    }

    // Line breaks inside blocks can still produce stray blank lines; drop leading, trailing
    // and repeated ones.
    std::vector<Line> out;
    out.reserve(lines.size());
    for (Line& line : lines) {
        if (is_blank(line) && (out.empty() || is_blank(out.back()))) {
            continue;
        }
        out.push_back(std::move(line));
    }
    while (!out.empty() && is_blank(out.back())) {
        out.pop_back();
    }
    return out;
}

std::string plain_text(const Line& line) {
    std::string out;
    for (const LineSpan& span : line.spans) {
        out += span.text;
    }
    return out;
}

} // namespace lv::content
