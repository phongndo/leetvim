#include "core/config.hpp"

#include "util/paths.hpp"
#include "util/strings.hpp"

#include <charconv>
#include <cstdlib>
#include <format>
#include <optional>

namespace lv::core {

namespace {

struct Value {
    std::string text;
    bool quoted = false;
};

/// Parses a single-line TOML value: a basic or literal string, or a bare token.
std::optional<Value> parse_value(std::string_view s, std::string& error) {
    s = str::trim(s);
    if (s.empty()) {
        error = "missing value";
        return std::nullopt;
    }
    if (s.front() == '\'') {
        const auto end = s.find('\'', 1);
        if (end == std::string_view::npos) {
            error = "unterminated string";
            return std::nullopt;
        }
        return Value{.text = std::string(s.substr(1, end - 1)), .quoted = true};
    }
    if (s.front() == '"') {
        std::string out;
        for (std::size_t i = 1; i < s.size(); ++i) {
            const char c = s[i];
            if (c == '"') return Value{.text = out, .quoted = true};
            if (c != '\\' || i + 1 >= s.size()) {
                out += c;
                continue;
            }
            switch (const char e = s[++i]) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            default: out += '\\'; out += e;
            }
        }
        error = "unterminated string";
        return std::nullopt;
    }
    // Bare value: strip a trailing comment.
    const auto hash = s.find('#');
    return Value{.text = std::string(str::trim(s.substr(0, hash))), .quoted = false};
}

std::optional<bool> parse_bool(std::string_view v) {
    if (v == "true" || v == "yes" || v == "on" || v == "1") return true;
    if (v == "false" || v == "no" || v == "off" || v == "0") return false;
    return std::nullopt;
}

std::string apply(Config& c, std::string_view key, const Value& v) {
    const std::string_view t = v.text;
    const auto set_bool = [&](bool& field) -> std::string {
        const auto b = parse_bool(t);
        if (!b) return std::format("{}: expected true or false", key);
        field = *b;
        return {};
    };
    if (key.starts_with("inject_before.")) {
        c.inject_before[std::string(key.substr(14))] = v.text;
        return {};
    }
    if (key.starts_with("inject_after.")) {
        c.inject_after[std::string(key.substr(13))] = v.text;
        return {};
    }
    if (key == "lang")
        c.lang = t;
    else if (key == "editor")
        c.editor = t;
    else if (key == "theme")
        c.theme = t;
    else if (key == "theme_background")
        return set_bool(c.theme_background);
    else if (key == "rounded_corners")
        return set_bool(c.rounded_corners);
    else if (key == "mouse")
        return set_bool(c.mouse);
    else if (key == "save_before_run")
        return set_bool(c.save_before_run);
    else if (key == "show_paid")
        return set_bool(c.show_paid);
    else if (key == "truecolor") {
        const auto b = parse_bool(t);
        if (t != "auto" && !b) return "truecolor: expected auto, true or false";
        c.truecolor = !b ? "auto" : (*b ? "true" : "false");
    } else if (key == "workspace")
        c.workspace = t;
    else if (key == "prefix")
        c.prefix = t;
    else if (key == "username")
        c.username = t;
    else if (key == "base_url") {
        c.base_url = t;
        while (c.base_url.ends_with('/')) c.base_url.pop_back();
    } else if (key == "split") {
        // std::from_chars(double) needs a newer macOS deployment target than nixpkgs uses.
        const std::string copy(t);
        char* end = nullptr;
        const double d = std::strtod(copy.c_str(), &end);
        if (end == copy.c_str() || *end != '\0' || d < 0.15 || d > 0.85) {
            return "split: expected 0.15 - 0.85";
        }
        c.split = d;
    } else if (key == "refresh_hours") {
        int h = 0;
        const auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), h);
        if (ec != std::errc{} || h < 0) return "refresh_hours: expected a whole number";
        c.refresh_hours = h;
    } else {
        return std::format("unknown option '{}'", key);
    }
    return {};
}

} // namespace

std::filesystem::path default_config_path() { return paths::config_dir() / "config.toml"; }

LoadedConfig parse_config(std::string_view text) {
    LoadedConfig out;
    std::string section;
    const auto lines = str::lines(text);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = str::trim(lines[i]);
        const std::size_t lineno = i + 1;
        if (line.empty() || line.starts_with('#')) continue;
        if (line.starts_with('[')) {
            const auto end = line.find(']');
            if (end == std::string_view::npos) {
                out.warnings.push_back(std::format("line {}: malformed section", lineno));
                continue;
            }
            section = str::trim(line.substr(1, end - 1));
            if (section == "inject.before") section = "inject_before";
            if (section == "inject.after") section = "inject_after";
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) {
            out.warnings.push_back(std::format("line {}: expected key = value", lineno));
            continue;
        }
        std::string key(str::trim(line.substr(0, eq)));
        if (key.size() >= 2 && key.front() == '"' && key.back() == '"') {
            key = key.substr(1, key.size() - 2);
        }
        if (!section.empty()) key = std::format("{}.{}", section, key);
        std::string_view raw = str::trim(line.substr(eq + 1));

        std::optional<Value> value;
        if (raw.starts_with(R"(""")") || raw.starts_with("'''")) {
            // Multi-line string: gather lines until the closing delimiter.
            const std::string_view delim = raw.substr(0, 3);
            std::string body(raw.substr(3));
            bool closed = false;
            while (true) {
                if (const auto end = body.find(delim); end != std::string::npos) {
                    body.resize(end);
                    closed = true;
                    break;
                }
                if (++i >= lines.size()) break;
                body += '\n';
                body += lines[i];
            }
            if (!closed) {
                out.warnings.push_back(std::format("line {}: unterminated string", lineno));
                continue;
            }
            if (body.starts_with('\n')) body.erase(0, 1);
            value = Value{.text = body, .quoted = true};
        } else {
            std::string error;
            value = parse_value(raw, error);
            if (!value) {
                out.warnings.push_back(std::format("line {}: {}", lineno, error));
                continue;
            }
        }
        if (auto err = apply(out.config, key, *value); !err.empty()) {
            out.warnings.push_back(std::format("line {}: {}", lineno, err));
        }
    }
    return out;
}

LoadedConfig load_config(const std::filesystem::path& path) {
    std::string text;
    if (!paths::read_file(path, text)) return {};
    return parse_config(text);
}

std::string set_config_value(Config& config, std::string_view key, std::string_view value) {
    return apply(config, str::trim(key), Value{.text = std::string(str::trim(value))});
}

std::string toml_quote(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default: out += c;
        }
    }
    out += '"';
    return out;
}

bool update_config_file(const std::filesystem::path& path, std::string_view key,
                        std::string_view value_toml) {
    std::string text;
    paths::read_file(path, text);
    std::string out;
    bool replaced = false;
    bool in_section = false;
    std::size_t top_level_end = 0; // insertion point: before the first section
    bool seen_section = false;
    for (const auto line : str::lines(text)) {
        const auto trimmed = str::trim(line);
        if (trimmed.starts_with('[')) {
            in_section = true;
            if (!seen_section) {
                top_level_end = out.size();
                seen_section = true;
            }
        }
        if (!in_section && !replaced) {
            const auto eq = trimmed.find('=');
            if (eq != std::string_view::npos && !trimmed.starts_with('#')
                && str::trim(trimmed.substr(0, eq)) == key) {
                out += std::format("{} = {}\n", key, value_toml);
                replaced = true;
                continue;
            }
        }
        out += line;
        out += '\n';
    }
    while (out.ends_with("\n\n")) out.pop_back();
    if (!replaced) {
        const std::string entry = std::format("{} = {}\n", key, value_toml);
        if (seen_section) {
            out.insert(top_level_end, entry);
        } else {
            if (!out.empty() && !out.ends_with('\n')) out += '\n';
            out += entry;
        }
    }
    return paths::write_file(path, out);
}

std::string_view default_config_text() {
    return R"(# leetvim configuration (TOML)

# Default solution language (LeetCode slug: cpp, python3, java, rust, golang, typescript, ...).
lang = "cpp"

# Editor command for the solution pane. Empty: nvim, vim or vi from PATH, then $VISUAL/$EDITOR.
editor = ""

# Where solution files are written.
workspace = "~/leetcode"

# Leader key while the editor pane has focus; e.g. <prefix> t runs the tests.
prefix = "ctrl-g"

# Ask vim/nvim to :write before running or submitting.
save_before_run = true

# Themes: default, tokyo-night, gruvbox, catppuccin, nord, light.
theme = "default"
theme_background = false
rounded_corners = true
truecolor = "auto"
mouse = true

# Width of the description pane, as a fraction of the screen.
split = 0.42

# Show premium-only problems in the list.
show_paid = true

# Show this user's stats when you are not signed in.
username = ""

# Re-sync the problem list when the cache is older than this many hours.
refresh_hours = 24

# Text inserted around the solution markers (not submitted), e.g. for LSP support:
# [inject_before]
# cpp = """
# #include <bits/stdc++.h>
# using namespace std;
# """
)";
}

} // namespace lv::core
