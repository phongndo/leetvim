#include "util/paths.hpp"

#include <pwd.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace lv::paths {

namespace fs = std::filesystem;

namespace {

fs::path xdg(const char* var, std::string_view fallback) {
    if (const char* value = std::getenv(var); value != nullptr && *value == '/') {
        return fs::path(value) / "leetvim";
    }
    return home() / fallback / "leetvim";
}

} // namespace

fs::path home() {
    if (const char* h = std::getenv("HOME"); h != nullptr && *h != '\0') return h;
    if (const passwd* pw = getpwuid(getuid()); pw != nullptr && pw->pw_dir != nullptr) {
        return pw->pw_dir;
    }
    return "/";
}

fs::path expand_user(std::string_view path) {
    if (path == "~") return home();
    if (path.starts_with("~/")) return home() / path.substr(2);
    return {path};
}

fs::path config_dir() { return xdg("XDG_CONFIG_HOME", ".config"); }
fs::path cache_dir() { return xdg("XDG_CACHE_HOME", ".cache"); }
fs::path data_dir() { return xdg("XDG_DATA_HOME", ".local/share"); }
fs::path state_dir() { return xdg("XDG_STATE_HOME", ".local/state"); }

bool read_file(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = std::move(buffer).str();
    return true;
}

bool write_file(const fs::path& path, std::string_view contents, fs::perms mode) {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    fs::path tmp = path;
    tmp += ".tmp" + std::to_string(getpid());
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        fs::permissions(tmp, mode, ec);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!out) return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace lv::paths
