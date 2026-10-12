#include "util/log.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>

namespace lv::log {

namespace {

std::mutex& log_mutex() {
    static std::mutex m;
    return m;
}

} // namespace

bool enabled() {
    static const bool on = std::getenv("LEETVIM_LOG") != nullptr;
    return on;
}

void write(std::string_view message) {
    const char* path = std::getenv("LEETVIM_LOG");
    if (path == nullptr) return;
    const std::scoped_lock lock(log_mutex());
    std::ofstream out(path, std::ios::app);
    const auto now =
        std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    out << std::format("{:%T} {}\n", now, message);
}

} // namespace lv::log
