#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace lv::app {

/// Where scripts/install.sh put leetvim, read from the receipt it leaves in the release
/// (share/leetvim/installation). Other installs (Homebrew, mise, Nix, source) have none.
struct Installation {
    std::string install_dir; // holds the `leetvim` symlink
    std::string data_dir;    // holds releases/
};

std::optional<Installation> parse_installation(std::string_view receipt);

/// `leetvim update [VERSION]`: replaces this process with the installer that came with the
/// running release, pointed at the same directories. Returns only on failure.
int cmd_update(const std::string& version);

} // namespace lv::app
