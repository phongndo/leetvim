#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace lv::core {

/// User settings, read from $XDG_CONFIG_HOME/leetvim/config.toml (a flat TOML subset).
struct Config {
    std::string lang = "cpp";
    std::string editor; // empty: nvim, vim or vi from PATH, then $VISUAL/$EDITOR
    std::string theme = "default";
    bool theme_background = false;
    bool rounded_corners = true;
    std::string truecolor = "auto"; // "auto", "true" or "false"
    std::string workspace = "~/leetcode";
    std::string prefix = "ctrl-g"; // leader key while the editor pane has focus
    bool mouse = true;
    bool save_before_run = true; // ask vim/nvim to :write before run/submit
    std::string username;        // show this user's stats when not signed in
    std::string base_url = "https://leetcode.com";
    double split = 0.42; // description pane width as a fraction of the screen
    bool show_paid = true;
    int refresh_hours = 24; // re-sync the problem list when the cache is older than this
    /// Text placed before/after the solution markers, per language slug
    /// ([inject_before] / [inject_after] tables). Not submitted.
    std::map<std::string, std::string> inject_before;
    std::map<std::string, std::string> inject_after;
};

struct LoadedConfig {
    Config config;
    std::vector<std::string> warnings; // unknown keys, bad values (with line numbers)
};

std::filesystem::path default_config_path();
/// A missing file yields the defaults without warnings.
LoadedConfig load_config(const std::filesystem::path& path);
LoadedConfig parse_config(std::string_view text);

/// Applies `key = value` (as typed in `:set key=value`). Returns an error message on failure.
std::string set_config_value(Config& config, std::string_view key, std::string_view value);

/// Persists one top-level key, editing the file in place so comments survive.
bool update_config_file(const std::filesystem::path& path, std::string_view key,
                        std::string_view value_toml);

/// A commented config file with every option at its default.
std::string_view default_config_text();

/// Quotes a string as a TOML basic string.
std::string toml_quote(std::string_view s);

} // namespace lv::core
