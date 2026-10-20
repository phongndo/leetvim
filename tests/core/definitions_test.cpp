// What leetvim uncomments from LeetCode's starter code.
#include "core/definitions.hpp"
#include "core/languages.hpp"

#include <doctest/doctest.h>

using namespace lv;
using namespace lv::core;

TEST_CASE("provided definitions are uncommented from the snippet") {
    const auto& cpp = *find_language("cpp");
    // Two /** */ blocks, as in "Linked List in Binary Tree".
    CHECK(provided_definitions(R"(/**
 * Definition for singly-linked list.
 * struct ListNode {
 *     int val;
 *     ListNode *next;
 * };
 */
/**
 * Definition for a binary tree node.
 * struct TreeNode {
 *     int val;
 * };
 */
class Solution {
public:
    bool isSubPath(ListNode* head, TreeNode* root) {}
};)",
                               cpp)
          == R"(// Definition for singly-linked list.
struct ListNode {
    int val;
    ListNode *next;
};

// Definition for a binary tree node.
struct TreeNode {
    int val;
};
)");
    // A bare /* */ block with a // heading, as in "Clone Graph".
    CHECK(provided_definitions(
              "/*\n// Definition for a Node.\nclass Node {\npublic:\n    int val;\n};\n*/\n\n"
              "class Solution {};",
              cpp)
          == "// Definition for a Node.\nclass Node {\npublic:\n    int val;\n};\n");
    // API declarations aren't definitions LeetCode gives the code of.
    CHECK(provided_definitions("// The API isBadVersion is defined for you.\n"
                               "// bool isBadVersion(int version);\n\nclass Solution {};",
                               cpp)
              .empty());
    CHECK(provided_definitions("class Solution {};", cpp).empty());

    CHECK(provided_definitions(
              "/**\n * Definition for singly-linked list.\n * public class ListNode {\n"
              " *     int val;\n * }\n */\nclass Solution {}",
              *find_language("java"))
          == "// Definition for singly-linked list.\nclass ListNode {\n    int val;\n}\n");
    CHECK(provided_definitions("/**\n * Example:\n * var li = ListNode(5)\n"
                               " * Definition for singly-linked list.\n"
                               " * class ListNode(var `val`: Int) {\n * }\n */\nclass Solution {}",
                               *find_language("kotlin"))
          == "// Definition for singly-linked list.\nclass ListNode(var `val`: Int) {\n}\n");
    CHECK(provided_definitions("// Definition for singly-linked list.\n// pub struct ListNode {\n"
                               "//   pub val: i32,\n// }\n// \n// impl ListNode {}\n"
                               "impl Solution {}",
                               *find_language("rust"))
          == "// Definition for singly-linked list.\npub struct ListNode {\n  pub val: i32,\n}\n\n"
             "impl ListNode {}\n");

    const auto& py = *find_language("python3");
    CHECK(
        provided_definitions("# Definition for singly-linked list.\n# class ListNode:\n"
                             "#     def __init__(self, val=0):\n#         self.val = val\n"
                             "# Definition for a binary tree node.\n# class TreeNode:\n"
                             "#     pass\nclass Solution:\n    pass",
                             py)
        == "# Definition for singly-linked list.\nclass ListNode:\n    def __init__(self, val=0):\n"
           "        self.val = val\n\n# Definition for a binary tree node.\nclass TreeNode:\n"
           "    pass\n");
    CHECK(provided_definitions(
              "\"\"\"\n# Definition for a Node.\nclass Node:\n    pass\n\"\"\"\n\n"
              "class Solution:\n    def f(self):\n        \"\"\"\n        :rtype: Node\n"
              "        \"\"\"",
              py)
          == "# Definition for a Node.\nclass Node:\n    pass\n");
    CHECK(provided_definitions(
              "\"\"\"\n# Definition for a Node.\nclass Node:\n"
              "    def __init__(self, x: int, next: 'Node' = None):\n        pass\n\"\"\"",
              py)
          == "# Definition for a Node.\nclass Node:\n    def __init__(self, x: int, next: 'Node' = "
             "None):"
             "  # pyright: ignore[reportArgumentType]\n        pass\n");
    CHECK(provided_definitions(
              "%% Definition for singly-linked list.\n%%\n%% -record(list_node, {}).\n",
              *find_language("erlang"))
          == "% Definition for singly-linked list.\n-record(list_node, {}).\n");
}
