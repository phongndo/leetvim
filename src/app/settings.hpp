#pragma once

#include "app/app.hpp"

#include <string_view>

namespace lv::app {

/// Every option of the config file, grouped by section. Changes apply at once and are
/// saved to the file.
class SettingsOverlay : public Overlay {
public:
    /// `select`: an option to start on ("theme", "ui.theme").
    explicit SettingsOverlay(App& app, std::string_view select = {});

    void draw(const tui::Painter& p, tui::Rect area) override;
    bool on_key(const tui::KeyEvent& ev) override;

private:
    void move(int delta);
    void change(const core::Option& option);

    App& app_;
    int selected_ = 0; // index into core::options()
    int scroll_ = 0;   // first visible row (rows include section headings)
    bool pending_g_ = false;
    const core::Option* capturing_ = nullptr; // waiting for the key to bind to it
};

} // namespace lv::app
