#include "content/html.hpp"
#include "content/markdown.hpp"
#include "support.hpp"

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

using namespace lv::content;
using lv::content::test::paragraph;
using lv::content::test::read_fixture;
using lv::content::test::span;

namespace {

std::string md(std::string_view html) { return to_markdown(parse_html(html)); }

std::string md_text(std::string text) {
    return to_markdown({.blocks = {paragraph({span(std::move(text))})}});
}

} // namespace

TEST_CASE("inline marks") {
    CHECK(md("<p>Return <strong>the median</strong> of <em>two</em> <code>nums</code>, "
             "<s>old</s> and <a href=\"https://x.y\">a link</a>.</p>")
          == "Return **the median** of *two* `nums`, ~~old~~ and [a link](https://x.y).\n");
    CHECK(md("<p><strong>a <em>b</em></strong> c</p>") == "**a *b*** c\n");
    CHECK(md("<p><strong>Follow-up:&nbsp;</strong>Can you?</p>") == "**Follow-up:** Can you?\n");
    CHECK(md("<p><strong><a href=\"u\">x</a></strong></p>") == "[**x**](u)\n");
    CHECK(md("<p><a href=\"a b\">x</a></p>") == "[x](<a b>)\n");
    CHECK(md("<p><u>under</u></p>") == "under\n");
}

TEST_CASE("code spans pick a safe delimiter") {
    const auto code = [](std::string text) {
        return to_markdown({.blocks = {paragraph({span(std::move(text), MarkCode)})}});
    };
    CHECK(code("a`b") == "``a`b``\n");
    CHECK(code("`x") == "`` `x ``\n");
    CHECK(code("*not emphasis* <b>") == "`*not emphasis* <b>`\n");
}

TEST_CASE("text is escaped only where Markdown would misread it") {
    CHECK(md_text("nums[i] * 2 and 2 - 1 = 1, snake_case & a > b")
          == "nums[i] * 2 and 2 - 1 = 1, snake_case & a > b\n");
    CHECK(md_text("a*b _x_ `t` \\ List<Integer> &amp; f[x](y) ~~")
          == "a\\*b \\_x\\_ \\`t\\` \\\\ List\\<Integer> \\&amp; f[x\\](y) \\~\\~\n");
    CHECK(md_text("# not a heading") == "\\# not a heading\n");
    CHECK(md_text("#hashtag") == "#hashtag\n");
    CHECK(md_text("1. not a list") == "1\\. not a list\n");
    CHECK(md_text("- not a list") == "\\- not a list\n");
    CHECK(md_text("-1 is fine") == "-1 is fine\n");
    CHECK(md_text("> not a quote") == "\\> not a quote\n");
}

TEST_CASE("block structure") {
    CHECK(md("<h3>Title</h3><p>a</p><hr><img alt=\"pic [1]\" src=\"https://a.b/c.png\">")
          == "### Title\n\na\n\n---\n\n![pic \\[1\\]](https://a.b/c.png)\n");
    CHECK(md("<p>a<br>b</p><ul><li>c<br>d</li></ul>") == "a\\\nb\n\n- c\\\n  d\n");
    CHECK(to_markdown({}).empty());
}

TEST_CASE("lists nest under their parent's content column") {
    CHECK(md("<ul><li>a<ul><li>b</li></ul></li><li>c</li></ul><p>p</p>"
             "<ol start=\"9\"><li>x<ul><li>y</li></ul>more</li><li>z</li></ol>")
          == "- a\n  - b\n- c\n\np\n\n9. x\n   - y\n\n   more\n10. z\n");
}

TEST_CASE("pre becomes a fenced block of plain text") {
    CHECK(md("<pre>\n<strong>Input:</strong> x = 1\n<strong>Output:</strong> 2\n</pre>")
          == "```\nInput: x = 1\nOutput: 2\n```\n");
    CHECK(md("<pre>a\n```\nb</pre>") == "````\na\n```\nb\n````\n");
}

TEST_CASE("fixture markdown") {
    const std::string lru = to_markdown(parse_html(read_fixture("lru-cache.html")));
    CHECK(lru.starts_with(
        "Design a data structure that follows the constraints of a "
        "[**Least Recently Used (LRU) cache**]"
        "(https://en.wikipedia.org/wiki/Cache_replacement_policies#LRU).\n\n"
        "Implement the `LRUCache` class:\n\n"
        "- `LRUCache(int capacity)` Initialize the LRU cache with **positive** size "
        "`capacity`.\n"
        "- `int get(int key)` Return the value"));
    CHECK(lru.find("**Example 1:**\n\n```\nInput\n[\"LRUCache\", \"put\"") != std::string::npos);
    CHECK(lru.find("lRUCache.get(4);    // return 4\n```\n\n**Constraints:**\n\n")
          != std::string::npos);
    CHECK(lru.ends_with("- `0 <= key <= 10⁴`\n- `0 <= value <= 10⁵`\n"
                        "- At most `2 * 10⁵` calls will be made to `get` and `put`.\n"));
    CHECK(lru.find("&quot;") == std::string::npos);
    CHECK(lru.find("\n\n\n") == std::string::npos);

    const std::string fib = to_markdown(parse_html(read_fixture("three-fibonacci-sum.html")));
    CHECK(fib.find("**Example 1:**\n\n**Input:** n = 16\n\n**Output:** true\n\n"
                   "**Explanation:**\n\nThe three consecutive terms 3, 5, and 8 have a sum of "
                   "`3 + 5 + 8 = 16`.\n")
          != std::string::npos);
    CHECK(fib.find("Its first few terms are `0, 1, 1, 2, 3, 5, 8, 13, ...`.") != std::string::npos);
}
