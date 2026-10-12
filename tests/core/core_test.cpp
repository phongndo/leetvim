#include "core/config.hpp"
#include "core/editor.hpp"
#include "core/languages.hpp"
#include "core/workspace.hpp"
#include "util/paths.hpp"

#include <doctest/doctest.h>

#include <unistd.h>

#include <filesystem>

using namespace lv;
using namespace lv::core;

namespace {

leetcode::Question two_sum() {
    leetcode::Question q;
    q.question_id = "1";
    q.frontend_id = "1";
    q.title = "Two Sum";
    q.slug = "two-sum";
    q.difficulty = leetcode::Difficulty::Easy;
    q.snippets = {{.lang = "C++", .lang_slug = "cpp", .code = "class Solution {\n};"},
                  {.lang = "Python3", .lang_slug = "python3", .code = "class Solution:\n    pass"}};
    q.example_testcases = {"[2,7,11,15]\n9", "[3,2,4]\n6"};
    q.meta.function_name = "twoSum";
    q.meta.param_names = {"nums", "target"};
    return q;
}

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        std::string tmpl =
            (std::filesystem::temp_directory_path() / "leetvim-test-XXXXXX").string();
        path = mkdtemp(tmpl.data());
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;
};

} // namespace

TEST_CASE("language lookup") {
    REQUIRE(find_language("cpp") != nullptr);
    CHECK(find_language("C++")->slug == "cpp");
    CHECK(find_language("py")->slug == "python3");
    CHECK(find_language("go")->ext == "go");
    CHECK(find_language("nope") == nullptr);
    CHECK(language_for_extension("py")->slug == "python3");
    CHECK(language_for_extension(".rs")->slug == "rust");
    CHECK(language_for_extension("sql")->slug == "mysql");
}

TEST_CASE("config parsing") {
    const auto loaded = parse_config(R"(
# comment
lang = "python3"   # trailing comment
theme = 'gruvbox'
mouse = false
split = 0.5
refresh_hours = 6
bogus = 1
split2

[inject_before]
cpp = """
#include <bits/stdc++.h>
using namespace std;
"""
python3 = "from typing import *"
)");
    const auto& c = loaded.config;
    CHECK(c.lang == "python3");
    CHECK(c.theme == "gruvbox");
    CHECK_FALSE(c.mouse);
    CHECK(c.split == doctest::Approx(0.5));
    CHECK(c.refresh_hours == 6);
    CHECK(c.inject_before.at("cpp") == "#include <bits/stdc++.h>\nusing namespace std;\n");
    CHECK(c.inject_before.at("python3") == "from typing import *");
    REQUIRE(loaded.warnings.size() == 2);
    CHECK(loaded.warnings[0].find("bogus") != std::string::npos);

    CHECK(parse_config(default_config_text()).warnings.empty());
    Config cfg;
    CHECK(set_config_value(cfg, "split", "2").find("split") != std::string::npos);
    CHECK(set_config_value(cfg, "lang", "rust").empty());
    CHECK(cfg.lang == "rust");
}

TEST_CASE("config file updates keep comments") {
    const TempDir dir;
    const auto path = dir.path / "config.toml";
    REQUIRE(paths::write_file(path, "# hello\nlang = \"cpp\"\n\n[inject_before]\ncpp = \"x\"\n"));
    REQUIRE(update_config_file(path, "lang", toml_quote("rust")));
    REQUIRE(update_config_file(path, "theme", toml_quote("nord")));
    std::string text;
    REQUIRE(paths::read_file(path, text));
    CHECK(text.find("# hello") != std::string::npos);
    CHECK(text.find("lang = \"rust\"") != std::string::npos);
    CHECK(text.find("theme = \"nord\"") < text.find("[inject_before]"));
    const auto loaded = parse_config(text);
    CHECK(loaded.config.lang == "rust");
    CHECK(loaded.config.theme == "nord");
    CHECK(loaded.config.inject_before.at("cpp") == "x");
}

TEST_CASE("solution files round trip") {
    const auto q = two_sum();
    const auto* cpp = find_language("cpp");
    const std::string file = render_solution(q, *cpp, "https://leetcode.com/problems/two-sum/",
                                             "#include <vector>", "int main() {}");
    CHECK(file.starts_with("// @leetvim id=1 slug=two-sum lang=cpp\n"));
    CHECK(extract_code(file) == "class Solution {\n};\n");
    const auto meta = parse_header(file);
    REQUIRE(meta.has_value());
    CHECK(meta->slug == "two-sum");
    CHECK(meta->lang == "cpp");

    CHECK(extract_code("no markers") == "no markers");
    CHECK(extract_code("# @leet start\nx = 1\n# @leet end\n") == "x = 1\n");
    CHECK(extract_code("// @lc code=start\nonly start") == "only start");
}

TEST_CASE("filenames and testcases") {
    const auto meta = parse_filename("/x/42.trapping-rain-water.py");
    REQUIRE(meta.has_value());
    CHECK(meta->frontend_id == "42");
    CHECK(meta->slug == "trapping-rain-water");
    CHECK(meta->lang == "python3");
    CHECK_FALSE(parse_filename("README").has_value());

    const auto cases = group_testcases("[1]\n2\n[3]\n4\n\n", 2);
    REQUIRE(cases.size() == 2);
    CHECK(cases[1] == "[3]\n4");
    CHECK(group_testcases("{\"a\":1}\n", 0).size() == 1);
}

TEST_CASE("workspace creates files once") {
    const TempDir dir;
    const Workspace ws(dir.path);
    const auto q = two_sum();
    const auto* py = find_language("python3");
    const auto path = ws.ensure_solution(q, *py, "url");
    REQUIRE(path.has_value());
    CHECK(path->filename() == "1.two-sum.py");
    REQUIRE(paths::write_file(*path, "edited"));
    CHECK(ws.ensure_solution(q, *py, "url").has_value());
    std::string text;
    REQUIRE(paths::read_file(*path, text));
    CHECK(text == "edited");
    CHECK_FALSE(ws.ensure_solution(q, *find_language("rust"), "url").has_value());

    CHECK(ws.testcases(q).size() == 2);
    const auto tc = ws.ensure_testcases(q);
    REQUIRE(tc.has_value());
    REQUIRE(paths::write_file(*tc, "[1,2]\n3\n[4,5]\n9\n[6,7]\n13\n"));
    CHECK(ws.testcases(q).size() == 3);
    CHECK(ws.solutions_for("two-sum").size() == 1);
}

TEST_CASE("editor commands") {
    CHECK(split_command("nvim -u 'my init.lua' --clean")
          == std::vector<std::string>{"nvim", "-u", "my init.lua", "--clean"});
    CHECK(split_command(R"(vim "a b" c\ d)") == std::vector<std::string>{"vim", "a b", "c d"});
    CHECK(is_vim_like({"/usr/bin/nvim"}));
    CHECK_FALSE(is_vim_like({"nano"}));
    CHECK(resolve_editor("hx --flag") == std::vector<std::string>{"hx", "--flag"});
    CHECK_FALSE(find_executable("sh").empty());
}
