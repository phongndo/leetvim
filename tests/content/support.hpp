#pragma once

#include "content/document.hpp"
#include "content/layout.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lv::content::test {

inline constexpr std::array fixtures{
    "three-fibonacci-sum.html",
    "combine-two-tables.html",
    "lru-cache.html",
    "median-of-two-sorted-arrays.html",
};

inline std::string read_fixture(std::string_view name) {
    const std::ifstream in(std::string(LEETVIM_FIXTURES) + "/" + std::string(name));
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

inline std::string text_of(const Block& block) {
    std::string out;
    for (const Span& span : block.spans) out += span.text;
    return out;
}

/// Lines' plain text joined with '\n': compact to compare and readable when a check fails.
inline std::string render(const std::vector<Line>& lines) {
    std::string out;
    for (const Line& line : lines) {
        if (&line != lines.data()) out += '\n';
        out += plain_text(line);
    }
    return out;
}

inline Span span(std::string text, std::uint8_t marks = MarkNone, std::string href = {}) {
    return {.text = std::move(text), .marks = marks, .href = std::move(href)};
}

inline LineSpan lspan(std::string text, std::uint8_t marks = MarkNone, Tone tone = Tone::Normal) {
    return {.text = std::move(text), .marks = marks, .tone = tone};
}

inline Block paragraph(std::vector<Span> spans) { return {.spans = std::move(spans)}; }

} // namespace lv::content::test
