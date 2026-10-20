#include "core/account.hpp"
#include "core/config.hpp"
#include "core/editor.hpp"
#include "core/languages.hpp"
#include "core/workspace.hpp"
#include "util/paths.hpp"

#include <doctest/doctest.h>

#include <unistd.h>

#include <filesystem>
#include <format>

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
[solution]
lang = "python3"   # trailing comment
workspace = '~/code/leet'
filename = "{lang}/{id}.{slug}.{ext}"

[solution.inject_before]
cpp = """
#include <bits/stdc++.h>
using namespace std;
"""
python3 = "from typing import *"

[keys]
prefix = "ctrl-a"
run = "f5"

[ui]
theme = "gruvbox"
mouse = false
split = 0.5
truecolor = true

[list]
sort = "difficulty"
show_stats = false

[account]
refresh_hours = 6
)");
    CHECK(loaded.warnings.empty());
    const auto& c = loaded.config;
    CHECK(c.solution.lang == "python3");
    CHECK(c.solution.workspace == "~/code/leet");
    CHECK(c.solution.filename == "{lang}/{id}.{slug}.{ext}");
    CHECK(c.solution.inject_before.at("cpp") == "#include <bits/stdc++.h>\nusing namespace std;\n");
    CHECK(c.solution.inject_before.at("python3") == "from typing import *");
    CHECK(c.keys.prefix == "ctrl-a");
    CHECK(c.keys.run == "f5");
    CHECK(c.keys.submit == "s");
    CHECK(c.ui.theme == "gruvbox");
    CHECK_FALSE(c.ui.mouse);
    CHECK(c.ui.split == doctest::Approx(0.5));
    CHECK(c.ui.truecolor == "true");
    CHECK(c.list.sort == "difficulty");
    CHECK_FALSE(c.list.show_stats);
    CHECK(c.account.refresh_hours == 6);
}

TEST_CASE("solution injections fall back to the language's prelude") {
    const auto loaded = parse_config(R"(
[solution.inject_before]
cpp = "#include <bits/stdc++.h>"
python3 = ""
)");
    CHECK(loaded.warnings.empty());
    const auto& s = loaded.config.solution;
    CHECK(s.before("cpp") == "#include <bits/stdc++.h>");
    CHECK(s.before("python3").empty());
    CHECK(s.before("java") == find_language("java")->prelude);
    CHECK(s.before("rust") == "struct Solution;\n");
    CHECK(s.before("c").empty()); // given to clangd by the workspace instead
    CHECK(s.before("mysql").empty());
    CHECK(s.after("cpp").empty());

    for (const auto& lang : languages()) {
        CHECK((lang.prelude.empty() || lang.prelude.ends_with('\n')));
        CHECK_FALSE(lang.prelude.contains("@lc code="));
    }
}

TEST_CASE("config problems are reported with their lines") {
    const auto loaded = parse_config(R"(theme = "nord"
[ui]
split = 2
mouse = "yes"
bogus = 1
theme = "nope"
[keys]
run = "nonsense"
[solution.inject_after]
klingon = "x"
[colors]
)");
    const auto& w = loaded.warnings;
    REQUIRE(w.size() == 8);
    CHECK(w[0] == "line 1: 'theme' belongs in the [ui] section");
    CHECK(w[1].starts_with("line 3: ui.split: expected a number from 0.15 to 0.85"));
    CHECK(w[2] == "line 4: ui.mouse: expected true or false");
    CHECK(w[3] == "line 5: unknown option ui.bogus");
    CHECK(w[4].starts_with("line 6: ui.theme: expected one of default, "));
    CHECK(w[5].starts_with("line 8: keys.run: expected a key"));
    CHECK(w[6] == "line 10: solution.inject_after: unknown language 'klingon'");
    CHECK(w[7] == "line 11: unknown section [colors]");
    // Bad values keep their defaults.
    CHECK(loaded.config.ui.split == doctest::Approx(0.42));
    CHECK(loaded.config.ui.theme == "default");

    const auto broken = parse_config("[ui]\ntheme = \"nord\"\nmouse = \n");
    REQUIRE(broken.warnings.size() == 1);
    CHECK(broken.warnings[0].starts_with("line 3: "));
    CHECK(broken.warnings[0].ends_with("(using the default settings)"));
    CHECK(broken.config.ui.theme == "default");
}

TEST_CASE("the default config lists every option at its default") {
    const auto text = default_config_text();
    const auto loaded = parse_config(text);
    CHECK(loaded.warnings.empty());
    const Config defaults;
    for (const auto& o : options()) {
        CAPTURE(o.key());
        CHECK(o.get(loaded.config) == o.get(defaults));
        CHECK(text.find(std::format("\n{} = ", o.name)) != std::string::npos);
        Config scratch;
        CHECK(set_config_value(scratch, o.key(), o.get(defaults)).empty());
        // Writing a default into the default file changes nothing.
        CHECK(set_option_in_text(text, o, toml_value(defaults, o)).value_or("") == text);
    }
    CHECK(key_conflicts(defaults).empty());
}

TEST_CASE("options by name and value") {
    REQUIRE(find_option("ui.theme") != nullptr);
    CHECK(find_option("theme") == find_option("ui.theme"));
    CHECK(find_option("keys.prefix")->kind == OptionKind::Key);
    CHECK(find_option("ui.nope") == nullptr);
    CHECK(find_option("nope") == nullptr);

    Config c;
    CHECK(set_config_value(c, " mouse ", " off ").empty());
    CHECK_FALSE(c.ui.mouse);
    CHECK(set_config_value(c, "split", "0.3").empty());
    CHECK(c.ui.split == doctest::Approx(0.3));
    CHECK(toml_value(c, *find_option("split")) == "0.3");
    CHECK(set_config_value(c, "split", "nan").starts_with("ui.split: expected"));
    CHECK(set_config_value(c, "refresh_hours", "-1").starts_with("account.refresh_hours"));
    CHECK(set_config_value(c, "refresh_hours", "12h").starts_with("account.refresh_hours"));
    CHECK(set_config_value(c, "lang", "rust").empty());
    CHECK(c.solution.lang == "rust");
    CHECK(set_config_value(c, "lang", "py").starts_with("solution.lang: expected one of"));
    CHECK(set_config_value(c, "keys.run", "ctrl-").starts_with("keys.run: expected a key"));
    CHECK(set_config_value(c, "workspace", "").starts_with("solution.workspace"));
    CHECK(set_config_value(c, "filename", "{slug}").find("{ext}") != std::string::npos);
    CHECK(set_config_value(c, "bogus", "1") == "unknown option 'bogus'");
    CHECK(toml_value(c, *find_option("lang")) == "\"rust\"");
}

TEST_CASE("key conflicts") {
    Config c;
    c.keys.run = "s";
    c.keys.zoom = "j";
    const auto conflicts = key_conflicts(c);
    REQUIRE(conflicts.size() == 2);
    CHECK(conflicts[0] == "keys.submit: s is also bound to keys.run");
    CHECK(conflicts[1] == "keys.zoom: j is reserved for moving the focus");
    // Swapping two keys in the file is fine: conflicts are judged on the result.
    CHECK(parse_config("[keys]\nrun = \"s\"\nsubmit = \"t\"\n").warnings.empty());
}

TEST_CASE("config file updates keep comments") {
    const auto* theme = find_option("ui.theme");
    const auto* mouse = find_option("ui.mouse");
    const auto* run = find_option("keys.run");
    const std::string text = R"(# hello
[solution]
lang = "cpp" # mine

[solution.inject_before]
cpp = """
[ui]
theme = "not this"
"""

[ui]
  theme = "nord"  # indented
)";
    auto updated = set_option_in_text(text, *theme, "\"gruvbox\"");
    REQUIRE(updated.has_value());
    CHECK(*updated == R"(# hello
[solution]
lang = "cpp" # mine

[solution.inject_before]
cpp = """
[ui]
theme = "not this"
"""

[ui]
  theme = "gruvbox"
)");
    // A key missing from its section goes after the section's last value...
    updated = set_option_in_text(text, *mouse, "false");
    REQUIRE(updated.has_value());
    CHECK(updated->ends_with("  theme = \"nord\"  # indented\nmouse = false\n"));
    // ...and a missing section is added at the end.
    updated = set_option_in_text(text, *run, "\"f5\"");
    REQUIRE(updated.has_value());
    CHECK(updated->ends_with("# indented\n\n[keys]\nrun = \"f5\"\n"));
    const auto loaded = parse_config(*updated);
    CHECK(loaded.config.keys.run == "f5");
    CHECK(loaded.config.ui.theme == "nord");
    CHECK(loaded.config.solution.inject_before.at("cpp") == "[ui]\ntheme = \"not this\"\n");

    // Dotted keys are found too.
    updated = set_option_in_text("ui.theme = \"nord\"\n", *theme, "\"light\"");
    REQUIRE(updated.has_value());
    CHECK(*updated == "ui.theme = \"light\"\n");

    // A broken file isn't touched, and edits that wouldn't read back are refused.
    CHECK_FALSE(set_option_in_text("[ui\n", *theme, "\"nord\"").has_value());
    CHECK_FALSE(set_option_in_text("ui.mouse = true\n", *theme, "\"nord\"").has_value());
}

TEST_CASE("config files are created on first change") {
    const TempDir dir;
    const auto path = dir.path / "leetvim" / "config.toml";
    CHECK(update_config_file(path, *find_option("ui.theme"), toml_quote("nord")).empty());
    std::string text;
    REQUIRE(paths::read_file(path, text));
    CHECK(text.starts_with("# leetvim settings"));
    const auto loaded = load_config(path);
    CHECK(loaded.warnings.empty());
    CHECK(loaded.config.ui.theme == "nord");
}

TEST_CASE("toml strings") {
    for (const std::string_view s : {"plain", "quote \" and \\ backslash", "tab\tnew\nline",
                                     "cr\r bell\a del\x7f", "ünïcödé ✓"}) {
        const auto loaded = parse_config("[account]\nusername = " + toml_quote(s) + "\n");
        CHECK(loaded.warnings.empty());
        CHECK(loaded.config.account.username == s);
    }
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

TEST_CASE("new solution files hold the definitions and their opening line") {
    auto q = two_sum();
    q.snippets = {{.lang = "PHP",
                   .lang_slug = "php",
                   .code = "/**\n * Definition for a singly-linked list.\n * class ListNode {}\n "
                           "*/\nclass Solution {}"}};
    const std::string file = render_solution(q, *find_language("php"), "url");
    CHECK(file.starts_with("<?php\n// @leetvim id=1 slug=two-sum lang=php\n"));
    CHECK(file.contains(
        "\n\n// Definition for a singly-linked list.\nclass ListNode {}\n\n// @lc code=start\n"));
    CHECK(extract_code(file).starts_with("/**\n * Definition"));
}

TEST_CASE("filename patterns") {
    CHECK(check_filename_pattern(default_filename_pattern).empty());
    CHECK(check_filename_pattern("{lang}/{slug}.{ext}").empty());
    CHECK_FALSE(check_filename_pattern("{slug}").empty());
    CHECK_FALSE(check_filename_pattern("solution.{ext}").empty());
    CHECK_FALSE(check_filename_pattern("{id}.{title}.{ext}").empty());
    CHECK_FALSE(check_filename_pattern("{id.{ext}").empty());
    CHECK_FALSE(check_filename_pattern("/abs/{id}.{ext}").empty());
    CHECK_FALSE(check_filename_pattern("../{id}.{ext}").empty());
    CHECK_FALSE(check_filename_pattern("a//{id}.{ext}").empty());

    const auto q = two_sum();
    const auto* py = find_language("python3");
    CHECK(solution_filename("{lang}/{id}-{slug}.{ext}", q, *py) == "python3/1-two-sum.py");
    const Workspace ws("/w", "{lang}/{id}.{slug}.{ext}");
    CHECK(ws.solution_path(q, *py) == "/w/python3/1.two-sum.py");
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

TEST_CASE("workspace keeps the language's support files, but not over the user's") {
    const TempDir dir;
    const auto read = [](const std::filesystem::path& path) {
        std::string text;
        return paths::read_file(path, text) ? text : "(missing)";
    };
    const Workspace ws(dir.path);
    CHECK(ws.ensure_support(*find_language("cpp")).empty());
    const auto clangd = read(dir.path / ".clangd");
    CHECK(clangd.starts_with("# Generated by leetvim"));
    // Relative to each depth, so the file is the same on every machine.
    CHECK(clangd.contains("PathMatch: '[^/]*\\.cpp'\nCompileFlags:\n"
                          "  Add: [-std=c++23, -include, include/leetcode.hpp]"));
    CHECK(clangd.contains("PathMatch: '[^/]*/[^/]*\\.c'\nCompileFlags:\n"
                          "  Add: [-include, ../include/leetcode.h]"));
    CHECK_FALSE(clangd.contains(dir.path.string()));
    CHECK(read(dir.path / "include" / "leetcode.hpp").ends_with("using namespace std;\n"));
    const auto q = two_sum();
    const auto solution = ws.ensure_solution(q, *find_language("cpp"), "url");
    REQUIRE(solution.has_value());
    CHECK_FALSE(read(*solution).contains("#include"));

    // An outdated generated file is rewritten; one the user claimed or wrote is kept.
    REQUIRE(paths::write_file(dir.path / ".clangd", "# Generated by leetvim, old\n"));
    REQUIRE(paths::write_file(dir.path / "include" / "leetcode.h", "// mine\n"));
    CHECK(ws.ensure_support(*find_language("cpp")).empty());
    CHECK(read(dir.path / ".clangd") == clangd);
    CHECK(read(dir.path / "include" / "leetcode.h") == "// mine\n");

    // The user's pyproject.toml configures ruff, so leetvim's ruff.toml would override it.
    REQUIRE(paths::write_file(dir.path / "pyproject.toml", "[tool.ruff]\nline-length = 79\n"));
    CHECK(ws.ensure_support(*find_language("python3")).empty());
    CHECK_FALSE(std::filesystem::exists(dir.path / "ruff.toml"));
    CHECK(read(dir.path / "pyrightconfig.json").starts_with("// Generated by leetvim"));
    const auto builtins = read(dir.path / "__builtins__.pyi");
    CHECK(builtins.contains("from typing import (\n    AbstractSet as AbstractSet,\n"));
    CHECK(builtins.contains("    deque as deque,\n"));
    CHECK(builtins.ends_with("import math as math\n"));
}

TEST_CASE("support files go in the language's directory and report failures") {
    const TempDir dir;
    Workspace ws(dir.path, "{lang}/{id}.{slug}.{ext}");
    CHECK(ws.ensure_support(*find_language("python3")).empty());
    CHECK(std::filesystem::exists(dir.path / "python3" / "__builtins__.pyi"));
    std::string ruff;
    REQUIRE(paths::read_file(dir.path / "python3" / "ruff.toml", ruff));
    CHECK(ruff.contains("    \"heappush\",\n"));
    CHECK(ruff.ends_with("    \"math\",\n]\n"));
    CHECK_FALSE(std::filesystem::exists(dir.path / "__builtins__.pyi"));

    // A directory where .clangd should be: reported, and tried again next time.
    std::filesystem::create_directories(dir.path / "cpp" / ".clangd");
    const auto problem = ws.ensure_support(*find_language("cpp"));
    CHECK(problem.starts_with("cannot write "));
    CHECK(problem.contains(".clangd"));
    std::filesystem::remove(dir.path / "cpp" / ".clangd");
    CHECK(ws.ensure_support(*find_language("cpp")).empty());
    CHECK(std::filesystem::is_regular_file(dir.path / "cpp" / ".clangd"));
}

TEST_CASE("support files make the workspace a project for Java and Rust servers") {
    const TempDir dir;
    const auto read = [](const std::filesystem::path& path) {
        std::string text;
        return paths::read_file(path, text) ? text : "(missing)";
    };
    const Workspace ws(dir.path, "{lang}/{id}.{slug}.{ext}");
    CHECK(ws.ensure_support(*find_language("java")).empty());
    CHECK(read(dir.path / "java" / "build.xml").starts_with("<!-- Generated by leetvim"));
    // A Maven or Gradle build of the user's is a project already.
    std::filesystem::remove(dir.path / "java" / "build.xml");
    REQUIRE(paths::write_file(dir.path / "java" / "pom.xml", "<project/>\n"));
    CHECK(ws.ensure_support(*find_language("java")).empty());
    CHECK_FALSE(std::filesystem::exists(dir.path / "java" / "build.xml"));

    // rust-analyzer's project lists each solution as a crate, so it follows new ones.
    const auto* rust = find_language("rust");
    auto q = two_sum();
    q.snippets = {{.lang = "Rust", .lang_slug = "rust", .code = "impl Solution {}"}};
    CHECK(ws.ensure_support(*rust).empty());
    const auto project = dir.path / "rust" / "rust-project.json";
    CHECK(read(project).starts_with("{\"//\": \"Generated by leetvim"));
    CHECK(read(project).ends_with(" \"crates\": [\n ]}\n"));
    REQUIRE(ws.ensure_solution(q, *rust, "url").has_value());
    CHECK(ws.ensure_support(*rust).empty());
    CHECK(read(project).ends_with(" \"crates\": [\n  {\"root_module\": \"1.two-sum.rs\", "
                                  "\"edition\": \"2021\", \"deps\": []}\n ]}\n"));
    std::filesystem::remove(project);
    REQUIRE(paths::write_file(dir.path / "rust" / "Cargo.toml", "[package]\n"));
    CHECK(ws.ensure_support(*rust).empty());
    CHECK_FALSE(std::filesystem::exists(project));
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
    CHECK(*tc == dir.path / "testcases" / "two-sum.txt");
    REQUIRE(paths::write_file(*tc, "[1,2]\n3\n[4,5]\n9\n[6,7]\n13\n"));
    CHECK(ws.testcases(q).size() == 3);
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

TEST_CASE("cookie domains come from the base url") {
    CHECK(cookie_domain("https://leetcode.com") == "leetcode.com");
    CHECK(cookie_domain("https://www.leetcode.com/") == "leetcode.com");
    CHECK(cookie_domain("http://localhost:8080/api") == "localhost:8080");
    CHECK(cookie_domain("leetcode.cn") == "leetcode.cn");
}

TEST_CASE("pasted credentials without a browser source can't be refreshed") {
    CHECK_FALSE(refresh_account({.session = "s", .csrf = "c", .source = ""}, "https://leetcode.com")
                    .has_value());
}
