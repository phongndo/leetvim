#pragma once

#include <filesystem>
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

/// Which package manager installed the running leetvim, if any, which decides what
/// `leetvim update` runs. (An install-script release is recognised by its receipt first.)
struct Origin {
    enum class Method { homebrew, mise, nix, other } method = Method::other;
    std::filesystem::path brew; // Homebrew: the prefix's bin/brew
    std::string formula;        // Homebrew
    std::string store_dir;      // Nix: /nix/store/<hash>-<name>
};

/// From the executable's resolved path and MISE_DATA_DIR (empty if unset).
Origin classify(const std::filesystem::path& executable, std::string_view mise_data_dir);

/// The name of the element in `nix profile list --json` output that holds store_dir.
std::optional<std::string> nix_profile_element(std::string_view json, std::string_view store_dir);

/// `leetvim update [VERSION]`: replaces this process with whatever installed leetvim (the
/// install script shipped in the release, brew, mise or nix), pointed at this installation.
/// Returns only on failure.
int cmd_update(const std::string& version);

} // namespace lv::app
