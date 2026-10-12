#include "core/config.hpp"

#include "core/languages.hpp"
#include "core/workspace.hpp"
#include "tui/input.hpp"
#include "tui/theme.hpp"
#include "util/paths.hpp"
#include "util/strings.hpp"

// toml++, header-only (this is its only user). Its parser states some preconditions as
// compiler assumptions (asserts in debug builds) that malformed input can break; as no-ops
// such input is just a parse error, which the config fuzzer checks.
#define TOML_HEADER_ONLY 1
#define TOML_ENABLE_FORMATTERS 0
#define TOML_ASSUME(expr) static_cast<void>(0)
#define TOML_ASSERT(expr) static_cast<void>(0)
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <optional>
#include <system_error>
#include <toml++/toml.hpp>

namespace lv::core {

namespace {

// ---------------------------------------------------------------- values as text

std::optional<bool> parse_bool(std::string_view v) {
    if (v == "true" || v == "yes" || v == "on" || v == "1") return true;
    if (v == "false" || v == "no" || v == "off" || v == "0") return false;
    return std::nullopt;
}

std::optional<double> parse_double(std::string_view v) {
    // std::from_chars(double) needs a newer macOS deployment target than nixpkgs uses.
    const std::string copy(v);
    char* end = nullptr;
    const double d = std::strtod(copy.c_str(), &end);
    if (copy.empty() || end != copy.c_str() + copy.size()) return std::nullopt;
    return d;
}

std::string to_text(bool v) { return v ? "true" : "false"; }
std::string to_text(int v) { return std::to_string(v); }
std::string to_text(double v) { return std::format("{}", v); }
std::string to_text(const std::string& v) { return v; }

// set_option() hands these canonical text, so they don't check it again.
void from_text(std::string_view t, bool& out) { out = t == "true"; }
void from_text(std::string_view t, int& out) {
    std::from_chars(t.data(), t.data() + t.size(), out);
}
void from_text(std::string_view t, double& out) { out = parse_double(t).value_or(out); }
void from_text(std::string_view t, std::string& out) { out = t; }

/// Fills in an option's accessors for the field `config.*Section.*Field`.
template <auto Section, auto Field>
constexpr Option field(Option o) {
    o.get = [](const Config& c) { return to_text(c.*Section.*Field); };
    o.assign = [](Config& c, std::string_view v) { from_text(v, c.*Section.*Field); };
    return o;
}

// ---------------------------------------------------------------- the options

std::vector<std::string> language_slugs() {
    std::vector<std::string> out;
    for (const auto& lang : languages()) out.emplace_back(lang.slug);
    return out;
}

std::vector<std::string> theme_names() {
    std::vector<std::string> out;
    for (const auto& theme : tui::builtin_themes()) out.emplace_back(theme.name);
    return out;
}

std::vector<std::string> truecolor_modes() { return {"auto", "true", "false"}; }

std::vector<std::string> sort_orders() {
    return {"id", "difficulty", "acceptance", "title", "status"};
}

std::string check_directory(std::string_view v) {
    return v.empty() ? std::string("expected a directory") : std::string();
}

using C = Config;
using enum OptionKind;

constexpr auto section_table = std::to_array<Section>({
    {.name = "solution", .help = ""},
    {.name = "editor", .help = ""},
    {.name = "keys",
     .help = "Keys in the solve view, pressed after the prefix (the description and results "
             "panes take them on their own too). Write a character (t, T, ?) or a key name, "
             "optionally with modifiers: ctrl-x, alt-x, shift-x, f1-f12, enter, tab, space, "
             "backspace, up, down, left, right, home, end, pageup, pagedown, insert, delete."},
    {.name = "ui", .help = ""},
    {.name = "list", .help = ""},
    {.name = "account", .help = ""},
});

constexpr auto option_table = std::to_array<Option>({
    field<&C::solution, &C::Solution::lang>(
        {.section = "solution",
         .name = "lang",
         .kind = Choice,
         .help = "Language for new solutions, as a LeetCode slug: cpp, python3, java, rust, "
                 "golang, ...",
         .choices = language_slugs}),
    field<&C::solution, &C::Solution::workspace>(
        {.section = "solution",
         .name = "workspace",
         .kind = Text,
         .help = "Directory for solution files and custom testcases.",
         .check = check_directory}),
    field<&C::solution, &C::Solution::filename>(
        {.section = "solution",
         .name = "filename",
         .kind = Text,
         .help = "Solution file inside the workspace; {id} {slug} {lang} {ext} are filled in, "
                 "and / makes subdirectories.",
         .check = check_filename_pattern}),

    field<&C::editor, &C::Editor::command>(
        {.section = "editor",
         .name = "command",
         .kind = Text,
         .help = "Editor for the solution pane. Empty: nvim, vim or vi from PATH, then $VISUAL "
                 "or $EDITOR."}),
    field<&C::editor, &C::Editor::save_before_run>(
        {.section = "editor",
         .name = "save_before_run",
         .kind = Bool,
         .help = "Have vim/nvim save the file before running or submitting."}),

    field<&C::keys, &C::Keys::prefix>({.section = "keys",
                                       .name = "prefix",
                                       .kind = Key,
                                       .help = "Prefix for leetvim's keys while the editor has "
                                               "focus, like tmux's; twice sends it to vim."}),
    field<&C::keys, &C::Keys::run>(
        {.section = "keys", .name = "run", .kind = Key, .help = "Run the testcases on LeetCode."}),
    field<&C::keys, &C::Keys::submit>(
        {.section = "keys", .name = "submit", .kind = Key, .help = "Submit the solution."}),
    field<&C::keys, &C::Keys::results>({.section = "keys",
                                        .name = "results",
                                        .kind = Key,
                                        .help = "Show or hide the results panel."}),
    field<&C::keys, &C::Keys::testcases>(
        {.section = "keys", .name = "testcases", .kind = Key, .help = "Open the testcases tab."}),
    field<&C::keys, &C::Keys::zoom>({.section = "keys",
                                     .name = "zoom",
                                     .kind = Key,
                                     .help = "Zoom the editor to the whole screen."}),
    field<&C::keys, &C::Keys::back>(
        {.section = "keys",
         .name = "back",
         .kind = Key,
         .help = "Back to the problem list; the editor keeps running."}),
    field<&C::keys, &C::Keys::help>(
        {.section = "keys", .name = "help", .kind = Key, .help = "Show every key."}),

    field<&C::ui, &C::Ui::theme>({.section = "ui",
                                  .name = "theme",
                                  .kind = Choice,
                                  .help = "Color theme.",
                                  .choices = theme_names}),
    field<&C::ui, &C::Ui::theme_background>(
        {.section = "ui",
         .name = "theme_background",
         .kind = Bool,
         .help = "Paint the theme's background instead of keeping the terminal's."}),
    field<&C::ui, &C::Ui::rounded_corners>({.section = "ui",
                                            .name = "rounded_corners",
                                            .kind = Bool,
                                            .help = "Round the corners of boxes."}),
    field<&C::ui, &C::Ui::truecolor>(
        {.section = "ui",
         .name = "truecolor",
         .kind = Choice,
         .help = "24-bit color; auto uses it when $COLORTERM says the terminal has it.",
         .choices = truecolor_modes}),
    field<&C::ui, &C::Ui::mouse>({.section = "ui",
                                  .name = "mouse",
                                  .kind = Bool,
                                  .help = "Click to focus and select, scroll with the wheel."}),
    field<&C::ui, &C::Ui::split>(
        {.section = "ui",
         .name = "split",
         .kind = Number,
         .help = "Width of the description pane, as a fraction of the screen (0.15 to 0.85).",
         .min = 0.15,
         .max = 0.85}),

    field<&C::list, &C::List::show_paid>({.section = "list",
                                          .name = "show_paid",
                                          .kind = Bool,
                                          .help = "Show premium-only problems."}),
    field<&C::list, &C::List::sort>({.section = "list",
                                     .name = "sort",
                                     .kind = Choice,
                                     .help = "Sort order at startup; o cycles it, O reverses it.",
                                     .choices = sort_orders}),
    field<&C::list, &C::List::show_stats>(
        {.section = "list",
         .name = "show_stats",
         .kind = Bool,
         .help = "Start with the stats box shown (1 toggles it)."}),
    field<&C::list, &C::List::show_preview>(
        {.section = "list",
         .name = "show_preview",
         .kind = Bool,
         .help = "Start with the preview shown (2 toggles it)."}),

    field<&C::account, &C::Account::username>(
        {.section = "account",
         .name = "username",
         .kind = Text,
         .help = "Whose stats to show when you aren't signed in."}),
    field<&C::account, &C::Account::refresh_hours>(
        {.section = "account",
         .name = "refresh_hours",
         .kind = Integer,
         .help = "Re-sync the problem list at startup when it's older than this many hours.",
         .min = 0,
         .max = 24 * 365}),
});

/// Help for the generated file: the option's own, plus its choices when they're few.
std::string describe(const Option& o) {
    std::string out(o.help);
    if (o.kind == Choice) {
        const auto choices = o.choices();
        if (choices.size() <= 8) out += std::format(" One of: {}.", str::join(choices, ", "));
    }
    return out;
}

/// `text` as "# " comment lines of at most 92 columns.
std::string comment(std::string_view text) {
    std::string out;
    std::string line = "#";
    for (const auto word : str::split(text, ' ')) {
        if (word.empty()) continue;
        if (line.size() + 1 + word.size() > 92 && line != "#") {
            out += line + '\n';
            line = "#";
        }
        line += ' ';
        line += word;
    }
    if (line != "#") out += line + '\n';
    return out;
}

const Option* find_in_section(std::string_view section, std::string_view name) {
    for (const auto& o : option_table) {
        if (o.section == section && o.name == name) return &o;
    }
    return nullptr;
}

// ---------------------------------------------------------------- TOML

/// The value of a TOML node as option text, or nullopt if it has the wrong type.
std::optional<std::string> node_text(const toml::node& node, const Option& o) {
    switch (o.kind) {
    case Bool:
        if (const auto b = node.value_exact<bool>()) return to_text(*b);
        break;
    case Integer:
        if (const auto i = node.value_exact<std::int64_t>()) return std::to_string(*i);
        break;
    case Number:
        if (const auto i = node.value_exact<std::int64_t>()) return std::to_string(*i);
        if (const auto d = node.value_exact<double>()) return to_text(*d);
        break;
    case Choice:
        // truecolor = true reads like truecolor = "true".
        if (const auto b = node.value_exact<bool>()) return to_text(*b);
        [[fallthrough]];
    case Text:
    case Key:
        if (auto s = node.value_exact<std::string>()) return s;
        break;
    }
    return std::nullopt;
}

std::string_view expected_type(OptionKind kind) {
    switch (kind) {
    case Bool: return "true or false";
    case Integer: return "a whole number";
    case Number: return "a number";
    case Text:
    case Choice:
    case Key: break;
    }
    return "a string";
}

/// A problem in the file, at a line.
struct Problem {
    std::uint32_t line = 0;
    std::string message;
};

/// Reads a [solution.inject_*] table: language slug -> text.
void read_injections(const toml::node& node, std::string_view table_name,
                     std::map<std::string, std::string>& out, std::vector<Problem>& problems) {
    const auto* table = node.as_table();
    if (table == nullptr) {
        problems.push_back(
            {node.source().begin.line,
             std::format("solution.{} must be a table of language = text", table_name)});
        return;
    }
    for (const auto& [key, value] : *table) {
        const auto line = value.source().begin.line;
        const auto* lang = find_language(key.str());
        if (lang == nullptr || lang->slug != key.str()) {
            problems.push_back(
                {line, std::format("solution.{}: unknown language '{}'", table_name, key.str())});
        } else if (const auto text = value.value_exact<std::string>()) {
            out[std::string(key.str())] = *text;
        } else {
            problems.push_back(
                {line, std::format("solution.{}.{}: expected a string", table_name, key.str())});
        }
    }
}

/// Parses TOML, or returns the syntax error as "line N: description".
std::optional<toml::table> parse_toml(std::string_view text, std::string& error) {
    try {
        return toml::parse(text);
    } catch (const toml::parse_error& e) {
        error = std::format("line {}: {}", e.source().begin.line, e.description());
        return std::nullopt;
    }
}

/// Removes an option from parsed TOML, and its section if that leaves it empty.
void erase_option(toml::table& root, const Option& o) {
    auto* section = root.get_as<toml::table>(o.section);
    if (section == nullptr) return;
    section->erase(o.name);
    if (section->empty()) root.erase(o.section);
}

std::string unquote(std::string_view s) {
    s = str::trim(s);
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front()) {
        s = s.substr(1, s.size() - 2);
    }
    return std::string(s);
}

} // namespace

// ---------------------------------------------------------------- options

std::string Option::key() const { return std::format("{}.{}", section, name); }

std::span<const Option> options() { return option_table; }
std::span<const Section> sections() { return section_table; }

const Option* find_option(std::string_view key) {
    if (const auto dot = key.find('.'); dot != std::string_view::npos) {
        return find_in_section(key.substr(0, dot), key.substr(dot + 1));
    }
    const Option* found = nullptr;
    for (const auto& o : option_table) {
        if (o.name != key) continue;
        if (found != nullptr) return nullptr; // ambiguous
        found = &o;
    }
    return found;
}

std::string set_option(Config& config, const Option& o, std::string_view value) {
    const auto fail = [&](std::string_view expected) {
        return std::format("{}: expected {}", o.key(), expected);
    };
    std::string canonical(value);
    switch (o.kind) {
    case Bool: {
        const auto b = parse_bool(value);
        if (!b) return fail("true or false");
        canonical = to_text(*b);
        break;
    }
    case Integer: {
        int n = 0;
        const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
        if (ec != std::errc{} || end != value.data() + value.size() || n < o.min || n > o.max) {
            return fail(std::format("a whole number from {} to {}", o.min, o.max));
        }
        canonical = to_text(n);
        break;
    }
    case Number: {
        const auto d = parse_double(value);
        if (!d || !std::isfinite(*d) || *d < o.min || *d > o.max) {
            return fail(std::format("a number from {} to {}", o.min, o.max));
        }
        canonical = to_text(*d);
        break;
    }
    case Choice: {
        auto choices = o.choices();
        if (std::ranges::find(choices, value) == choices.end()) {
            if (choices.size() > 8) {
                choices.resize(8);
                choices.emplace_back("…");
            }
            return fail("one of " + str::join(choices, ", "));
        }
        break;
    }
    case Key:
        if (!tui::KeySpec::parse(value)) return fail("a key such as t, ctrl-g or f5");
        break;
    case Text: break;
    }
    if (o.check != nullptr) {
        if (auto err = o.check(canonical); !err.empty()) {
            return std::format("{}: {}", o.key(), err);
        }
    }
    o.assign(config, canonical);
    return {};
}

std::string set_config_value(Config& config, std::string_view key, std::string_view value) {
    key = str::trim(key);
    const Option* o = find_option(key);
    if (o == nullptr) return std::format("unknown option '{}'", key);
    return set_option(config, *o, str::trim(value));
}

std::string toml_value(const Config& config, const Option& o) {
    switch (o.kind) {
    case Bool:
    case Integer:
    case Number: return o.get(config);
    case Text:
    case Choice:
    case Key: break;
    }
    return toml_quote(o.get(config));
}

std::vector<std::string> key_conflicts(const Config& config) {
    // In the prefix layer these move the focus, and Esc cancels.
    constexpr std::array reserved{
        std::pair{tui::KeySpec{.key = tui::Key::Char, .ch = U'h'}, "moving the focus"},
        std::pair{tui::KeySpec{.key = tui::Key::Char, .ch = U'j'}, "moving the focus"},
        std::pair{tui::KeySpec{.key = tui::Key::Char, .ch = U'k'}, "moving the focus"},
        std::pair{tui::KeySpec{.key = tui::Key::Char, .ch = U'l'}, "moving the focus"},
        std::pair{tui::KeySpec{.key = tui::Key::Left}, "moving the focus"},
        std::pair{tui::KeySpec{.key = tui::Key::Down}, "moving the focus"},
        std::pair{tui::KeySpec{.key = tui::Key::Up}, "moving the focus"},
        std::pair{tui::KeySpec{.key = tui::Key::Right}, "moving the focus"},
        std::pair{tui::KeySpec{.key = tui::Key::Escape}, "cancelling the prefix"},
    };
    std::vector<std::string> out;
    std::vector<std::pair<tui::KeySpec, const Option*>> seen;
    for (const auto& o : option_table) {
        if (o.kind != Key) continue;
        const auto spec = tui::KeySpec::parse(o.get(config));
        if (!spec) continue;
        if (o.name != "prefix") {
            for (const auto& [key, what] : reserved) {
                if (key == *spec) {
                    out.push_back(
                        std::format("{}: {} is reserved for {}", o.key(), spec->to_string(), what));
                }
            }
        }
        for (const auto& [key, other] : seen) {
            if (key == *spec) {
                out.push_back(std::format("{}: {} is also bound to {}", o.key(), spec->to_string(),
                                          other->key()));
            }
        }
        seen.emplace_back(*spec, &o);
    }
    return out;
}

// ---------------------------------------------------------------- reading

std::filesystem::path default_config_path() { return paths::config_dir() / "config.toml"; }

LoadedConfig parse_config(std::string_view text) {
    LoadedConfig out;
    std::string error;
    const auto root = parse_toml(text, error);
    if (!root) {
        out.warnings.push_back(error + " (using the default settings)");
        return out;
    }
    std::vector<Problem> problems;
    const auto warn = [&](const toml::source_region& at, std::string message) {
        problems.push_back({at.begin.line, std::move(message)});
    };
    for (const auto& [section_key, section_node] : *root) {
        const std::string_view section = section_key.str();
        const auto* table = section_node.as_table();
        const bool known =
            std::ranges::find(section_table, section, &Section::name) != section_table.end();
        if (!known || table == nullptr) {
            if (known) {
                warn(section_key.source(),
                     std::format("{} must be a section: [{}]", section, section));
            } else if (const auto* o = find_option(section)) {
                warn(section_key.source(),
                     std::format("'{}' belongs in the [{}] section", section, o->section));
            } else {
                warn(section_key.source(), std::format("unknown section [{}]", section));
            }
            continue;
        }
        for (const auto& [name_key, node] : *table) {
            const std::string_view name = name_key.str();
            if (section == "solution" && (name == "inject_before" || name == "inject_after")) {
                read_injections(node, name,
                                name == "inject_before" ? out.config.solution.inject_before
                                                        : out.config.solution.inject_after,
                                problems);
                continue;
            }
            const Option* o = find_in_section(section, name);
            if (o == nullptr) {
                warn(name_key.source(), std::format("unknown option {}.{}", section, name));
                continue;
            }
            const auto value = node_text(node, *o);
            if (!value) {
                warn(node.source(),
                     std::format("{}: expected {}", o->key(), expected_type(o->kind)));
            } else if (auto err = set_option(out.config, *o, *value); !err.empty()) {
                warn(node.source(), std::move(err));
            }
        }
    }
    // toml++ keeps tables sorted by key; report in file order.
    std::ranges::stable_sort(problems, {}, &Problem::line);
    for (auto& [line, message] : problems) {
        out.warnings.push_back(std::format("line {}: {}", line, message));
    }
    for (auto& conflict : key_conflicts(out.config)) out.warnings.push_back(std::move(conflict));
    return out;
}

LoadedConfig load_config(const std::filesystem::path& path) {
    std::string text;
    if (!paths::read_file(path, text)) return {};
    return parse_config(text);
}

// ---------------------------------------------------------------- writing

std::expected<std::string, std::string> set_option_in_text(std::string_view text, const Option& o,
                                                           std::string_view value_toml) {
    std::string error;
    auto before = parse_toml(text, error);
    if (!before) return std::unexpected(error);

    auto lines = str::lines(text);
    if (text.ends_with('\n')) lines.pop_back();

    // Find the option's line(s) and the last line of its section. Multi-line strings are
    // skipped whole so their contents never look like keys or headers.
    const std::string key = o.key();
    std::optional<std::pair<std::size_t, std::size_t>> target;
    std::optional<std::size_t> section_end;
    std::string current; // the section of the line being read
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto line = str::trim(lines[i]);
        if (line.empty() || line.starts_with('#')) continue;
        if (line.starts_with('[')) {
            const auto close = line.find(']');
            current = line.starts_with("[[") || close == std::string_view::npos
                          ? std::string(line) // array of tables: never an option's section
                          : unquote(line.substr(1, close - 1));
            if (current == o.section) section_end = i;
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string name = unquote(line.substr(0, eq));
        const auto value = str::trim(line.substr(eq + 1));
        std::size_t last = i;
        for (const std::string_view delim : {R"(""")", "'''"}) {
            if (value.starts_with(delim) && value.find(delim, 3) == std::string_view::npos) {
                while (last + 1 < lines.size()) {
                    if (lines[++last].contains(delim)) break;
                }
                break;
            }
        }
        if ((current.empty() ? name : std::format("{}.{}", current, name)) == key) {
            target.emplace(i, last);
        }
        if (current == o.section) section_end = last;
        i = last;
    }

    std::string out;
    const auto emit = [&](std::string_view line) {
        out += line;
        out += '\n';
    };
    if (target) {
        // Keep the indentation and the key as written.
        const auto [first, last] = *target;
        const std::string_view line = lines[first];
        const auto indent = line.find_first_not_of(" \t");
        const auto eq = line.find('=');
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (i == first) {
                emit(std::format("{}{} = {}", line.substr(0, indent),
                                 str::trim(line.substr(indent, eq - indent)), value_toml));
            } else if (i < first || i > last) {
                emit(lines[i]);
            }
        }
    } else if (section_end) {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            emit(lines[i]);
            if (i == *section_end) emit(std::format("{} = {}", o.name, value_toml));
        }
    } else {
        for (const auto line : lines) emit(line);
        while (out.ends_with("\n\n")) out.pop_back();
        if (!out.empty()) out += '\n';
        emit(std::format("[{}]", o.section));
        emit(std::format("{} = {}", o.name, value_toml));
    }

    // The edit must change this one value and nothing else.
    auto after = parse_toml(out, error);
    const auto wanted = parse_toml(std::format("v = {}", value_toml), error);
    if (!after || !wanted || after->at_path(key) != (*wanted)["v"]) {
        return std::unexpected("couldn't change " + key + " safely; edit the file by hand");
    }
    erase_option(*before, o);
    erase_option(*after, o);
    if (*before != *after) {
        return std::unexpected("couldn't change " + key + " safely; edit the file by hand");
    }
    return out;
}

std::string update_config_file(const std::filesystem::path& path, const Option& option,
                               std::string_view value_toml) {
    std::string text;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        text = default_config_text();
    } else if (!paths::read_file(path, text)) {
        return std::format("can't read {}", path.string());
    }
    auto updated = set_option_in_text(text, option, value_toml);
    if (!updated) return std::format("{}: {}", path.string(), updated.error());
    if (!paths::write_file(path, *updated)) return std::format("can't write {}", path.string());
    return {};
}

std::string default_config_text() {
    const Config defaults;
    std::string out =
        "# leetvim settings, each at its default. Change them here, with :set <option>=<value>,\n"
        "# or in the settings screen (press , or type :settings).\n";
    for (const auto& section : section_table) {
        out += '\n';
        if (!section.help.empty()) out += comment(section.help);
        out += std::format("[{}]\n", section.name);
        bool first = true;
        for (const auto& o : option_table) {
            if (o.section != section.name) continue;
            if (!first) out += '\n';
            first = false;
            out += comment(describe(o));
            out += std::format("{} = {}\n", o.name, toml_value(defaults, o));
        }
        if (section.name == "solution") {
            out += "\n"
                   + comment("Text added before the code in new solution files, per "
                             "language, e.g. for your LSP; [solution.inject_after] adds it "
                             "after. It isn't submitted.");
            out += "# [solution.inject_before]\n"
                   "# cpp = \"\"\"\n"
                   "# #include <bits/stdc++.h>\n"
                   "# using namespace std;\n"
                   "# \"\"\"\n";
        }
    }
    return out;
}

std::string toml_quote(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (const auto u = static_cast<unsigned char>(c); u < 0x20 || u == 0x7f) {
                out += std::format("\\u{:04X}", u);
            } else {
                out += c;
            }
        }
    }
    out += '"';
    return out;
}

} // namespace lv::core
