#pragma once

#include <filesystem>
#include <string_view>

namespace lv::paths {

std::filesystem::path home();
/// Expands a leading "~" or "~/" to the home directory.
std::filesystem::path expand_user(std::string_view path);

// XDG base directories with the conventional fallbacks, suffixed with "leetvim".
std::filesystem::path config_dir(); // $XDG_CONFIG_HOME/leetvim
std::filesystem::path cache_dir();  // $XDG_CACHE_HOME/leetvim
std::filesystem::path data_dir();   // $XDG_DATA_HOME/leetvim
std::filesystem::path state_dir();  // $XDG_STATE_HOME/leetvim

/// Reads a whole file; returns false if it cannot be opened.
bool read_file(const std::filesystem::path& path, std::string& out);
/// Writes atomically (temp file + rename), creating parent directories.
/// `mode` sets the permission bits of the new file (e.g. 0600 for secrets).
bool write_file(const std::filesystem::path& path, std::string_view contents,
                std::filesystem::perms mode = std::filesystem::perms::owner_read
                                              | std::filesystem::perms::owner_write
                                              | std::filesystem::perms::group_read
                                              | std::filesystem::perms::others_read);

} // namespace lv::paths
