// Solution files: what gets submitted must be exactly the snippet between the markers.
#include "core/workspace.hpp"

#include "core/languages.hpp"
#include "require.hpp"

#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    namespace core = lv::core;
    FuzzedDataProvider fdp(data, size);
    const auto langs = core::languages();
    const auto& lang = langs[fdp.ConsumeIntegralInRange<std::size_t>(0, langs.size() - 1)];
    const auto lines_per_case = fdp.ConsumeIntegralInRange<std::size_t>(0, 4);
    const std::string title = fdp.ConsumeRandomLengthString(64);
    const std::string before = fdp.ConsumeRandomLengthString(256);
    const std::string after = fdp.ConsumeRandomLengthString(256);
    const std::string code = fdp.ConsumeRemainingBytesAsString();

    (void)core::extract_code(code);
    (void)core::parse_header(code);
    (void)core::group_testcases(code, lines_per_case);

    lv::leetcode::Question q;
    q.frontend_id = "42";
    q.slug = "trapping-rain-water";
    q.title = title;
    q.snippets.push_back(
        {.lang = std::string(lang.name), .lang_slug = std::string(lang.slug), .code = code});
    const auto file = core::render_solution(
        q, lang, "https://leetcode.com/problems/trapping-rain-water/", before, after);

    // Text that itself contains a marker legitimately changes what's between the markers.
    for (const auto& part : {title, before, after, code}) {
        if (part.contains("@lc code=") || part.contains("@leet ")) return 0;
    }
    std::string expected = code;
    if (!expected.empty() && !expected.ends_with('\n')) expected += '\n';
    require(core::extract_code(file) == expected, "extract_code() returns the snippet");

    const auto meta = core::parse_header(file);
    require(meta && meta->frontend_id == q.frontend_id && meta->slug == q.slug
                && meta->lang == lang.slug,
            "parse_header() reads the rendered header");
    return 0;
}
