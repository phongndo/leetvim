#include "util/strings.hpp"

#include <algorithm>
#include <cctype>
#include <format>

namespace lv::str {

namespace {

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

} // namespace

std::string_view trim(std::string_view s) {
    const auto is_space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = s.find(sep, start);
        if (end == std::string_view::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, end - start));
        start = end + 1;
    }
}

std::vector<std::string_view> lines(std::string_view s) {
    auto out = split(s, '\n');
    for (auto& line : out) {
        if (line.ends_with('\r')) line.remove_suffix(1);
    }
    return out;
}

std::string join(std::span<const std::string> parts, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) out.append(sep);
        out.append(parts[i]);
    }
    return out;
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    std::ranges::transform(out, out.begin(), lower);
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](char x, char y) { return lower(x) == lower(y); });
}

bool icontains(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    const auto result =
        std::ranges::search(haystack, needle, [](char x, char y) { return lower(x) == lower(y); });
    return !result.empty();
}

std::string replace_all(std::string_view s, std::string_view from, std::string_view to) {
    if (from.empty()) return std::string(s);
    std::string out;
    std::size_t start = 0;
    while (true) {
        const std::size_t hit = s.find(from, start);
        if (hit == std::string_view::npos) break;
        out.append(s.substr(start, hit - start));
        out.append(to);
        start = hit + from.size();
    }
    out.append(s.substr(start));
    return out;
}

std::string human_count(std::int64_t n) {
    const auto abs = n < 0 ? -n : n;
    if (abs >= 1'000'000'000) return std::format("{:.1f}B", static_cast<double>(n) / 1e9);
    if (abs >= 1'000'000) return std::format("{:.1f}M", static_cast<double>(n) / 1e6);
    if (abs >= 10'000) return std::format("{:.1f}K", static_cast<double>(n) / 1e3);
    return std::to_string(n);
}

std::string percent(double value) { return std::format("{:.1f}%", value); }

} // namespace lv::str
