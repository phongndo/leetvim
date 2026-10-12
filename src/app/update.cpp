#include "app/update.hpp"

#include "util/paths.hpp"
#include "util/process.hpp"

#include <nlohmann/json.hpp>

#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
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

constexpr std::string_view mise_tool = "github:phongndo/leetvim";
constexpr std::string_view docs =
    "https://github.com/phongndo/leetvim/blob/main/docs/installation.md";

int fail(std::string_view message) {
    std::println(stderr, "leetvim: {}", message);
    return 1;
}

/// Replaces this process with argv[0] (searched on PATH unless it has a slash).
int run(const std::vector<std::string>& argv) {
    std::string line;
    for (const auto& arg : argv) line += (line.empty() ? "" : " ") + arg;
    std::println(stderr, "Updating with: {}", line);
    std::vector<std::string> args = argv;
    std::vector<char*> ptrs;
    ptrs.reserve(args.size() + 1);
    for (auto& a : args) ptrs.push_back(a.data());
    ptrs.push_back(nullptr);
    execvp(ptrs[0], ptrs.data());
    return fail(std::format("could not run {}: {}. Run it yourself: {}", argv[0],
                            std::strerror(errno), line));
}

/// `v0.2.0` and `0.2.0` name the same release.
std::string_view bare(std::string_view version) {
    return version.starts_with('v') ? version.substr(1) : version;
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

Origin classify(const fs::path& executable, std::string_view mise_data_dir) {
    using enum Origin::Method;
    const std::vector<fs::path> parts(executable.begin(), executable.end());
    // <prefix>/Cellar/<formula>/<version>/...
    for (std::size_t i = 0; i + 2 < parts.size(); ++i) {
        if (parts[i] != "Cellar") continue;
        fs::path prefix;
        for (std::size_t j = 0; j < i; ++j) prefix /= parts[j];
        return {.method = homebrew, .brew = prefix / "bin" / "brew", .formula = parts[i + 1]};
    }
    const std::string path = executable.string();
    const bool in_mise =
        mise_data_dir.empty()
            ? path.contains("/mise/installs/")
            : path.starts_with((fs::path(mise_data_dir) / "installs").string() + "/");
    if (in_mise) return {.method = mise};
    if (path.starts_with("/nix/store/") && parts.size() > 3) {
        return {.method = nix, .store_dir = (parts[0] / parts[1] / parts[2] / parts[3]).string()};
    }
    return {.method = other};
}

std::optional<std::string> nix_profile_element(std::string_view json, std::string_view store_dir) {
    const auto listing = nlohmann::json::parse(json, nullptr, false);
    if (listing.is_discarded() || !listing.contains("elements") || !listing["elements"].is_object())
        return std::nullopt;
    for (const auto& [name, element] : listing["elements"].items()) {
        const auto paths = element.find("storePaths");
        if (paths == element.end() || !paths->is_array()) continue;
        for (const auto& store_path : *paths) {
            if (store_path.is_string() && store_path.get<std::string>() == store_dir) return name;
        }
    }
    return std::nullopt;
}

int cmd_update(const std::string& version) {
    const fs::path executable = executable_path();
    const fs::path share = executable.parent_path().parent_path() / "share" / "leetvim";
    std::string receipt;
    const auto installation =
        !executable.empty() && paths::read_file(share / "installation", receipt)
            ? parse_installation(receipt)
            : std::nullopt;
    if (installation) {
        // Rerun the install script that came with this release, in the same directories.
        const std::string script = (share / "install.sh").string();
        const std::string current = std::string("v") + LEETVIM_VERSION;
        setenv("LEETVIM_INSTALL_DIR", installation->install_dir.c_str(), 1);
        setenv("LEETVIM_DATA_DIR", installation->data_dir.c_str(), 1);
        setenv("LEETVIM_VERSION", version.empty() ? "latest" : version.c_str(), 1);
        setenv("LEETVIM_CURRENT_RELEASE", current.c_str(), 1);
        execl("/bin/sh", "sh", script.c_str(), static_cast<char*>(nullptr));
        return fail(std::format("cannot run {}: {}", script, std::strerror(errno)));
    }

    // Resolved like the executable's path, so a symlinked data directory still matches.
    std::string mise_data_dir;
    if (const char* dir = std::getenv("MISE_DATA_DIR"); dir != nullptr && *dir != '\0') {
        std::error_code ec;
        const auto resolved = fs::weakly_canonical(dir, ec);
        mise_data_dir = ec ? std::string(dir) : resolved.string();
    }
    const Origin origin = classify(executable, mise_data_dir);
    switch (origin.method) {
    case Origin::Method::homebrew: {
        if (!version.empty()) {
            return fail("Homebrew installs only the latest release; run `leetvim update` "
                        "without a version.");
        }
        const bool own_brew = access(origin.brew.c_str(), X_OK) == 0;
        return run({own_brew ? origin.brew.string() : "brew", "upgrade", origin.formula});
    }
    case Origin::Method::mise:
        if (!version.empty()) {
            return fail(std::format("Pin a version with mise instead: mise use -g {}@{}", mise_tool,
                                    bare(version)));
        }
        return run({"mise", "upgrade", std::string(mise_tool)});
    case Origin::Method::nix: {
        const auto listing = process::capture({"nix", "profile", "list", "--json"});
        const auto element =
            listing ? nix_profile_element(*listing, origin.store_dir) : std::nullopt;
        if (!element) {
            return fail("This leetvim comes from a Nix configuration or `nix run`; update its "
                        "flake input (nix flake update leetvim) and rebuild.");
        }
        if (!version.empty()) {
            return fail(std::format("Pin a version with Nix instead: nix profile remove {} && "
                                    "nix profile add github:phongndo/leetvim/v{}",
                                    *element, bare(version)));
        }
        return run({"nix", "profile", "upgrade", *element});
    }
    case Origin::Method::other: break;
    }
    return fail(std::format("`leetvim update` can only update a leetvim installed by Homebrew, the "
                            "install script, mise or Nix. Rebuild this one from source, or "
                            "reinstall it with one of those: {}",
                            docs));
}

} // namespace lv::app
