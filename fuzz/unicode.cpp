// Text measuring and fitting, used for every cell leetvim draws, on arbitrary bytes.
#include "util/unicode.hpp"

#include "require.hpp"
#include "util/fuzzy.hpp"

#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    namespace u = lv::unicode;
    FuzzedDataProvider fdp(data, size);
    const int width = fdp.ConsumeIntegralInRange(0, 120);
    const std::string pattern = fdp.ConsumeRandomLengthString(32);
    const std::string s = fdp.ConsumeRemainingBytesAsString();

    for (std::size_t pos = 0; pos < s.size();) {
        const std::size_t before = pos;
        (void)u::decode(s, pos);
        require(pos > before, "decode() advances");
    }
    for (std::size_t pos = 0; pos < s.size();) {
        const std::size_t before = pos;
        (void)u::next_grapheme(s, pos);
        require(pos > before, "next_grapheme() advances");
    }
    // Measuring is compositional at a space: what follows can't change the space's width.
    require(u::display_width(" " + s) == 1 + u::display_width(s),
            "a leading space adds one column");
    require(u::display_width(u::truncate(s, width)) <= width, "truncate() fits the width");
    require(u::display_width(u::pad_right(s, width)) == width, "pad_right() is exactly the width");
    require(u::display_width(u::pad_left(s, width)) == width, "pad_left() is exactly the width");

    std::vector<std::size_t> positions;
    if (lv::fuzzy::match(pattern, s, &positions)) {
        for (std::size_t i = 0; i < positions.size(); ++i) {
            require(positions[i] < s.size() && (i == 0 || positions[i] > positions[i - 1]),
                    "fuzzy::match() positions are increasing offsets into the text");
        }
    }
    return 0;
}
