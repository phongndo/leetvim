#pragma once

#include "core/languages.hpp"

#include <string>
#include <string_view>

namespace lv::core {

/// The types LeetCode defines for a problem (ListNode, TreeNode, Node, ...), which its
/// snippet shows only in comments after a "Definition for ..." line: that code uncommented,
/// so an LSP knows the types. "" if the snippet has none.
std::string provided_definitions(std::string_view snippet, const Language& lang);

} // namespace lv::core
