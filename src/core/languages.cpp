#include "core/languages.hpp"

#include "util/strings.hpp"

#include <array>
#include <utility>

namespace lv::core {

namespace {

// Order matters for language_for_extension: the first match for an extension wins.
constexpr std::array table = std::to_array<Language>({
    {"cpp", "C++", "cpp", "//"},
    {"java", "Java", "java", "//"},
    {"python3", "Python3", "py", "#"},
    {"python", "Python", "py", "#"},
    {"c", "C", "c", "//"},
    {"csharp", "C#", "cs", "//"},
    {"javascript", "JavaScript", "js", "//"},
    {"typescript", "TypeScript", "ts", "//"},
    {"php", "PHP", "php", "//"},
    {"swift", "Swift", "swift", "//"},
    {"kotlin", "Kotlin", "kt", "//"},
    {"dart", "Dart", "dart", "//"},
    {"golang", "Go", "go", "//"},
    {"ruby", "Ruby", "rb", "#"},
    {"scala", "Scala", "scala", "//"},
    {"rust", "Rust", "rs", "//"},
    {"racket", "Racket", "rkt", ";"},
    {"erlang", "Erlang", "erl", "%"},
    {"elixir", "Elixir", "ex", "#"},
    {"mysql", "MySQL", "sql", "--"},
    {"mssql", "MS SQL Server", "sql", "--"},
    {"oraclesql", "Oracle", "sql", "--"},
    {"postgresql", "PostgreSQL", "sql", "--"},
    {"pythondata", "Pandas", "py", "#"},
    {"bash", "Bash", "sh", "#"},
});

constexpr std::array aliases = std::to_array<std::pair<std::string_view, std::string_view>>({
    {"c++", "cpp"},
    {"cxx", "cpp"},
    {"py", "python3"},
    {"py3", "python3"},
    {"python2", "python"},
    {"cs", "csharp"},
    {"c#", "csharp"},
    {"js", "javascript"},
    {"node", "javascript"},
    {"ts", "typescript"},
    {"go", "golang"},
    {"kt", "kotlin"},
    {"rb", "ruby"},
    {"rs", "rust"},
    {"sql", "mysql"},
    {"postgres", "postgresql"},
    {"pandas", "pythondata"},
    {"sh", "bash"},
});

} // namespace

std::span<const Language> languages() { return table; }

const Language* find_language(std::string_view slug_or_alias) {
    const std::string key = str::to_lower(str::trim(slug_or_alias));
    std::string_view slug = key;
    for (const auto& [alias, target] : aliases) {
        if (alias == key) slug = target;
    }
    for (const auto& lang : table) {
        if (lang.slug == slug) return &lang;
    }
    return nullptr;
}

const Language* language_for_extension(std::string_view ext) {
    if (ext.starts_with('.')) ext.remove_prefix(1);
    for (const auto& lang : table) {
        if (lang.ext == ext) return &lang;
    }
    return nullptr;
}

} // namespace lv::core
