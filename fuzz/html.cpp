// Problem descriptions are HTML from LeetCode: parse, lay out and convert arbitrary input.
#include "content/html.hpp"

#include "content/layout.hpp"
#include "content/markdown.hpp"
#include "require.hpp"
#include "util/unicode.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view html(reinterpret_cast<const char*>(data), size);
    const auto doc = lv::content::parse_html(html);
    for (const int width : {16, 41, 97}) {
        for (const auto& line : lv::content::layout(doc, width)) {
            // layout() promises that every line fits, gutters and indentation included.
            require(lv::unicode::display_width(lv::content::plain_text(line)) <= width,
                    "layout() lines fit the width");
        }
    }
    (void)lv::content::to_markdown(doc);
    (void)lv::content::decode_entities(html);
    return 0;
}
