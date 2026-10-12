// config.toml is hand-edited, and the TUI edits it in place: an edit must read back as the
// value written, and strings must survive quoting.
#include "core/config.hpp"

#include "require.hpp"
#include "util/unicode.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

namespace core = lv::core;

/// Valid UTF-8 without surrogates, which is what TOML strings can hold.
bool valid_utf8(std::string_view s) {
    std::size_t pos = 0;
    while (pos < s.size()) {
        const std::size_t start = pos;
        const char32_t cp = lv::unicode::decode(s, pos);
        if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF
            || lv::unicode::to_utf8(cp) != s.substr(start, pos - start)) {
            return false;
        }
    }
    return true;
}

/// A valid value for every option that differs from its default.
core::Config changed() {
    core::Config c;
    const core::Config defaults;
    for (const auto& o : core::options()) {
        std::string value;
        switch (o.kind) {
        case core::OptionKind::Bool: value = o.get(defaults) == "true" ? "false" : "true"; break;
        case core::OptionKind::Integer: value = "7"; break;
        case core::OptionKind::Number: value = "0.5"; break;
        case core::OptionKind::Choice: {
            const auto choices = o.choices();
            value = choices.back() != o.get(defaults) ? choices.back() : choices.front();
            break;
        }
        case core::OptionKind::Key: value = "f9"; break;
        case core::OptionKind::Text: value = "{lang}/\"{slug}\".{ext}"; break;
        }
        require(core::set_option(c, o, value).empty(), "the changed values are valid");
    }
    return c;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    (void)core::parse_config(text);

    if (const auto eq = text.find('='); eq != std::string_view::npos) {
        core::Config config;
        (void)core::set_config_value(config, text.substr(0, eq), text.substr(eq + 1));
    }

    // set_option_in_text() refuses edits that would change anything else, so whenever it
    // succeeds the option reads back as written.
    static const core::Config target = changed();
    const auto options = core::options();
    const auto& option = options[size % options.size()];
    if (const auto edited =
            core::set_option_in_text(text, option, core::toml_value(target, option))) {
        require(option.get(core::parse_config(*edited).config) == option.get(target),
                "an edited option reads back as written");
    }

    if (valid_utf8(text)) {
        const auto loaded = core::parse_config("[account]\nusername = " + core::toml_quote(text));
        require(loaded.warnings.empty() && loaded.config.account.username == text,
                "toml_quote() values read back unchanged");
    }
    return 0;
}
