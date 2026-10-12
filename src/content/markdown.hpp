#pragma once

#include "content/document.hpp"

#include <string>

namespace lv::content {

/// Renders a Document as CommonMark-compatible Markdown.
std::string to_markdown(const Document& doc);

} // namespace lv::content
