// What leetvim uncomments from LeetCode's starter code, and the heads of real problems'
// solution files against golden files.
#include "core/definitions.hpp"
#include "core/languages.hpp"
#include "core/workspace.hpp"
#include "leetcode/parse.hpp"
#include "util/paths.hpp"
#include "util/strings.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

using namespace lv;
using namespace lv::core;

namespace {

/// Where `actual` first differs from `expected`, for a failure that says.
std::string first_difference(std::string_view actual, std::string_view expected) {
    const auto got = str::lines(actual);
    const auto want = str::lines(expected);
    for (std::size_t i = 0; i < std::max(got.size(), want.size()); ++i) {
        const auto a = i < got.size() ? got[i] : "(end)";
        const auto b = i < want.size() ? want[i] : "(end)";
        if (a != b) return std::format(R"(line {}: got "{}", want "{}")", i + 1, a, b);
    }
    return "";
}

} // namespace

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

TEST_CASE("the APIs a snippet describes are declared without the prose") {
    const auto& cpp = *find_language("cpp");
    CHECK(provided_definitions("// The API isBadVersion is defined for you.\n"
                               "// bool isBadVersion(int version);\n\nclass Solution {};",
                               cpp)
          == "// The API isBadVersion is defined for you.\nbool isBadVersion(int version);\n");
    CHECK(provided_definitions(R"(/**
 * Forward declaration of guess API.
 * @param  num   your guess
 * @return 	     -1 if num is higher than the picked number
 *               otherwise return 0
 * int guess(int num);
 */)",
                               cpp)
          == "// Forward declaration of guess API.\nint guess(int num);\n");
    CHECK(provided_definitions(R"(/**
 * // This is the MountainArray's API interface.
 * // You should not implement it, or speculate about its implementation
 * class MountainArray {
 *   public:
 *     int get(int index);
 * };
 */)",
                               cpp)
          == R"(// This is the MountainArray's API interface.
// You should not implement it, or speculate about its implementation
class MountainArray {
  public:
    int get(int index);
};
)");
    // Comments in the code may mention an API in passing.
    CHECK(provided_definitions("class PeekingIterator : public Iterator {\n"
                               "    // As in the Iterator interface:\n    // int next();\n};",
                               cpp)
              .empty());

    // C's structs are left undeclared, and LeetCode's NestedInteger closes a brace it never
    // opened.
    CHECK(provided_definitions(R"(/**
 * *********************************************************************
 * // This is the interface that allows for creating nested lists.
 * *********************************************************************
 *
 * // Return true if this NestedInteger holds a single integer.
 * bool NestedIntegerIsInteger(struct NestedInteger *);
 * int get(MountainArray *, int index);
 * };
 */)",
                               *find_language("c"))
          == R"(// This is the interface that allows for creating nested lists.
typedef struct NestedInteger NestedInteger;
typedef struct MountainArray MountainArray;
// Return true if this NestedInteger holds a single integer.
bool NestedIntegerIsInteger(struct NestedInteger *);
int get(MountainArray *, int index);
)");
    // Prose alone declares nothing.
    CHECK(provided_definitions("/*\n * // This is the definition for customFunction API.\n"
                               " * // Returns f(x, y) for any given positive integers.\n */",
                               *find_language("c"))
              .empty());

    const auto& py = *find_language("python3");
    CHECK(provided_definitions("# The guess API is already defined for you.\n"
                               "# @param num, your guess\n#          otherwise return 0\n"
                               "# def guess(num: int) -> int:\n\nclass Solution:\n    pass",
                               py)
          == "# The guess API is already defined for you.\ndef guess(num: int) -> int:\n    raise "
             "NotImplementedError\n");
    CHECK(provided_definitions(R"(# """
# This is the interface that allows for creating nested lists.
# """
#class NestedInteger:
#    def __init__(self, value=None):
#        """
#        Initializes a single integer equal to value.
#        """
#
#    def getList(self) -> [NestedInteger]:
#    def getInteger(self):
)",
                               py)
          == R"(# This is the interface that allows for creating nested lists.
class NestedInteger:
    def __init__(self, value=None):
        """
        Initializes a single integer equal to value.
        """
        raise NotImplementedError

    def getList(self) -> "list[NestedInteger]":
        raise NotImplementedError
    def getInteger(self) -> Any:
        raise NotImplementedError
)");
    CHECK(provided_definitions(R"("""
   This is the custom function interface.
   You should not implement it, or speculate about its implementation
   class CustomFunction:
       # Returns f(x, y) for any given positive integers x and y.
       def f(self, x, y):
"""
class Solution:
    pass)",
                               py)
          == R"(# This is the custom function interface.
class CustomFunction:
    # Returns f(x, y) for any given positive integers x and y.
    def f(self, x, y) -> Any:
        raise NotImplementedError
)");

    // A language without declarations keeps the description a comment, as JavaScript's
    // "Definition for isBadVersion()" is: a function's description, not the code of a type.
    CHECK(provided_definitions("/**\n * Definition for isBadVersion()\n *\n"
                               " * @param {integer} version number\n"
                               " * isBadVersion = function(version) {\n *     ...\n * };\n */",
                               *find_language("javascript"))
              .empty());
    CHECK(provided_definitions("// The API isBadVersion is defined for you.\n"
                               "// bool isBadVersion(int version);",
                               *find_language("dart"))
              .empty());
}

TEST_CASE("an API comes from the class the solution extends") {
    // LeetCode names the class only after `extends`, with the API's functions in the comment.
    const auto java = provided_definitions(
        "/* The isBadVersion API is defined in the parent class VersionControl.\n"
        "      boolean isBadVersion(int version); */\n\n"
        "public class Solution extends VersionControl {\n}",
        *find_language("java"));
    CHECK(java
          == "// The isBadVersion API is defined in the parent class VersionControl.\n"
             "class VersionControl {\n"
             "    boolean isBadVersion(int version) { throw new "
             "UnsupportedOperationException(); }\n}\n");
    CHECK(provided_definitions("/* The isBadVersion API is defined in the parent class "
                               "VersionControl.\n      public function isBadVersion($version){} "
                               "*/\n\nclass Solution extends VersionControl {\n}",
                               *find_language("php"))
          == "// The isBadVersion API is defined in the parent class VersionControl.\n"
             "class VersionControl {\n    public function isBadVersion($version) {}\n}\n");
    // An interface whose objects a solution creates is a class.
    CHECK(provided_definitions("/**\n * // This is the interface that allows for creating "
                               "nested lists.\n * public interface NestedInteger {\n"
                               " *     public boolean isInteger();\n * }\n */",
                               *find_language("java"))
          == "// This is the interface that allows for creating nested lists.\n"
             "class NestedInteger {\n"
             "    public boolean isInteger() { throw new UnsupportedOperationException(); }\n"
             "}\n");
}

TEST_CASE("real problems' heads match their golden files") {
    // tests/fixtures/snippets holds LeetCode's data for problems whose starter code describes
    // APIs or types; tests/fixtures/heads, the head of each language's solution file for each.
    // LEETVIM_UPDATE_GOLDEN=1 rewrites the golden files.
    namespace fs = std::filesystem;
    const fs::path fixtures(LEETVIM_FIXTURES);
    const bool update = std::getenv("LEETVIM_UPDATE_GOLDEN") != nullptr;
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(fixtures / "snippets")) {
        files.push_back(entry.path());
    }
    std::ranges::sort(files);
    REQUIRE_FALSE(files.empty());
    for (const auto& file : files) {
        std::string body;
        REQUIRE(paths::read_file(file, body));
        const auto q = leetcode::parse_question(body);
        REQUIRE(q.has_value());
        std::string heads;
        for (const auto& snippet : q->snippets) {
            const auto* lang = find_language(snippet.lang_slug);
            REQUIRE(lang != nullptr);
            const auto head =
                render_head(*q, *lang, std::format("https://leetcode.com/problems/{}/", q->slug),
                            lang->prelude);
            heads += std::format("=== {}\n{}", lang->slug, head);
        }
        const auto golden = fixtures / "heads" / file.filename().replace_extension(".txt");
        std::string expected;
        const bool read = paths::read_file(golden, expected);
        if (update) {
            if (!read || expected != heads) REQUIRE(paths::write_file(golden, heads));
            continue;
        }
        REQUIRE_MESSAGE(read, golden.string());
        const auto where = golden.filename().string() + ": " + first_difference(heads, expected);
        INFO(where);
        CHECK(heads == expected);
    }
}
