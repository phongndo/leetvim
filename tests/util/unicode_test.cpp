#include "util/fuzzy.hpp"
#include "util/strings.hpp"
#include "util/unicode.hpp"

#include <doctest/doctest.h>

using namespace lv;

TEST_CASE("utf8 decode and encode round trip") {
    const std::string s = "a€😀中";
    std::size_t pos = 0;
    CHECK(unicode::decode(s, pos) == U'a');
    CHECK(unicode::decode(s, pos) == U'€');
    CHECK(unicode::decode(s, pos) == U'😀');
    CHECK(unicode::decode(s, pos) == U'中');
    CHECK(pos == s.size());
    CHECK(unicode::to_utf8(U'😀') == "😀");
}

TEST_CASE("malformed utf8 yields replacement and advances") {
    const std::string s = "\xff"
                          "a\xe2\x82";
    std::size_t pos = 0;
    CHECK(unicode::decode(s, pos) == unicode::replacement_char);
    CHECK(unicode::decode(s, pos) == U'a');
    CHECK(unicode::decode(s, pos) == unicode::replacement_char);
    CHECK(unicode::decode(s, pos) == unicode::replacement_char);
    CHECK(pos == s.size());
}

TEST_CASE("display width") {
    CHECK(unicode::display_width("hello") == 5);
    CHECK(unicode::display_width("中文") == 4);
    CHECK(unicode::display_width("é") == 1);
    CHECK(unicode::display_width("😀") == 2);
    CHECK(unicode::display_width("👍🏽") == 2);
    CHECK(unicode::display_width("👨‍👩‍👧") == 2);
    CHECK(unicode::display_width("❤️‍🔥") == 2);
    CHECK(unicode::display_width("🇺🇸") == 2);
    // A joiner that follows no emoji joins nothing (found by fuzz/unicode.cpp).
    CHECK(unicode::display_width(" ‍") == 1);
    CHECK(unicode::display_width("a‍b") == 2);
    CHECK(unicode::display_width(unicode::pad_left("‍", 48)) == 48);
    // Likewise VS16 only widens characters that have an emoji form.
    CHECK(unicode::display_width("❤️") == 2);
    CHECK(unicode::display_width("1️⃣") == 2);
    CHECK(unicode::display_width(" ️") == 1);
    CHECK(unicode::display_width("️") == 0);
    CHECK(unicode::display_width("╭─╮■≤") == 5);
}

TEST_CASE("truncate and pad") {
    CHECK(unicode::truncate("abcdef", 4) == "abc…");
    CHECK(unicode::truncate("abc", 4) == "abc");
    CHECK(unicode::truncate("中文字", 5) == "中文…");
    CHECK(unicode::pad_right("ab", 4) == "ab  ");
    CHECK(unicode::pad_left("ab", 4) == "  ab");
    CHECK(unicode::pad_right("abcdef", 3) == "ab…");
}

TEST_CASE("string helpers") {
    CHECK(str::trim("  x y \n") == "x y");
    CHECK(str::split("a,b,,c", ',').size() == 4);
    CHECK(str::lines("a\r\nb\n").size() == 3);
    CHECK(str::icontains("Two Sum", "sum"));
    CHECK(str::replace_all("a-b-c", "-", "+") == "a+b+c");
    CHECK(str::human_count(23771382) == "23.8M");
    CHECK(str::human_count(950) == "950");
}

TEST_CASE("fuzzy matching prefers compact word-start matches") {
    CHECK_FALSE(fuzzy::match("xyz", "two sum").has_value());
    CHECK(fuzzy::match("", "anything") == 0);
    const auto exact = fuzzy::match("two", "two sum");
    const auto scattered = fuzzy::match("two", "the world of tea");
    REQUIRE(exact.has_value());
    REQUIRE(scattered.has_value());
    CHECK(*exact > *scattered);
    std::vector<std::size_t> pos;
    REQUIRE(fuzzy::match("ts", "Two Sum", &pos).has_value());
    CHECK(pos == std::vector<std::size_t>{0, 4});
}
