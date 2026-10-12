#include "content/html.hpp"
#include "content/layout.hpp"
#include "support.hpp"
#include "util/unicode.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace lv::content;
using lv::content::test::fixtures;
using lv::content::test::lspan;
using lv::content::test::paragraph;
using lv::content::test::read_fixture;
using lv::content::test::render;
using lv::content::test::span;

namespace {

bool is_blank(const Line& line) {
    return plain_text(line).find_first_not_of(' ') == std::string::npos;
}

// The invariants every layout must satisfy, whatever the input.
void check_invariants(const std::vector<Line>& lines, int width) {
    for (std::size_t i = 0; i < lines.size(); ++i) {
        CAPTURE(i);
        CAPTURE(plain_text(lines[i]));
        CHECK(lv::unicode::display_width(plain_text(lines[i])) <= std::max(width, 1));
        if (i > 0) {
            CHECK_FALSE((is_blank(lines[i]) && is_blank(lines[i - 1])));
        }
    }
    if (!lines.empty()) {
        CHECK_FALSE(is_blank(lines.front()));
        CHECK_FALSE(is_blank(lines.back()));
    }
}

Document doc_of(std::vector<Block> blocks) { return {.blocks = std::move(blocks)}; }

Block item(std::string text, std::string marker, int level) {
    return {
        .kind = BlockKind::ListItem,
        .spans = {span(std::move(text))},
        .level = level,
        .marker = std::move(marker),
    };
}

} // namespace

TEST_CASE("paragraphs wrap greedily at spaces") {
    const Document doc = doc_of({paragraph({span("aaa bbb ccc ddd")})});
    CHECK(render(layout(doc, 10)) == "aaa bbb\nccc ddd");
    CHECK(render(layout(doc, 15)) == "aaa bbb ccc ddd");
    CHECK(render(layout(doc, 11)) == "aaa bbb ccc\nddd");
}

TEST_CASE("words longer than the width are split") {
    CHECK(render(layout(doc_of({paragraph({span("abcdefghijklmnop")})}), 10))
          == "abcdefghij\nklmnop");
    CHECK(render(layout(doc_of({paragraph({span("see abcdefghijklmnop")})}), 10))
          == "see\nabcdefghij\nklmnop");
}

TEST_CASE("marks are kept per span, also within a word") {
    const Document doc =
        doc_of({paragraph({span("ab", MarkBold), span("c", MarkCode), span(" de")})});
    const auto wide = layout(doc, 80);
    REQUIRE(wide.size() == 1);
    CHECK(wide[0].spans
          == std::vector<LineSpan>{lspan("ab", MarkBold), lspan("c", MarkCode), lspan(" de")});

    const auto narrow = layout(doc, 3);
    REQUIRE(narrow.size() == 2);
    CHECK(narrow[0].spans == std::vector<LineSpan>{lspan("ab", MarkBold), lspan("c", MarkCode)});
    CHECK(narrow[1].spans == std::vector<LineSpan>{lspan("de")});
}

TEST_CASE("line breaks force a new line") {
    const Document doc = parse_html("<p>one<br>two three<br><br>four</p>");
    CHECK(render(layout(doc, 80)) == "one\ntwo three\n\nfour");
}

TEST_CASE("headings are bold with the heading tone") {
    const Document doc = doc_of(
        {{.kind = BlockKind::Heading, .spans = {span("Title "), span("x", MarkCode)}, .level = 1}});
    const auto lines = layout(doc, 40);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].spans
          == std::vector<LineSpan>{lspan("Title ", MarkBold, Tone::Heading),
                                   lspan("x", MarkBold | MarkCode, Tone::Heading)});
}

TEST_CASE("list items: markers, nesting and hanging indent") {
    const Document doc = doc_of({
        paragraph({span("Intro")}),
        item("first item text that wraps", "•", 1),
        item("nested", "•", 2),
        item("x", "10.", 1),
        item("continued", "   ", 1),
        paragraph({span("After")}),
    });
    const auto lines = layout(doc, 20);
    CHECK(render(lines)
          == "Intro\n"
             "\n"
             "• first item text\n"
             "  that wraps\n"
             "  • nested\n"
             "10. x\n"
             "    continued\n"
             "\n"
             "After");
    REQUIRE(lines.size() == 9);
    CHECK(lines[2].spans
          == std::vector<LineSpan>{lspan("•", MarkNone, Tone::Bullet), lspan(" "),
                                   lspan("first item text")});
    CHECK(lines[3].spans.front() == lspan("  "));
    CHECK(lines[4].spans[1] == lspan("•", MarkNone, Tone::Bullet));
}

TEST_CASE("preformatted blocks get a gutter and keep their spacing") {
    const Document doc = doc_of({
        {
            .kind = BlockKind::Preformatted,
            .spans = {span("Input:", MarkBold), span(" x = 1\n\tindented\n\nlast")},
            .in_example = true,
        },
    });
    const auto lines = layout(doc, 40);
    CHECK(render(lines) == "▎ Input: x = 1\n▎     indented\n▎ \n▎ last");
    REQUIRE(lines.size() == 4);
    for (const Line& line : lines) {
        CHECK(line.code_block);
        CHECK(line.spans.front() == lspan("▎ ", MarkNone, Tone::Gutter));
    }
    CHECK(lines[0].spans
          == std::vector<LineSpan>{lspan("▎ ", MarkNone, Tone::Gutter),
                                   lspan("Input:", MarkBold, Tone::CodeBlock),
                                   lspan(" x = 1", MarkNone, Tone::CodeBlock)});

    // Long lines wrap; continuation lines keep the gutter and drop the break's spaces.
    const Document wide =
        doc_of({{.kind = BlockKind::Preformatted, .spans = {span("  abcdefghij klmnop")}}});
    CHECK(render(layout(wide, 10)) == "▎   abcdef\n▎ ghij\n▎ klmnop");
}

TEST_CASE("example blocks get a continuous gutter") {
    const Document doc = parse_html(R"(
        <p><strong>Example 1:</strong></p>
        <div class="example-block">
        <p><strong>Input:</strong> <span class="example-io">n = 16</span></p>
        <p><strong>Output:</strong> <span class="example-io">true</span></p>
        </div>
        <p>After</p>)");
    const auto lines = layout(doc, 40);
    CHECK(render(lines) == "Example 1:\n\n▎ Input: n = 16\n▎ \n▎ Output: true\n\nAfter");
    REQUIRE(lines.size() == 7);
    CHECK(lines[3].spans == std::vector<LineSpan>{lspan("▎ ", MarkNone, Tone::Gutter)});
    CHECK_FALSE(lines[3].code_block);
    CHECK(lines[2].spans[1] == lspan("Input:", MarkBold));

    // Wrapped example lines keep the gutter.
    for (const Line& line : layout(doc, 12)) {
        const std::string text = plain_text(line);
        if (!text.empty() && text != "Example 1:" && text != "After") {
            CHECK(text.starts_with("▎ "));
        }
    }
}

TEST_CASE("rules span the width and images are truncated") {
    const Document doc = doc_of({
        {.kind = BlockKind::Rule},
        {.kind = BlockKind::Image, .src = "https://assets.leetcode.com/uploads/2020/10/03/x.jpg"},
    });
    const auto lines = layout(doc, 20);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].spans
          == std::vector<LineSpan>{lspan("────────────────────", MarkNone, Tone::Rule)});
    REQUIRE(lines[2].spans.size() == 1);
    CHECK(lines[2].spans[0].tone == Tone::Image);
    CHECK(plain_text(lines[2]).starts_with("🖼 image: https://"));
    CHECK(lv::unicode::display_width(plain_text(lines[2])) == 20);
    CHECK(plain_text(layout(doc, 80)[2])
          == "🖼 image: https://assets.leetcode.com/uploads/2020/10/03/x.jpg");
}

TEST_CASE("blocks are separated by exactly one blank line") {
    const Document doc = doc_of({
        paragraph({span("\n\na")}),
        paragraph({}),
        paragraph({span("b\n\n\n\nc\n")}),
        item("x", "•", 1),
        item("y", "•", 1),
        paragraph({span("d\n")}),
    });
    CHECK(render(layout(doc, 40)) == "a\n\nb\n\nc\n\n• x\n• y\n\nd");
    CHECK(layout(Document{}, 40).empty());
}

TEST_CASE("wide characters and emoji are measured by display width") {
    const std::string text = "中文字符测试 emoji 😀😀😀 👨‍👩‍👧 done";
    const Document doc = doc_of({paragraph({span(text)})});
    const auto lines = layout(doc, 5);
    check_invariants(lines, 5);
    CHECK(render(lines)
          == "中文\n字符\n测试\nemoji\n😀😀\n😀 👨‍👩‍👧\ndone");
}

TEST_CASE("tiny widths neither crash nor overflow") {
    const Document synthetic = doc_of({
        {.kind = BlockKind::Heading, .spans = {span("A heading")}, .level = 2},
        item("deeply nested item with words", "10.", 4),
        {.kind = BlockKind::Paragraph, .spans = {span("中文 example")}, .in_example = true},
        {.kind = BlockKind::Preformatted, .spans = {span("\t中x\n\n  y")}},
        {.kind = BlockKind::Rule, .in_example = true},
        {.kind = BlockKind::Image, .src = "u"},
    });
    for (int width = -1; width <= 9; ++width) {
        CAPTURE(width);
        check_invariants(layout(synthetic, width), width);
        for (const char* name : fixtures) {
            CAPTURE(name);
            check_invariants(layout(parse_html(read_fixture(name)), width), width);
        }
    }
}

TEST_CASE("fixtures lay out within the width at common sizes") {
    for (const char* name : fixtures) {
        const Document doc = parse_html(read_fixture(name));
        for (const int width : {20, 40, 80, 120}) {
            CAPTURE(name);
            CAPTURE(width);
            const auto lines = layout(doc, width);
            REQUIRE_FALSE(lines.empty());
            check_invariants(lines, width);
            for (const Line& line : lines) {
                if (line.code_block) {
                    CHECK(line.spans.front().tone == Tone::Gutter);
                }
            }
        }
    }
}

TEST_CASE("fixture layout reads naturally") {
    const auto lines = layout(parse_html(read_fixture("median-of-two-sorted-arrays.html")), 60);
    const std::string text = render(lines);
    CHECK(text.starts_with("Given two sorted arrays nums1 and nums2 of size m and n\n"
                           "respectively, return the median of the two sorted arrays.\n"
                           "\n"
                           "The overall run time complexity should be O(log (m+n)).\n"
                           "\n"
                           "Example 1:\n"
                           "\n"
                           "▎ Input: nums1 = [1,3], nums2 = [2]\n"
                           "▎ Output: 2.00000\n"
                           "▎ Explanation: merged array = [1,2,3] and median is 2.\n"));
    CHECK(text.ends_with("Constraints:\n"
                         "\n"
                         "• nums1.length == m\n"
                         "• nums2.length == n\n"
                         "• 0 <= m <= 1000\n"
                         "• 0 <= n <= 1000\n"
                         "• 1 <= m + n <= 2000\n"
                         "• -10⁶ <= nums1[i], nums2[i] <= 10⁶"));
}

TEST_CASE("plain_text concatenates spans") {
    CHECK(plain_text(
              Line{.spans = {lspan("a"), lspan("b", MarkBold), lspan("c", MarkNone, Tone::Rule)}})
          == "abc");
    CHECK(plain_text(Line{}).empty());
}
