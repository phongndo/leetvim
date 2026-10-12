#pragma once

#include "util/paths.hpp"

#include <unistd.h> // mkdtemp on macOS (stdlib.h on Linux)

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace lv::test {

inline std::string fixture(std::string_view name) {
    std::string text;
    paths::read_file(std::filesystem::path(LEETVIM_FIXTURES) / name, text);
    return text;
}

/// A fresh directory under the system temp dir, removed with its contents on destruction.
class TempDir {
public:
    TempDir() {
        std::string path =
            (std::filesystem::temp_directory_path() / "leetvim-test-XXXXXX").string();
        if (mkdtemp(path.data()) != nullptr) path_ = path;
    }
    ~TempDir() {
        std::error_code ec;
        if (!path_.empty()) std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

/// Sets (or, with nullptr, unsets) an environment variable for the current scope.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name) {
        if (const char* old = std::getenv(name)) old_ = old;
        if (value != nullptr) {
            setenv(name, value, 1);
        } else {
            unsetenv(name);
        }
    }
    ~ScopedEnv() {
        if (old_) {
            setenv(name_, old_->c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;
    ScopedEnv(ScopedEnv&&) = delete;
    ScopedEnv& operator=(ScopedEnv&&) = delete;

private:
    const char* name_;
    std::optional<std::string> old_;
};

} // namespace lv::test
