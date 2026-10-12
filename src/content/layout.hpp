#pragma once

#include "content/document.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lv::content {

/// What a laid-out span represents; the UI maps tones (plus marks) to theme colors.
enum class Tone : std::uint8_t {
    Normal,
    Heading,
    CodeBlock, // text inside <pre>
    Gutter,    // the bar drawn left of examples and code blocks
    Bullet,    // list markers
    Rule,      // horizontal rule characters
    Image,     // image placeholder text
};

struct LineSpan {
    std::string text;
    std::uint8_t marks = MarkNone;
    Tone tone = Tone::Normal;

    bool operator==(const LineSpan&) const = default;
};

struct Line {
    std::vector<LineSpan> spans;
    bool code_block = false; // line belongs to a <pre> block

    bool operator==(const Line&) const = default;
};

/// Word-wraps a Document to `width` terminal columns. Every returned line is at most
/// `width` columns wide (Unicode-aware), indentation and gutters included.
/// Blocks are separated by single blank lines.
std::vector<Line> layout(const Document& doc, int width);

/// Plain text of a line (concatenated spans), handy for search and tests.
std::string plain_text(const Line& line);

} // namespace lv::content
