#include "content/html.hpp"
#include "support.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using namespace lv::content;
using lv::content::test::read_fixture;
using lv::content::test::span;
using lv::content::test::text_of;

namespace {

std::string first_text(std::string_view html) {
    const Document doc = parse_html(html);
    return doc.blocks.empty() ? "<no blocks>" : text_of(doc.blocks.front());
}

} // namespace

TEST_CASE("decode_entities handles named and numeric references") {
    CHECK(decode_entities("a &lt;= b &amp;&amp; c &ge; d") == "a <= b && c ≥ d");
    CHECK(decode_entities("&quot;x&quot; &#39;y&#39;") == "\"x\" 'y'");
    CHECK(decode_entities("&le;&ne;&times;&rarr;&hellip;&minus;") == "≤≠×→…−");
    CHECK(decode_entities("&#x41;&#66;&#x1F600;&#65") == "AB😀A");
    CHECK(decode_entities("&nbsp;&#160;") == "  ");
    CHECK(decode_entities("&amp;lt;") == "&lt;");
    CHECK(decode_entities("AT&T &unknown; & &#; &") == "AT&T &unknown; & &#; &");
    CHECK(decode_entities("&#0;&#x110000;&#99999999999;") == "���");
}

TEST_CASE("inline whitespace collapses and trims at block boundaries") {
    const Document doc = parse_html("<p>  Hello \n\t  <b>big</b>\n world  </p>");
    REQUIRE(doc.blocks.size() == 1);
    CHECK(doc.blocks[0].kind == BlockKind::Paragraph);
    CHECK(doc.blocks[0].spans
          == std::vector<Span>{span("Hello "), span("big", MarkBold), span(" world")});
}

TEST_CASE("text outside any block becomes a paragraph") {
    const Document doc = parse_html("<p>a</p>\n<strong>Follow-up:&nbsp;</strong>Can you?");
    REQUIRE(doc.blocks.size() == 2);
    CHECK(doc.blocks[1].spans
          == std::vector<Span>{span("Follow-up:", MarkBold), span(" Can you?")});
}

TEST_CASE("empty and nbsp-only paragraphs are dropped") {
    const Document doc =
        parse_html("<p>a</p><p>&nbsp;</p>\n<p> </p><p><br></p><div></div><p>b</p>");
    REQUIRE(doc.blocks.size() == 2);
    CHECK(text_of(doc.blocks[0]) == "a");
    CHECK(text_of(doc.blocks[1]) == "b");
    CHECK(parse_html("").blocks.empty());
    CHECK(parse_html("<p>&nbsp;</p>").blocks.empty());
}

TEST_CASE("marks nest, combine and merge") {
    const Document doc =
        parse_html("<p><strong>bold <em>both</em></strong> <code>x</code> <u>u</u> <s>s</s> "
                   "<a href=\"https://x.y/?a=1&amp;b=2\" target=_blank>link <b>b</b></a></p>");
    REQUIRE(doc.blocks.size() == 1);
    const std::string href = "https://x.y/?a=1&b=2";
    CHECK(doc.blocks[0].spans
          == std::vector<Span>{
              span("bold ", MarkBold),
              span("both", MarkBold | MarkItalic),
              span(" "),
              span("x", MarkCode),
              span(" "),
              span("u", MarkUnderline),
              span(" "),
              span("s", MarkStrike),
              span(" "),
              span("link ", MarkLink, href),
              span("b", MarkLink | MarkBold, href),
          });

    CHECK(parse_html("<p><b>a</b><b>b</b><strong>c</strong></p>").blocks[0].spans
          == std::vector<Span>{span("abc", MarkBold)});
    // Whitespace between two code spans is not code; whitespace inside one is.
    CHECK(parse_html("<p><code>a</code> <code>b</code></p>").blocks[0].spans
          == std::vector<Span>{span("a", MarkCode), span(" "), span("b", MarkCode)});
    CHECK(parse_html("<p><code>a  b</code></p>").blocks[0].spans
          == std::vector<Span>{span("a b", MarkCode)});
    // An anchor without href is not a link.
    CHECK(parse_html("<p><a name=\"x\">plain</a></p>").blocks[0].spans
          == std::vector<Span>{span("plain")});
}

TEST_CASE("sup and sub become Unicode scripts when possible") {
    CHECK(first_text("<p>10<sup>9</sup> + 7</p>") == "10⁹ + 7");
    CHECK(first_text("<p>n<sup>2</sup></p>") == "n²");
    CHECK(first_text("<p>2<sup>31</sup> - 1</p>") == "2³¹ - 1");
    CHECK(first_text("<p>[-2<sup>31</sup>, 2<sup> 31 </sup> - 1]</p>") == "[-2³¹, 2³¹ - 1]");
    CHECK(first_text("<p>x<sup>n-1</sup> 10<sup>-5</sup></p>") == "xⁿ⁻¹ 10⁻⁵");
    CHECK(first_text("<p>x<sup>q</sup></p>") == "x^q");
    CHECK(first_text("<p>2<sup>n/2</sup></p>") == "2^(n/2)");
    CHECK(first_text("<p>2<sup>a b</sup></p>") == "2^(a b)");
    CHECK(first_text("<p>a<sub>i</sub> + a<sub>10</sub></p>") == "aᵢ + a₁₀");
    CHECK(first_text("<p>a<sub>i,j</sub> x<sub>Q</sub></p>") == "a_(i,j) x_Q");
    CHECK(first_text("<p>x<sup></sup>y</p>") == "xy");
    // Marks around the script apply to the converted text.
    CHECK(parse_html("<p><code>0 &lt;= key &lt;= 10<sup>4</sup></code></p>").blocks[0].spans
          == std::vector<Span>{span("0 <= key <= 10⁴", MarkCode)});
    // An unclosed <sup> ends with its block.
    const Document doc = parse_html("<p>2<sup>10</p><p>next</p>");
    REQUIRE(doc.blocks.size() == 2);
    CHECK(text_of(doc.blocks[0]) == "2¹⁰");
    CHECK(text_of(doc.blocks[1]) == "next");
}

TEST_CASE("lists: markers, nesting, start attribute and folded paragraphs") {
    const Document doc = parse_html(R"(
        <ul>
            <li>a</li>
            <li>b <ul><li>c</li></ul></li>
        </ul>
        <ol start="3">
            <li>x</li>
            <li><p>y</p>
                <p>z</p></li>
            <li>u<ol><li>v</li></ol>tail</li>
        </ol>)");
    struct Expected {
        std::string text;
        std::string marker;
        int level;
    };
    const std::vector<Expected> expected{
        {"a", "•", 1},    {"b", "•", 1},  {"c", "•", 2},  {"x", "3.", 1},
        {"y z", "4.", 1}, {"u", "5.", 1}, {"v", "1.", 2}, {"tail", "  ", 1},
    };
    REQUIRE(doc.blocks.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        CHECK(doc.blocks[i].kind == BlockKind::ListItem);
        CHECK(text_of(doc.blocks[i]) == expected[i].text);
        CHECK(doc.blocks[i].marker == expected[i].marker);
        CHECK(doc.blocks[i].level == expected[i].level);
    }
}

TEST_CASE("pre keeps spacing, newlines and inline marks") {
    const Document doc = parse_html("<pre>\n<strong>Input:</strong> nums = [2,7]\n"
                                    "  a &lt; b&nbsp;&nbsp;c\n\n\tx\n  \n</pre>");
    REQUIRE(doc.blocks.size() == 1);
    const Block& pre = doc.blocks[0];
    CHECK(pre.kind == BlockKind::Preformatted);
    CHECK(pre.in_example);
    CHECK(pre.spans
          == std::vector<Span>{span("Input:", MarkBold), span(" nums = [2,7]\n  a < b  c\n\n\tx")});
    // Only a single leading newline is dropped.
    CHECK(first_text("<pre>\n\n  x</pre>") == "\n  x");
    CHECK(parse_html("<pre>\n   \n</pre>").blocks.empty());
}

TEST_CASE("example blocks mark their paragraphs") {
    const Document doc = parse_html(R"(
        <p><strong class="example">Example 1:</strong></p>
        <div class="example-block">
        <p><strong>Input:</strong> <span class="example-io">n = 16</span></p>
        <p><strong>Explanation:</strong></p>
        </div>
        <p>After</p>)");
    REQUIRE(doc.blocks.size() == 4);
    CHECK_FALSE(doc.blocks[0].in_example);
    CHECK(doc.blocks[1].in_example);
    CHECK(doc.blocks[1].spans == std::vector<Span>{span("Input:", MarkBold), span(" n = 16")});
    CHECK(doc.blocks[2].in_example);
    CHECK_FALSE(doc.blocks[3].in_example);
}

TEST_CASE("br is a line break inside a paragraph") {
    CHECK(first_text("<p>a<br>b <br/> c<br><br></p>") == "a\nb\nc");
    CHECK(first_text("<p><br>a</p>") == "a");
}

TEST_CASE("headings, rules, images and tables") {
    const Document doc =
        parse_html("<h2>Title <code>x</code></h2><hr><p>a<img alt=\"pic\" src=\"a.png\" />b</p>"
                   "<table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td> 2 </td></tr></table>");
    REQUIRE(doc.blocks.size() == 7);
    CHECK(doc.blocks[0].kind == BlockKind::Heading);
    CHECK(doc.blocks[0].level == 2);
    CHECK(doc.blocks[0].spans == std::vector<Span>{span("Title "), span("x", MarkCode)});
    CHECK(doc.blocks[1].kind == BlockKind::Rule);
    CHECK(text_of(doc.blocks[2]) == "a");
    CHECK(doc.blocks[3].kind == BlockKind::Image);
    CHECK(doc.blocks[3].src == "a.png");
    CHECK(text_of(doc.blocks[3]) == "pic");
    CHECK(text_of(doc.blocks[4]) == "b");
    CHECK(doc.blocks[5].spans
          == std::vector<Span>{span("A", MarkBold), span(" | "), span("B", MarkBold)});
    CHECK(doc.blocks[6].spans == std::vector<Span>{span("1 | 2")});
}

TEST_CASE("malformed html is tolerated") {
    // Unclosed inline tags end with their block.
    const Document leak = parse_html("<p>one <b>two</p><p>three</p><ul><li><code>x</li><li>y</ul>");
    REQUIRE(leak.blocks.size() == 4);
    CHECK(leak.blocks[1].spans == std::vector<Span>{span("three")});
    CHECK(leak.blocks[3].spans == std::vector<Span>{span("y")});

    CHECK(first_text("<p>a < b and c<5 </p>") == "a < b and c<5");
    CHECK(first_text("<p>unterminated <b") == "unterminated <b");
    CHECK(first_text("x<!-- comment") == "x");
    CHECK(first_text("<!DOCTYPE html><p>a<!-- <p>hidden</p> -->b</p>") == "ab");
    CHECK(first_text("</div></li></b><p>ok</p></span>") == "ok");
    CHECK(first_text("<a href=\"unterminated>text</a> more") == "text more");
    CHECK(first_text("<p>a<script>if (a<b) x();</script>b<style>p{}</style></p>") == "ab");
    CHECK(first_text("<P CLASS=x>Upper</P>") == "Upper");

    const Document implied = parse_html("<p>a<p>b<div><p>c</div>d");
    REQUIRE(implied.blocks.size() == 4);
    CHECK(text_of(implied.blocks[3]) == "d");

    for (const std::string_view junk : {
             "<",
             "<<<>>>&&&;;</</>< /p>",
             "<p",
             "</",
             "&",
             "<pre>",
             "<ul><li>",
             "<sup>",
             "<table><td>x",
             "<a href='x",
         }) {
        CAPTURE(junk);
        CHECK_NOTHROW((void)parse_html(junk));
    }
}

TEST_CASE("fixtures parse into the expected structure") {
    const Document fib = parse_html(read_fixture("three-fibonacci-sum.html"));
    REQUIRE_FALSE(fib.blocks.empty());
    CHECK(fib.blocks[0].spans
          == std::vector<Span>{span("You are given an integer "), span("n", MarkCode), span(".")});
    CHECK(std::ranges::count_if(fib.blocks, [](const Block& b) { return b.in_example; }) == 12);
    CHECK(fib.blocks.back().kind == BlockKind::ListItem);
    CHECK(fib.blocks.back().spans == std::vector<Span>{span("1 <= n <= 10⁹", MarkCode)});

    const Document lru = parse_html(read_fixture("lru-cache.html"));
    const auto pre = std::ranges::find(lru.blocks, BlockKind::Preformatted, &Block::kind);
    REQUIRE(pre != lru.blocks.end());
    CHECK(pre->spans.front() == span("Input", MarkBold));
    CHECK(text_of(*pre).find("lRUCache.put(1, 1); // cache is {1=1}\n") != std::string::npos);
    CHECK(text_of(lru.blocks[0]).starts_with("Design a data structure"));
    CHECK(lru.blocks[0].spans[1].marks == (MarkBold | MarkLink));

    const Document sql = parse_html(read_fixture("combine-two-tables.html"));
    REQUIRE(sql.blocks.size() >= 2);
    CHECK(
        text_of(sql.blocks[1]).starts_with("+-------------+---------+\n| Column Name | Type    |"));

    const Document median = parse_html(read_fixture("median-of-two-sorted-arrays.html"));
    CHECK(text_of(median.blocks.back()) == "-10⁶ <= nums1[i], nums2[i] <= 10⁶");
}
