#pragma once

#include <format>
#include <string_view>

namespace lv::log {

/// True when LEETVIM_LOG names a file to append debug messages to.
bool enabled();
void write(std::string_view message);

template <typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled()) write(std::format(fmt, std::forward<Args>(args)...));
}

} // namespace lv::log
