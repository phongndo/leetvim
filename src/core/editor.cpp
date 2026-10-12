#include "core/editor.hpp"

#include "util/strings.hpp"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>

namespace lv::core {

std::vector<std::string> split_command(std::string_view command) {
    std::vector<std::string> out;
    std::string current;
    bool have = false;
    char quote = 0;
    for (std::size_t i = 0; i < command.size(); ++i) {
        const char c = command[i];
        if (quote != 0) {
            if (c == quote) {
                quote = 0;
            } else if (c == '\\' && quote == '"' && i + 1 < command.size()) {
                current += command[++i];
            } else {
                current += c;
            }
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            have = true;
        } else if (c == '\\' && i + 1 < command.size()) {
            current += command[++i];
            have = true;
        } else if (c == ' ' || c == '\t') {
            if (have) out.push_back(std::move(current));
            current.clear();
            have = false;
        } else {
            current += c;
            have = true;
        }
    }
    if (have) out.push_back(std::move(current));
    return out;
}

std::string find_executable(std::string_view name) {
    if (name.contains('/')) {
        return access(std::string(name).c_str(), X_OK) == 0 ? std::string(name) : std::string();
    }
    const char* path = std::getenv("PATH");
    if (path == nullptr) return {};
    for (const auto dir : str::split(path, ':')) {
        if (dir.empty()) continue;
        const auto candidate = std::filesystem::path(dir) / name;
        if (access(candidate.c_str(), X_OK) == 0) return candidate.string();
    }
    return {};
}

std::vector<std::string> resolve_editor(std::string_view configured) {
    if (!str::trim(configured).empty()) return split_command(configured);
    // leetvim is about solving in vim, so a vi-family editor wins over $EDITOR; set
    // `editor` in the config to use something else.
    for (const char* name : {"nvim", "vim", "vi"}) {
        if (auto exe = find_executable(name); !exe.empty()) return {exe};
    }
    for (const char* var : {"VISUAL", "EDITOR"}) {
        if (const char* value = std::getenv(var); value != nullptr && *value != '\0') {
            return split_command(value);
        }
    }
    return {};
}

bool is_vim_like(const std::vector<std::string>& argv) {
    if (argv.empty()) return false;
    const std::string name = std::filesystem::path(argv.front()).filename().string();
    static constexpr std::array<std::string_view, 6> vims{"nvim", "vim",       "vi",
                                                          "nvi",  "vim.basic", "vim.tiny"};
    return std::ranges::contains(vims, name);
}

} // namespace lv::core
