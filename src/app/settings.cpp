#include "app/settings.hpp"

#include "app/overlays.hpp"
#include "app/ui.hpp"
#include "util/paths.hpp"

#include <algorithm>
#include <format>
#include <iterator>
#include <set>
#include <utility>

namespace lv::app {

using tui::Key;
using tui::KeyEvent;
using tui::ModCtrl;
using tui::Rect;

namespace {

/// What the settings screen lists: each section's heading, then its options.
struct Row {
    std::string_view heading;             // set for a heading
    const core::Option* option = nullptr; // set for an option
};

std::vector<Row> rows() {
    std::vector<Row> out;
    for (const auto& section : core::sections()) {
        out.push_back({.heading = section.name});
        for (const auto& o : core::options()) {
            if (o.section == section.name) out.push_back({.heading = {}, .option = &o});
        }
    }
    return out;
}

std::string home_relative(const std::filesystem::path& path) {
    const std::string home = paths::home().string();
    std::string s = path.string();
    if (home.size() > 1 && s.starts_with(home + "/")) return "~" + s.substr(home.size());
    return s;
}

} // namespace

SettingsOverlay::SettingsOverlay(App& app, std::string_view select) : app_(app) {
    if (const auto* o = core::find_option(select)) {
        selected_ = static_cast<int>(o - core::options().data());
    }
}

void SettingsOverlay::move(int delta) {
    const int count = static_cast<int>(core::options().size());
    selected_ = std::clamp(selected_ + delta, 0, count - 1);
}

void SettingsOverlay::draw(const tui::Painter& p, Rect area) {
    const auto& t = app_.theme();
    const auto& config = app_.config();
    const core::Config defaults;
    const auto& selected = core::options()[static_cast<std::size_t>(selected_)];
    const auto all = rows();

    std::vector<tui::Segments> hints;
    if (capturing_ != nullptr) {
        hints = {ui::label(std::format("press the new key for {}", capturing_->key()), t.hi_fg,
                           tui::AttrBold),
                 ui::hint(t, "esc", "cancel")};
    } else {
        hints = {ui::hint(t, "⏎", "change"), ui::hint(t, "r", "reset"),
                 ui::hint(t, "esc", "close")};
    }
    const int footer = 4; // rule, two lines of help, default and file
    const Rect r = centered(area, std::min(area.w - 4, 88),
                            std::min(area.h - 2, static_cast<int>(all.size()) + footer + 2));
    const Rect inner = overlay_frame(app_, p, r, "settings", std::move(hints));
    const int list_h = std::max(1, inner.h - footer);

    // Keep the selection in view, with its section's heading when it fits.
    const auto at = std::ranges::find(all, &selected, &Row::option);
    const int row = static_cast<int>(std::distance(all.begin(), at));
    const int top = at != all.begin() && std::prev(at)->option == nullptr ? row - 1 : row;
    scroll_ = std::min(scroll_, top);
    if (row >= scroll_ + list_h) scroll_ = row - list_h + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(all.size()) - list_h));

    const int name_w = 20;
    const Rect list{inner.x, inner.y, inner.w - 1, list_h};
    const tui::Painter clip = p.clipped(list);
    for (int i = 0; i < list_h; ++i) {
        const auto index = static_cast<std::size_t>(scroll_) + static_cast<std::size_t>(i);
        if (index >= all.size()) break;
        const int y = list.y + i;
        if (all[index].option == nullptr) {
            clip.text(list.x + 1, y, std::format("[{}]", all[index].heading),
                      ui::fg(t.title, tui::AttrBold));
            continue;
        }
        const auto& o = *all[index].option;
        const bool sel = &o == &selected;
        const tui::Style base{.fg = t.selected_fg, .bg = t.selected_bg, .attrs = tui::AttrBold};
        if (sel) clip.fill({list.x, y, list.w, 1}, base);
        clip.text(list.x + 3, y, o.name, sel ? base : ui::fg(t.main_fg), name_w - 1);
        const std::string value = o.get(config);
        const bool changed = value != o.get(defaults);
        const bool waiting = capturing_ == &o;
        const tui::Style style = sel             ? base
                                 : value.empty() ? ui::fg(t.inactive_fg)
                                 : changed       ? ui::fg(t.accent)
                                                 : ui::fg(t.graph_text);
        clip.text(list.x + 3 + name_w, y,
                  waiting         ? "press a key…"
                  : value.empty() ? "(empty)"
                                  : value,
                  style, list.right() - (list.x + 3 + name_w) - 1);
    }
    tui::scrollbar(p, inner.right() - 1, list.y, list_h, scroll_, list_h,
                   static_cast<int>(all.size()), ui::fg(t.div_line), ui::fg(t.hi_fg));

    // Footer: what the selected option does, its default and where it's saved.
    int y = list.bottom();
    std::string rule;
    for (int i = 0; i < inner.w; ++i) rule += "─";
    p.text(inner.x, y++, rule, ui::fg(t.div_line));
    ui::draw_wrapped(p, {inner.x + 1, y, inner.w - 2, 2}, selected.help, ui::fg(t.main_fg));
    y += 2;
    p.text(inner.x + 1, y,
           std::format("default {}  ·  saved in {}", core::toml_value(defaults, selected),
                       home_relative(app_.config_path())),
           ui::fg(t.graph_text), inner.w - 2);
}

bool SettingsOverlay::on_key(const KeyEvent& ev) {
    if (capturing_ != nullptr) {
        const auto* option = std::exchange(capturing_, nullptr);
        if (ev.key != Key::None && !ev.is(Key::Escape)) {
            app_.set_option(option->key(), tui::KeySpec::of(ev).to_spec());
        }
        return true;
    }
    const auto& selected = core::options()[static_cast<std::size_t>(selected_)];
    if ((std::exchange(pending_g_, false) && ev.is('g')) || ev.is(Key::Home)) {
        selected_ = 0;
    } else if (ev.is('j') || ev.is(Key::Down) || ev.is('n', ModCtrl) || ev.is(Key::Tab)) {
        move(1);
    } else if (ev.is('k') || ev.is(Key::Up) || ev.is('p', ModCtrl)
               || ev.is(Key::Tab, tui::ModShift)) {
        move(-1);
    } else if (ev.is('d', ModCtrl) || ev.is(Key::PageDown)) {
        move(8);
    } else if (ev.is('u', ModCtrl) || ev.is(Key::PageUp)) {
        move(-8);
    } else if (ev.is('G') || ev.is(Key::End)) {
        move(static_cast<int>(core::options().size()));
    } else if (ev.is('g')) {
        pending_g_ = true;
    } else if (ev.is(Key::Enter) || ev.is(' ') || ev.is('l') || ev.is(Key::Right)) {
        change(selected);
    } else if (ev.is('r')) {
        app_.set_option(selected.key(), selected.get(core::Config{}));
    } else if (ev.is(Key::Escape) || ev.is('q') || ev.is(',') || ev.is('c', ModCtrl)) {
        closed_ = true;
    }
    return true;
}

void SettingsOverlay::change(const core::Option& option) {
    App& app = app_;
    // Not const: the callbacks below capture copies, which must stay movable.
    std::string key = option.key();
    std::string current = option.get(app.config());
    switch (option.kind) {
    case core::OptionKind::Bool: app.set_option(key, current == "true" ? "false" : "true"); break;
    case core::OptionKind::Key: capturing_ = &option; break;
    case core::OptionKind::Choice: {
        std::vector<PickerOverlay::Item> items;
        for (const auto& choice : option.choices()) {
            items.push_back({.label = choice, .detail = "", .value = choice, .color = {}});
        }
        // Moving through the choices previews them; only picking one saves it.
        app.push_overlay(std::make_unique<PickerOverlay>(
            app, key, std::move(items),
            PickerOverlay::Callbacks{
                .on_pick = [&app, key](const std::string& v) { app.set_option(key, v); },
                .on_done = {},
                .on_highlight = [&app,
                                 key](const std::string& v) { app.set_option(key, v, false); },
                .on_cancel = [&app, key, current] { app.set_option(key, current, false); }},
            false, std::set<std::string>{}, current));
        break;
    }
    case core::OptionKind::Text:
    case core::OptionKind::Integer:
    case core::OptionKind::Number:
        app.push_overlay(std::make_unique<PromptOverlay>(
            app, key,
            std::vector<std::string>{std::string(option.help),
                                     "Default: " + core::toml_value(core::Config{}, option)},
            [&app, key](const std::string& v) { app.set_option(key, v); }, false, current));
        break;
    }
}

} // namespace lv::app
