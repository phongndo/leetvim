#pragma once

#include "core/languages.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace lv::core {

/// What LeetCode defines for a problem that its snippet shows only in comments, uncommented
/// so an LSP knows it: the types after a "Definition for ..." line (ListNode, TreeNode, Node,
/// ...), and where the language has a Language::declare_api, the declarations in an API's
/// description (isBadVersion, MountainArray, NestedInteger, ...) without its prose. "" if the
/// snippet has none.
std::string provided_definitions(std::string_view snippet, const Language& lang);

/// How each language declares an API from LeetCode's description of it, for
/// Language::declare_api. A description shows the API as code among prose, written as the
/// language would declare it, more or less: Java's interfaces have method bodies, Go's
/// functions have empty ones, Rust's leave out `fn`. These keep the code, give each function
/// a body that compiles, and declare the class the solution inherits the API from where
/// LeetCode only names it (`class Solution extends VersionControl`). The languages left out
/// keep the description a comment: C#'s and Scala's servers need a build leetvim doesn't
/// make, JavaScript's and Ruby's don't report undefined names, and Kotlin's and Swift's
/// haven't been tried.
namespace api {
std::vector<std::string> c(ApiDescription description, std::string_view snippet);
std::vector<std::string> cpp(ApiDescription description, std::string_view snippet);
std::vector<std::string> golang(ApiDescription description, std::string_view snippet);
std::vector<std::string> java(ApiDescription description, std::string_view snippet);
std::vector<std::string> php(ApiDescription description, std::string_view snippet);
std::vector<std::string> python(ApiDescription description, std::string_view snippet);
std::vector<std::string> rust(ApiDescription description, std::string_view snippet);
std::vector<std::string> typescript(ApiDescription description, std::string_view snippet);
} // namespace api

} // namespace lv::core
