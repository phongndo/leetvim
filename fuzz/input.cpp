// Terminal input: keys, mouse reports, pastes and focus events, arriving in arbitrary chunks.
#include "tui/input.hpp"

#include "require.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return 0;
    const std::string_view bytes(reinterpret_cast<const char*>(data) + 1, size - 1);
    const std::size_t cut = bytes.empty() ? 0 : data[0] % bytes.size();

    lv::tui::InputParser parser;
    std::size_t events = 0;
    const auto drain = [&] {
        while (parser.next()) ++events;
    };
    parser.feed(bytes.substr(0, cut));
    drain();
    parser.feed(bytes.substr(cut));
    drain();
    parser.flush();
    drain();
    // Each event consumes at least one byte; more events than bytes means input was replayed.
    require(events <= bytes.size(), "at most one event per input byte");
    return 0;
}
