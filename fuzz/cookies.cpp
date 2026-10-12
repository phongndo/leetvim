// Browser cookie stores and pasted Cookie headers are read from outside leetvim.
#include "leetcode/browsers.hpp"
#include "leetcode/credentials.hpp"
#include "require.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    namespace bd = lv::leetcode::browser_detail;
    const std::string_view in(reinterpret_cast<const char*>(data), size);
    (void)bd::parse_binarycookies(in);
    (void)lv::leetcode::parse_cookie_string(in);

    static const std::string key = bd::chromium_key("peanuts", 1);
    (void)bd::chromium_decrypt(in, key, true);
    (void)bd::chromium_decrypt(in, key, false);
    // Decryption rejects non-printable plaintext (that's how a wrong key shows); cookie
    // values are printable ASCII.
    const bool printable = std::ranges::all_of(in, [](char c) { return c >= 0x20 && c < 0x7f; });
    if (printable) {
        require(bd::chromium_decrypt(bd::chromium_encrypt(in, key), key, false) == std::string(in),
                "chromium_decrypt() undoes chromium_encrypt()");
    }

    const auto dot = in.find('.');
    if (dot != std::string_view::npos) (void)bd::host_matches(in.substr(0, dot), in.substr(dot));
    return 0;
}
