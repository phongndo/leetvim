#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A small document model for LeetCode problem statements. LeetCode serves problem
// content as HTML; it is parsed once into this model and then rendered either as
// styled terminal lines (layout.hpp) or as Markdown (markdown.hpp).
namespace lv::content {

/// Inline formatting flags; combine with bitwise or (hence not an enum class).
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum Mark : std::uint8_t {
    MarkNone = 0,
    MarkBold = 1U << 0U,
    MarkItalic = 1U << 1U,
    MarkCode = 1U << 2U,
    MarkUnderline = 1U << 3U,
    MarkStrike = 1U << 4U,
    MarkLink = 1U << 5U,
};

struct Span {
    std::string text;
    std::uint8_t marks = MarkNone;
    std::string href; // set when marks has MarkLink

    bool operator==(const Span&) const = default;
};

enum class BlockKind : std::uint8_t {
    Paragraph,
    Heading,
    Preformatted, // text keeps its newlines and spacing
    ListItem,
    Rule,
    Image,
};

struct Block {
    BlockKind kind = BlockKind::Paragraph;
    std::vector<Span> spans;
    int level = 0;           // Heading: 1-6. ListItem: nesting depth starting at 1.
    std::string marker;      // ListItem: "•" or "3."
    bool in_example = false; // inside an example (<div class="example-block"> or <pre>)
    std::string src;         // Image: source URL

    bool operator==(const Block&) const = default;
};

struct Document {
    std::vector<Block> blocks;
};

} // namespace lv::content
