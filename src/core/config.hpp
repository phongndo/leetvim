#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lv::core {

/// User settings, read from config.toml. Each struct is one [section] of the file; every
/// option in them is described once, in options().
struct Config {
    struct Solution {
        std::string lang = "cpp";
        std::string workspace = "~/.leetvim";
        std::string filename = "{id}.{slug}.{ext}";
        /// Text placed before/after the solution markers, per language slug
        /// ([solution.inject_before] / [solution.inject_after]). Not submitted.
        std::map<std::string, std::string> inject_before;
        std::map<std::string, std::string> inject_after;
    };
    struct Editor {
        std::string command; // empty: nvim, vim or vi from PATH, then $VISUAL/$EDITOR
        bool save_before_run = true;
    };
    /// Key specs as written in the file ("ctrl-g", "t", "f5"); see tui::KeySpec.
    struct Keys {
        std::string prefix = "ctrl-g";
        std::string run = "t";
        std::string submit = "s";
        std::string results = "r";
        std::string testcases = "c";
        std::string zoom = "z";
        std::string back = "q";
        std::string help = "?";
    };
    struct Ui {
        std::string theme = "default";
        bool theme_background = false;
        bool rounded_corners = true;
        std::string truecolor = "auto"; // "auto", "true" or "false"
        bool mouse = true;
        double split = 0.42; // description pane width as a fraction of the screen
    };
    struct List {
        bool show_paid = true;
        std::string sort = "id";
        bool show_stats = true;
        bool show_preview = true;
    };
    struct Account {
        std::string username; // whose stats to show when not signed in
        int refresh_hours = 24;
    };

    Solution solution;
    Editor editor;
    Keys keys;
    Ui ui;
    List list;
    Account account;
};

enum class OptionKind : std::uint8_t {
    Text,
    Bool,
    Integer,
    Number,
    Choice, // one of choices()
    Key,    // a tui::KeySpec
};

/// One setting: where it lives in the file, how to check it, and how to read and write it
/// on a Config. Values cross this interface as text ("true", "0.42", "nord").
struct Option {
    std::string_view section;
    std::string_view name;
    OptionKind kind = OptionKind::Text;
    std::string_view help; // one line, shown in the generated file and the settings screen
    std::string (*get)(const Config&) = nullptr;
    void (*assign)(Config&, std::string_view) = nullptr; // takes an already checked value
    std::vector<std::string> (*choices)() = nullptr;     // Choice
    std::string (*check)(std::string_view) = nullptr;    // extra rule for Text; "" when valid
    double min = 0;                                      // Integer and Number
    double max = 0;

    /// "section.name", as used by :set.
    [[nodiscard]] std::string key() const;
};

struct Section {
    std::string_view name;
    std::string_view help;
};

/// Every option, grouped by section in file order.
std::span<const Option> options();
std::span<const Section> sections();
/// By "section.name", or by a bare name that only one section has ("theme").
const Option* find_option(std::string_view key);

/// Validates and applies a value typed as text. Returns an error message, or "" on success.
std::string set_option(Config& config, const Option& option, std::string_view value);
/// `:set key=value`: find_option() plus set_option().
std::string set_config_value(Config& config, std::string_view key, std::string_view value);
/// The option's current value as TOML ("true", "0.42", "\"nord\"").
std::string toml_value(const Config& config, const Option& option);
/// Problems with the key bindings as a whole, such as one key bound to two actions.
std::vector<std::string> key_conflicts(const Config& config);

struct LoadedConfig {
    Config config;
    std::vector<std::string> warnings; // syntax errors, unknown keys, bad values (with lines)
};

/// $XDG_CONFIG_HOME/leetvim/config.toml, ~/.config/leetvim/config.toml by default.
std::filesystem::path default_config_path();
/// A missing file yields the defaults without warnings.
LoadedConfig load_config(const std::filesystem::path& path);
/// A file that isn't valid TOML yields the defaults and the syntax error.
LoadedConfig parse_config(std::string_view text);

/// Sets one option in the text of a config file, keeping everything else (comments
/// included). Fails rather than return text that would read back differently.
std::expected<std::string, std::string>
set_option_in_text(std::string_view text, const Option& option, std::string_view value_toml);
/// set_option_in_text() on a file, creating it from default_config_text() if missing.
/// Returns an error message, or "" on success.
std::string update_config_file(const std::filesystem::path& path, const Option& option,
                               std::string_view value_toml);

/// A commented config file with every option at its default.
std::string default_config_text();

/// Quotes a string as a TOML basic string.
std::string toml_quote(std::string_view s);

} // namespace lv::core
