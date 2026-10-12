// config.toml is hand-edited; values the TUI writes back must read back unchanged.
#include "core/config.hpp"

#include "require.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    namespace core = lv::core;
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    (void)core::parse_config(text);

    if (const auto eq = text.find('='); eq != std::string_view::npos) {
        core::Config config;
        (void)core::set_config_value(config, text.substr(0, eq), text.substr(eq + 1));
    }

    // toml_quote() writes `workspace` and friends; control characters other than tab and
    // newline can't come from the TUI's line editor.
    const bool writable = std::ranges::none_of(text, [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u < 0x20 && c != '\t' && c != '\n';
    });
    if (writable) {
        const auto loaded = core::parse_config("workspace = " + core::toml_quote(text) + "\n");
        require(loaded.config.workspace == text && loaded.warnings.empty(),
                "toml_quote() values read back unchanged");
    }
    return 0;
}
