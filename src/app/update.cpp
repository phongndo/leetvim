#include "app/update.hpp"

#include "util/paths.hpp"

#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <print>
#include <vector>

namespace lv::app {

namespace fs = std::filesystem;

namespace {

/// The running executable with symlinks resolved, so the release directory rather than the
/// `leetvim` symlink that points into it.
fs::path executable_path() {
    std::error_code ec;
#ifdef __APPLE__
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    auto path = fs::canonical(buffer.data(), ec);
#else
    auto path = fs::canonical("/proc/self/exe", ec);
#endif
    return ec ? fs::path{} : path;
}

} // namespace

std::optional<Installation> parse_installation(std::string_view receipt) {
    Installation found;
    while (!receipt.empty()) {
        const auto end = receipt.find('\n');
        const auto line = receipt.substr(0, end);
        receipt = end == std::string_view::npos ? std::string_view{} : receipt.substr(end + 1);
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const auto key = line.substr(0, eq);
        const auto value = line.substr(eq + 1);
        if (key == "install_dir") found.install_dir = value;
        if (key == "data_dir") found.data_dir = value;
    }
    if (!found.install_dir.starts_with('/') || !found.data_dir.starts_with('/'))
        return std::nullopt;
    return found;
}

int cmd_update(const std::string& version) {
    const fs::path share = executable_path().parent_path().parent_path() / "share" / "leetvim";
    std::string receipt;
    const auto installation = paths::read_file(share / "installation", receipt)
                                  ? parse_installation(receipt)
                                  : std::nullopt;
    if (!installation) {
        std::println(stderr, "leetvim: `leetvim update` updates installs made by the install "
                             "script. Update this one the way you installed it:");
        std::println(stderr, "  brew upgrade leetvim");
        std::println(stderr, "  mise upgrade github:phongndo/leetvim");
        std::println(stderr, "  nix profile upgrade leetvim");
        return 1;
    }
    const std::string script = (share / "install.sh").string();
    const std::string current = std::string("v") + LEETVIM_VERSION;
    setenv("LEETVIM_INSTALL_DIR", installation->install_dir.c_str(), 1);
    setenv("LEETVIM_DATA_DIR", installation->data_dir.c_str(), 1);
    setenv("LEETVIM_VERSION", version.empty() ? "latest" : version.c_str(), 1);
    setenv("LEETVIM_CURRENT_RELEASE", current.c_str(), 1);
    execl("/bin/sh", "sh", script.c_str(), static_cast<char*>(nullptr));
    std::println(stderr, "leetvim: cannot run {}: {}", script, std::strerror(errno));
    return 1;
}

} // namespace lv::app
