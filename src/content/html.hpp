#pragma once

#include "content/document.hpp"

#include <string>
#include <string_view>

namespace lv::content {

/// Parses LeetCode problem HTML into a Document. Tolerant of malformed markup; never throws.
/// <sup>/<sub> become Unicode super/subscripts where possible ("10<sup>9</sup>" -> "10⁹"),
/// falling back to "^(...)"/"_(...)".
Document parse_html(std::string_view html);

/// Decodes HTML character references (named and numeric). &nbsp; becomes a plain space.
std::string decode_entities(std::string_view text);

} // namespace lv::content
