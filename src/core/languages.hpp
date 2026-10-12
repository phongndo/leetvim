#pragma once

#include <span>
#include <string_view>

namespace lv::core {

struct Language {
    std::string_view slug;    // LeetCode's langSlug: "cpp", "python3", ...
    std::string_view name;    // "C++"
    std::string_view ext;     // file extension without the dot
    std::string_view comment; // line comment prefix
};

std::span<const Language> languages();
/// Looks up a language by slug or common alias ("c++", "py", "go", "rs", "ts", ...).
const Language* find_language(std::string_view slug_or_alias);
/// The preferred language for a file extension ("py" -> python3, "sql" -> mysql).
const Language* language_for_extension(std::string_view ext);

} // namespace lv::core
