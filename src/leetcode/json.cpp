#include "leetcode/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <limits>

namespace lv::leetcode::json {

Json parse(std::string_view text) { return Json::parse(text, nullptr, false); }

const Json* find(const Json& j, const char* key) {
    if (!j.is_object()) {
        return nullptr;
    }
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) {
        return nullptr;
    }
    return &*it;
}

std::string to_text(const Json& v) {
    if (v.is_string()) {
        return v.get_ref<const std::string&>();
    }
    if (v.is_number_integer()) {
        return std::to_string(v.get<std::int64_t>());
    }
    if (v.is_number_unsigned()) {
        return std::to_string(v.get<std::uint64_t>());
    }
    if (v.is_number_float()) {
        return std::format("{}", v.get<double>());
    }
    return {};
}

std::optional<std::int64_t> to_int(const Json& v) {
    if (v.is_number_integer()) {
        return v.get<std::int64_t>();
    }
    if (v.is_number_unsigned()) {
        const auto u = v.get<std::uint64_t>();
        if (u > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(u);
    }
    if (v.is_number_float()) {
        const double d = v.get<double>();
        if (!std::isfinite(d) || std::abs(d) > 9e18) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(d);
    }
    if (v.is_boolean()) {
        return v.get<bool>() ? 1 : 0;
    }
    if (v.is_string()) {
        const auto& s = v.get_ref<const std::string&>();
        std::int64_t out = 0;
        const char* begin = s.data();
        const char* end = begin + s.size();
        while (begin != end && *begin == ' ') {
            ++begin;
        }
        const auto [ptr, ec] = std::from_chars(begin, end, out);
        if (ec != std::errc{} || ptr == begin) {
            return std::nullopt;
        }
        return out;
    }
    return std::nullopt;
}

std::optional<double> to_double(const Json& v) {
    if (v.is_number()) {
        return v.get<double>();
    }
    if (v.is_string()) {
        const auto& s = v.get_ref<const std::string&>();
        char* end = nullptr;
        const double d = std::strtod(s.c_str(), &end);
        if (end == s.c_str() || !std::isfinite(d)) {
            return std::nullopt;
        }
        return d;
    }
    return std::nullopt;
}

std::string get_string(const Json& j, const char* key) {
    const Json* v = find(j, key);
    return v != nullptr ? to_text(*v) : std::string{};
}

std::int64_t get_i64(const Json& j, const char* key, std::int64_t fallback) {
    const Json* v = find(j, key);
    if (v == nullptr) {
        return fallback;
    }
    return to_int(*v).value_or(fallback);
}

int get_int(const Json& j, const char* key, int fallback) {
    const std::int64_t v = get_i64(j, key, fallback);
    if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) {
        return fallback;
    }
    return static_cast<int>(v);
}

std::optional<double> get_double(const Json& j, const char* key) {
    const Json* v = find(j, key);
    return v != nullptr ? to_double(*v) : std::nullopt;
}

bool get_bool(const Json& j, const char* key) {
    const Json* v = find(j, key);
    if (v == nullptr) {
        return false;
    }
    if (v->is_boolean()) {
        return v->get<bool>();
    }
    if (v->is_string()) {
        const auto& s = v->get_ref<const std::string&>();
        return s == "true" || s == "True" || s == "1";
    }
    return to_int(*v).value_or(0) != 0;
}

std::vector<std::string> get_strings(const Json& j, const char* key) {
    std::vector<std::string> out;
    const Json* v = find(j, key);
    if (v == nullptr || !v->is_array()) {
        return out;
    }
    out.reserve(v->size());
    for (const auto& item : *v) {
        out.push_back(to_text(item));
    }
    return out;
}

Json get_nested(const Json& j, const char* key) {
    const Json* v = find(j, key);
    if (v == nullptr) {
        return nullptr;
    }
    if (!v->is_string()) {
        return *v;
    }
    Json parsed = parse(v->get_ref<const std::string&>());
    if (parsed.is_discarded()) {
        return nullptr;
    }
    return parsed;
}

} // namespace lv::leetcode::json
