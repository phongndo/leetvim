#include "core/languages.hpp"

#include "util/strings.hpp"

#include <array>
#include <utility>

namespace lv::core {

namespace {

// Preludes sit outside the code markers, so they're never submitted. The IWYU pragmas stop
// clangd flagging the headers a solution doesn't use; <bits/stdc++.h> is libstdc++ only.
constexpr std::string_view cpp_prelude = R"(#include <algorithm> // IWYU pragma: keep
#include <array> // IWYU pragma: keep
#include <bit> // IWYU pragma: keep
#include <bitset> // IWYU pragma: keep
#include <cassert> // IWYU pragma: keep
#include <cctype> // IWYU pragma: keep
#include <chrono> // IWYU pragma: keep
#include <climits> // IWYU pragma: keep
#include <cmath> // IWYU pragma: keep
#include <cstdint> // IWYU pragma: keep
#include <cstdlib> // IWYU pragma: keep
#include <cstring> // IWYU pragma: keep
#include <deque> // IWYU pragma: keep
#include <functional> // IWYU pragma: keep
#include <iomanip> // IWYU pragma: keep
#include <iostream> // IWYU pragma: keep
#include <iterator> // IWYU pragma: keep
#include <limits> // IWYU pragma: keep
#include <list> // IWYU pragma: keep
#include <map> // IWYU pragma: keep
#include <memory> // IWYU pragma: keep
#include <numeric> // IWYU pragma: keep
#include <optional> // IWYU pragma: keep
#include <queue> // IWYU pragma: keep
#include <random> // IWYU pragma: keep
#include <ranges> // IWYU pragma: keep
#include <regex> // IWYU pragma: keep
#include <set> // IWYU pragma: keep
#include <sstream> // IWYU pragma: keep
#include <stack> // IWYU pragma: keep
#include <string> // IWYU pragma: keep
#include <string_view> // IWYU pragma: keep
#include <tuple> // IWYU pragma: keep
#include <unordered_map> // IWYU pragma: keep
#include <unordered_set> // IWYU pragma: keep
#include <utility> // IWYU pragma: keep
#include <variant> // IWYU pragma: keep
#include <vector> // IWYU pragma: keep
using namespace std;
)";
constexpr std::string_view c_prelude = R"(#include <ctype.h> // IWYU pragma: keep
#include <limits.h> // IWYU pragma: keep
#include <math.h> // IWYU pragma: keep
#include <stdbool.h> // IWYU pragma: keep
#include <stdint.h> // IWYU pragma: keep
#include <stdio.h> // IWYU pragma: keep
#include <stdlib.h> // IWYU pragma: keep
#include <string.h> // IWYU pragma: keep
)";
constexpr std::string_view java_prelude = "import java.util.*;\n";
constexpr std::string_view csharp_prelude = R"(using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
)";
// typing first, so collections' Counter and OrderedDict win. The star imports are the
// point, so pyright's and ruff's warnings about them are off.
constexpr std::string_view python3_prelude = R"(# pyright: reportWildcardImportFromLibrary=false
# ruff: noqa: F403, F405
from typing import *
from collections import *
from bisect import *
from functools import *
from heapq import *
from itertools import *
import math
)";
// The build tag makes gopls check each file on its own: the workspace keeps every solution
// in one directory, which would otherwise be one package with clashing names.
constexpr std::string_view golang_prelude = "//go:build ignore\n\npackage main\n";

// Order matters for language_for_extension: the first match for an extension wins.
constexpr std::array table = std::to_array<Language>({
    {"cpp", "C++", "cpp", "//", "", cpp_prelude},
    {"java", "Java", "java", "//", "", java_prelude},
    {"python3", "Python3", "py", "#", "", python3_prelude},
    {"python", "Python", "py", "#", "", ""},
    {"c", "C", "c", "//", "", c_prelude},
    {"csharp", "C#", "cs", "//", "", csharp_prelude},
    {"javascript", "JavaScript", "js", "//", "", ""},
    {"typescript", "TypeScript", "ts", "//", "", ""},
    {"php", "PHP", "php", "//", "<?php", ""},
    {"swift", "Swift", "swift", "//", "", ""},
    {"kotlin", "Kotlin", "kt", "//", "", ""},
    {"dart", "Dart", "dart", "//", "", ""},
    {"golang", "Go", "go", "//", "", golang_prelude},
    {"ruby", "Ruby", "rb", "#", "", ""},
    {"scala", "Scala", "scala", "//", "", ""},
    {"rust", "Rust", "rs", "//", "", "struct Solution;\n"},
    {"racket", "Racket", "rkt", ";", "", "#lang racket\n"},
    {"erlang", "Erlang", "erl", "%", "", ""},
    {"elixir", "Elixir", "ex", "#", "", ""},
    {"mysql", "MySQL", "sql", "--", "", ""},
    {"mssql", "MS SQL Server", "sql", "--", "", ""},
    {"oraclesql", "Oracle", "sql", "--", "", ""},
    {"postgresql", "PostgreSQL", "sql", "--", "", ""},
    {"pythondata", "Pandas", "py", "#", "", ""},
    {"bash", "Bash", "sh", "#", "", ""},
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
