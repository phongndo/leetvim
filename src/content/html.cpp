#include "content/html.hpp"

#include "util/strings.hpp"
#include "util/unicode.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lv::content {

namespace {

struct Entity {
    std::string_view name;
    char32_t cp;
};

// Named references seen in LeetCode statements plus common math and typography.
// Space-like references decode to a plain space so they collapse like ordinary whitespace.
constexpr auto entities = std::to_array<Entity>({
    {"nbsp", U' '},   {"ensp", U' '},   {"emsp", U' '},    {"thinsp", U' '}, {"amp", U'&'},
    {"lt", U'<'},     {"gt", U'>'},     {"quot", U'"'},    {"apos", U'\''},  {"le", U'≤'},
    {"ge", U'≥'},     {"ne", U'≠'},     {"times", U'×'},   {"divide", U'÷'}, {"plusmn", U'±'},
    {"minus", U'−'},  {"middot", U'·'}, {"sdot", U'⋅'},    {"bull", U'•'},   {"hellip", U'…'},
    {"deg", U'°'},    {"infin", U'∞'},  {"sum", U'∑'},     {"prod", U'∏'},   {"radic", U'√'},
    {"asymp", U'≈'},  {"equiv", U'≡'},  {"prop", U'∝'},    {"cap", U'∩'},    {"cup", U'∪'},
    {"isin", U'∈'},   {"notin", U'∉'},  {"sub", U'⊂'},     {"sube", U'⊆'},   {"sup", U'⊃'},
    {"supe", U'⊇'},   {"forall", U'∀'}, {"exist", U'∃'},   {"empty", U'∅'},  {"and", U'∧'},
    {"or", U'∨'},     {"not", U'¬'},    {"oplus", U'⊕'},   {"lceil", U'⌈'},  {"rceil", U'⌉'},
    {"lfloor", U'⌊'}, {"rfloor", U'⌋'}, {"lang", U'⟨'},    {"rang", U'⟩'},   {"larr", U'←'},
    {"rarr", U'→'},   {"uarr", U'↑'},   {"darr", U'↓'},    {"harr", U'↔'},   {"lArr", U'⇐'},
    {"rArr", U'⇒'},   {"hArr", U'⇔'},   {"ndash", U'–'},   {"mdash", U'—'},  {"lsquo", U'‘'},
    {"rsquo", U'’'},  {"ldquo", U'“'},  {"rdquo", U'”'},   {"laquo", U'«'},  {"raquo", U'»'},
    {"prime", U'′'},  {"Prime", U'″'},  {"copy", U'©'},    {"reg", U'®'},    {"trade", U'™'},
    {"sect", U'§'},   {"para", U'¶'},   {"cent", U'¢'},    {"pound", U'£'},  {"yen", U'¥'},
    {"euro", U'€'},   {"frac12", U'½'}, {"frac14", U'¼'},  {"frac34", U'¾'}, {"sup1", U'¹'},
    {"sup2", U'²'},   {"sup3", U'³'},   {"micro", U'µ'},   {"alpha", U'α'},  {"beta", U'β'},
    {"gamma", U'γ'},  {"delta", U'δ'},  {"epsilon", U'ε'}, {"theta", U'θ'},  {"lambda", U'λ'},
    {"mu", U'μ'},     {"pi", U'π'},     {"sigma", U'σ'},   {"phi", U'φ'},    {"omega", U'ω'},
    {"Delta", U'Δ'},  {"Sigma", U'Σ'},  {"Omega", U'Ω'},   {"zwj", U'‍'},  {"zwnj", U'‌'},
});

struct ScriptChar {
    char32_t from;
    char32_t to;
};

constexpr auto superscripts = std::to_array<ScriptChar>({
    {U'0', U'⁰'}, {U'1', U'¹'}, {U'2', U'²'}, {U'3', U'³'}, {U'4', U'⁴'}, {U'5', U'⁵'},
    {U'6', U'⁶'}, {U'7', U'⁷'}, {U'8', U'⁸'}, {U'9', U'⁹'}, {U'+', U'⁺'}, {U'-', U'⁻'},
    {U'−', U'⁻'}, {U'=', U'⁼'}, {U'(', U'⁽'}, {U')', U'⁾'}, {U'a', U'ᵃ'}, {U'b', U'ᵇ'},
    {U'c', U'ᶜ'}, {U'd', U'ᵈ'}, {U'e', U'ᵉ'}, {U'f', U'ᶠ'}, {U'g', U'ᵍ'}, {U'h', U'ʰ'},
    {U'i', U'ⁱ'}, {U'j', U'ʲ'}, {U'k', U'ᵏ'}, {U'l', U'ˡ'}, {U'm', U'ᵐ'}, {U'n', U'ⁿ'},
    {U'o', U'ᵒ'}, {U'p', U'ᵖ'}, {U'r', U'ʳ'}, {U's', U'ˢ'}, {U't', U'ᵗ'}, {U'u', U'ᵘ'},
    {U'v', U'ᵛ'}, {U'w', U'ʷ'}, {U'x', U'ˣ'}, {U'y', U'ʸ'}, {U'z', U'ᶻ'}, {U'A', U'ᴬ'},
    {U'B', U'ᴮ'}, {U'D', U'ᴰ'}, {U'E', U'ᴱ'}, {U'G', U'ᴳ'}, {U'H', U'ᴴ'}, {U'I', U'ᴵ'},
    {U'J', U'ᴶ'}, {U'K', U'ᴷ'}, {U'L', U'ᴸ'}, {U'M', U'ᴹ'}, {U'N', U'ᴺ'}, {U'O', U'ᴼ'},
    {U'P', U'ᴾ'}, {U'R', U'ᴿ'}, {U'T', U'ᵀ'}, {U'U', U'ᵁ'}, {U'V', U'ⱽ'}, {U'W', U'ᵂ'},
});

constexpr auto subscripts = std::to_array<ScriptChar>({
    {U'0', U'₀'}, {U'1', U'₁'}, {U'2', U'₂'}, {U'3', U'₃'}, {U'4', U'₄'}, {U'5', U'₅'},
    {U'6', U'₆'}, {U'7', U'₇'}, {U'8', U'₈'}, {U'9', U'₉'}, {U'+', U'₊'}, {U'-', U'₋'},
    {U'−', U'₋'}, {U'=', U'₌'}, {U'(', U'₍'}, {U')', U'₎'}, {U'a', U'ₐ'}, {U'e', U'ₑ'},
    {U'h', U'ₕ'}, {U'i', U'ᵢ'}, {U'j', U'ⱼ'}, {U'k', U'ₖ'}, {U'l', U'ₗ'}, {U'm', U'ₘ'},
    {U'n', U'ₙ'}, {U'o', U'ₒ'}, {U'p', U'ₚ'}, {U'r', U'ᵣ'}, {U's', U'ₛ'}, {U't', U'ₜ'},
    {U'u', U'ᵤ'}, {U'v', U'ᵥ'}, {U'x', U'ₓ'},
});

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_ascii_punct(char c) { return c > ' ' && c < 0x7F && !is_alpha(c) && !is_digit(c); }

int digit_value(char c, bool hex) {
    if (is_digit(c)) {
        return c - '0';
    }
    if (hex && c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (hex && c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

struct Reference {
    char32_t cp = 0;
    std::size_t length = 0; // 0: not a character reference
};

// Decodes the character reference at the start of `s` (which begins with '&').
Reference parse_reference(std::string_view s) {
    if (s.size() < 3) {
        return {};
    }
    if (s[1] == '#') {
        const bool hex = s[2] == 'x' || s[2] == 'X';
        const std::size_t first = hex ? 3 : 2;
        std::size_t i = first;
        std::uint32_t value = 0;
        for (; i < s.size(); ++i) {
            const int digit = digit_value(s[i], hex);
            if (digit < 0) {
                break;
            }
            value = std::min<std::uint32_t>((value * (hex ? 16U : 10U)) + digit, 0x110000);
        }
        if (i == first) {
            return {};
        }
        if (i < s.size() && s[i] == ';') {
            ++i;
        }
        if (value == 0xA0) {
            return {.cp = U' ', .length = i};
        }
        const bool valid = value != 0 && value <= 0x10FFFF;
        return {
            .cp = valid ? static_cast<char32_t>(value) : unicode::replacement_char,
            .length = i,
        };
    }
    std::size_t i = 1;
    while (i < s.size() && i <= 32 && (is_alpha(s[i]) || is_digit(s[i]))) {
        ++i;
    }
    if (i == 1 || i >= s.size() || s[i] != ';') {
        return {};
    }
    const std::string_view name = s.substr(1, i - 1);
    const auto* it = std::ranges::find(entities, name, &Entity::name);
    if (it == entities.end()) {
        return {};
    }
    return {.cp = it->cp, .length = i + 1};
}

// Makes decoded text safe to lay out: no-break spaces become spaces, control characters other
// than tab and newline are dropped, and malformed UTF-8 becomes U+FFFD.
std::string clean(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    std::size_t pos = 0;
    while (pos < s.size()) {
        const auto byte = static_cast<unsigned char>(s[pos]);
        if (byte < 0x80) {
            ++pos;
            if (byte == '\t' || byte == '\n' || (byte >= 0x20 && byte != 0x7F)) {
                out.push_back(static_cast<char>(byte));
            }
            continue;
        }
        const char32_t cp = unicode::decode(s, pos);
        if (cp == 0xA0) {
            out.push_back(' ');
        } else if (cp >= 0xA0) {
            unicode::append_utf8(out, cp);
        }
    }
    return out;
}

// Collapses whitespace runs to single spaces and trims the ends.
std::string collapse(std::string_view s) {
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        if (is_space(s[i])) {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < s.size() && !is_space(s[i])) {
            ++i;
        }
        if (!out.empty()) {
            out.push_back(' ');
        }
        out.append(s.substr(start, i - start));
    }
    return out;
}

// "2" -> "²"; falls back to "^x" for a single token or "^(n/2)" otherwise.
std::string to_script(std::string_view text, std::span<const ScriptChar> table, char sign) {
    std::string out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const char32_t cp = unicode::decode(text, pos);
        const auto it = std::ranges::find(table, cp, &ScriptChar::from);
        if (it == table.end()) {
            const bool token =
                std::ranges::none_of(text, [](char c) { return is_space(c) || is_ascii_punct(c); });
            return token ? std::format("{}{}", sign, text) : std::format("{}({})", sign, text);
        }
        unicode::append_utf8(out, it->to);
    }
    return out;
}

int parse_int(std::string_view s, int fallback) {
    s = str::trim(s);
    int value = 0;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    return result.ec == std::errc{} ? value : fallback;
}

bool has_class(std::string_view classes, std::string_view name) {
    return std::ranges::contains(str::split(collapse(classes), ' '), name);
}

bool is_heading(std::string_view tag) {
    return tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6';
}

// Block containers whose content folds into an enclosing list item.
bool is_flow(std::string_view tag) {
    static constexpr auto tags = std::to_array<std::string_view>({
        "address", "article", "aside", "blockquote", "caption",    "center",  "dd",
        "details", "div",     "dl",    "dt",         "figcaption", "figure",  "footer",
        "header",  "main",    "nav",   "p",          "section",    "summary",
    });
    return is_heading(tag) || std::ranges::contains(tags, tag);
}

bool is_structural(std::string_view tag) {
    static constexpr auto tags =
        std::to_array<std::string_view>({"pre", "ul", "ol", "li", "table", "tr", "td", "th"});
    return std::ranges::contains(tags, tag);
}

std::uint8_t mark_for(std::string_view tag) {
    if (tag == "b" || tag == "strong") {
        return MarkBold;
    }
    if (tag == "i" || tag == "em") {
        return MarkItalic;
    }
    if (tag == "code" || tag == "tt" || tag == "kbd" || tag == "samp") {
        return MarkCode;
    }
    if (tag == "u" || tag == "ins") {
        return MarkUnderline;
    }
    if (tag == "s" || tag == "del" || tag == "strike") {
        return MarkStrike;
    }
    return MarkNone;
}

bool is_inline(std::string_view tag) { return tag == "a" || mark_for(tag) != MarkNone; }

void append(Block& block, std::string_view text, std::uint8_t marks, std::string_view href) {
    if ((marks & MarkLink) == 0) {
        href = {};
    }
    if (!block.spans.empty() && block.spans.back().marks == marks
        && block.spans.back().href == href) {
        block.spans.back().text.append(text);
    } else {
        block.spans.push_back(
            {.text = std::string(text), .marks = marks, .href = std::string(href)});
    }
}

bool at_line_start(const Block& block) {
    if (block.spans.empty() || block.spans.back().text.empty()) {
        return true;
    }
    const char last = block.spans.back().text.back();
    return last == ' ' || last == '\n';
}

// Drops trailing (and optionally leading) whitespace, removing spans left empty.
void trim(std::vector<Span>& spans, bool leading) {
    constexpr std::string_view ws = " \t\n";
    while (!spans.empty()) {
        std::string& text = spans.back().text;
        const std::size_t end = text.find_last_not_of(ws);
        text.erase(end == std::string::npos ? 0 : end + 1);
        if (!text.empty()) {
            break;
        }
        spans.pop_back();
    }
    while (leading && !spans.empty()) {
        std::string& text = spans.front().text;
        text.erase(0, text.find_first_not_of(ws));
        if (!text.empty()) {
            break;
        }
        spans.erase(spans.begin());
    }
}

struct Tag {
    std::string name;
    bool closing = false;
    bool self_closing = false;
    std::string cls;
    std::string href;
    std::string src;
    std::string alt;
    std::string start;
};

// An open block-level element.
struct Element {
    std::string tag;
    std::size_t inline_depth = 0; // inline marks opened inside are closed with it
    bool example = false;         // <div class="example-block">
    int next = 1;                 // <ol>: number of the next item
    std::string marker;           // <li>
    bool marker_used = false;     // <li>: later blocks of the item get a blank marker
    int cells = 0;                // <tr>
};

struct InlineMark {
    std::string tag;
    std::uint8_t marks = MarkNone;
    std::string href;
};

class Parser {
public:
    explicit Parser(std::string_view html) : html_(html) {}

    Document run() {
        std::size_t pos = 0;
        while (pos < html_.size()) {
            const std::size_t lt = html_.find('<', pos);
            if (lt == std::string_view::npos) {
                text(html_.substr(pos));
                break;
            }
            text(html_.substr(pos, lt - pos));
            pos = markup(lt);
        }
        finish_script();
        while (!open_.empty()) {
            close_top();
        }
        flush();
        return std::move(doc_);
    }

private:
    // Handles the markup at html_[pos] == '<' and returns the position after it.
    std::size_t markup(std::size_t pos) {
        const std::string_view rest = html_.substr(pos);
        if (rest.starts_with("<!--")) {
            return skip_past(pos + 4, "-->");
        }
        if (rest.starts_with("<!") || rest.starts_with("<?")) {
            return skip_past(pos + 2, ">");
        }
        std::size_t end = 0;
        const std::optional<Tag> tag = parse_tag(pos, end);
        if (!tag) {
            text("<");
            return pos + 1;
        }
        if (tag->closing) {
            end_tag(tag->name);
            return end;
        }
        start_tag(*tag);
        if (tag->self_closing) {
            end_tag(tag->name);
        }
        if (tag->name == "script" || tag->name == "style") {
            return skip_past(find_closing(end, tag->name), ">");
        }
        return end;
    }

    [[nodiscard]] std::size_t skip_past(std::size_t from, std::string_view terminator) const {
        const std::size_t at = html_.find(terminator, std::min(from, html_.size()));
        return at == std::string_view::npos ? html_.size() : at + terminator.size();
    }

    [[nodiscard]] std::size_t find_closing(std::size_t from, std::string_view name) const {
        const std::string needle = std::format("</{}", name);
        for (std::size_t i = from; i + needle.size() <= html_.size(); ++i) {
            if (str::iequals(html_.substr(i, needle.size()), needle)) {
                return i;
            }
        }
        return html_.size();
    }

    std::optional<Tag> parse_tag(std::size_t pos, std::size_t& end) const {
        const auto size = html_.size();
        Tag tag;
        std::size_t i = pos + 1;
        if (i < size && html_[i] == '/') {
            tag.closing = true;
            ++i;
        }
        if (i >= size || !is_alpha(html_[i])) {
            return std::nullopt;
        }
        const std::size_t name_start = i;
        while (i < size && (is_alpha(html_[i]) || is_digit(html_[i]) || html_[i] == '-')) {
            ++i;
        }
        tag.name = str::to_lower(html_.substr(name_start, i - name_start));

        while (i < size) {
            const char c = html_[i];
            if (c == '>') {
                end = i + 1;
                return tag;
            }
            if (is_space(c)) {
                ++i;
                continue;
            }
            if (c == '/') {
                tag.self_closing = i + 1 < size && html_[i + 1] == '>';
                ++i;
                continue;
            }
            const std::size_t attr_start = i;
            while (i < size && !is_space(html_[i]) && html_[i] != '=' && html_[i] != '>'
                   && html_[i] != '/') {
                ++i;
            }
            if (i == attr_start) { // stray '='
                ++i;
                continue;
            }
            const std::string attr = str::to_lower(html_.substr(attr_start, i - attr_start));
            while (i < size && is_space(html_[i])) {
                ++i;
            }
            std::string_view value;
            if (i < size && html_[i] == '=') {
                ++i;
                while (i < size && is_space(html_[i])) {
                    ++i;
                }
                if (i < size && (html_[i] == '"' || html_[i] == '\'')) {
                    const std::size_t close = html_.find(html_[i], i + 1);
                    if (close == std::string_view::npos) {
                        break;
                    }
                    value = html_.substr(i + 1, close - i - 1);
                    i = close + 1;
                } else {
                    const std::size_t value_start = i;
                    while (i < size && !is_space(html_[i]) && html_[i] != '>') {
                        ++i;
                    }
                    value = html_.substr(value_start, i - value_start);
                }
            }
            if (attr == "class") {
                tag.cls = decode_entities(value);
            }
            if (attr == "href") {
                tag.href = decode_entities(value);
            }
            if (attr == "src") {
                tag.src = decode_entities(value);
            }
            if (attr == "alt") {
                tag.alt = collapse(clean(decode_entities(value)));
            }
            if (attr == "start") {
                tag.start = decode_entities(value);
            }
        }
        // Unterminated attribute quote or no '>': end at the next '>' and drop the attributes.
        const std::size_t gt = html_.find('>', name_start);
        if (gt == std::string_view::npos) {
            return std::nullopt;
        }
        end = gt + 1;
        return Tag{.name = std::move(tag.name), .closing = tag.closing};
    }

    void start_tag(const Tag& tag) {
        const std::string& name = tag.name;
        if (name == "sup" || name == "sub") {
            if (script_depth_++ == 0) {
                script_sup_ = name == "sup";
                script_.clear();
            }
            return;
        }
        if (is_inline(name)) {
            const bool link = name == "a" && !tag.href.empty();
            inline_.push_back({
                .tag = name,
                .marks = link ? std::uint8_t{MarkLink} : mark_for(name),
                .href = link ? tag.href : std::string(),
            });
            return;
        }
        if (name == "br") {
            line_break();
            return;
        }
        const bool flow = is_flow(name);
        if (!flow && !is_structural(name) && name != "hr" && name != "img") {
            return;
        }

        finish_script();
        if (name == "img") {
            image(tag);
        } else if (name == "hr") {
            close_paragraph();
            flush();
            doc_.blocks.push_back({.kind = BlockKind::Rule, .in_example = in_example()});
        } else if (flow) {
            const std::string_view top =
                open_.empty() ? std::string_view{} : std::string_view{open_.back().tag};
            if (top == "p" || (is_heading(name) && is_heading(top))) {
                close_top();
            }
            boundary();
            open(name).example = name == "div" && has_class(tag.cls, "example-block");
        } else if (name == "li") {
            close_implied("li", {"ul", "ol"});
            flush();
            std::string marker = next_marker();
            open(name).marker = std::move(marker);
        } else if (name == "tr") {
            close_implied("tr", {"table"});
            flush();
            open(name);
        } else if (name == "td" || name == "th") {
            close_implied("td", {"tr", "table"});
            close_implied("th", {"tr", "table"});
            if (Element* row = innermost("tr"); row != nullptr) {
                if (row->cells > 0) {
                    separator(" | ");
                }
                ++row->cells;
            }
            open(name);
            if (name == "th") {
                inline_.push_back({.tag = name, .marks = MarkBold});
            }
        } else { // pre, ul, ol, table
            close_paragraph();
            flush();
            open(name).next = parse_int(tag.start, 1);
            pre_fresh_ = name == "pre";
        }
    }

    void end_tag(const std::string& name) {
        if (name == "sup" || name == "sub") {
            if (script_depth_ > 0) {
                --script_depth_;
                if (script_depth_ == 0) {
                    emit_script();
                }
            }
            return;
        }
        if (is_inline(name)) {
            for (std::size_t i = inline_.size(); i-- > 0;) {
                if (inline_[i].tag == name) {
                    inline_.resize(i);
                    break;
                }
            }
            return;
        }
        if (!is_flow(name) && !is_structural(name)) {
            return;
        }
        finish_script();
        for (std::size_t i = open_.size(); i-- > 0;) {
            if (open_[i].tag == name) {
                while (open_.size() > i) {
                    close_top();
                }
                return;
            }
        }
    }

    Element& open(const std::string& tag) {
        return open_.emplace_back(Element{.tag = tag, .inline_depth = inline_.size()});
    }

    void close_top() {
        const Element element = std::move(open_.back());
        open_.pop_back();
        if (inline_.size() > element.inline_depth) {
            inline_.resize(element.inline_depth);
        }
        if (element.tag == "td" || element.tag == "th") {
            return;
        }
        if (is_flow(element.tag)) {
            boundary();
        } else {
            flush();
        }
    }

    void close_paragraph() {
        if (!open_.empty() && open_.back().tag == "p") {
            close_top();
        }
    }

    // Closes the innermost open `tag` (and anything inside it) unless a `scope` element is
    // open inside it, the way a new <li> ends the previous one.
    void close_implied(std::string_view tag, std::initializer_list<std::string_view> scope) {
        for (std::size_t i = open_.size(); i-- > 0;) {
            if (open_[i].tag == tag) {
                while (open_.size() > i) {
                    close_top();
                }
                return;
            }
            if (std::ranges::contains(scope, open_[i].tag)) {
                return;
            }
        }
    }

    Element* innermost(std::string_view tag) {
        const auto it = std::ranges::find(open_ | std::views::reverse, tag, &Element::tag);
        return it == open_.rend() ? nullptr : &*it;
    }

    std::string next_marker() {
        for (Element& element : open_ | std::views::reverse) {
            if (element.tag == "ol") {
                return std::format("{}.", element.next++);
            }
            if (element.tag == "ul") {
                return "•";
            }
        }
        return "•";
    }

    [[nodiscard]] bool in_pre() const {
        return std::ranges::any_of(open_, [](const Element& e) { return e.tag == "pre"; });
    }

    [[nodiscard]] bool in_example() const { return std::ranges::any_of(open_, &Element::example); }

    [[nodiscard]] bool in_list_item() const {
        for (const Element& element : open_ | std::views::reverse) {
            if (element.tag == "li") {
                return true;
            }
            if (element.tag == "ul" || element.tag == "ol") {
                return false;
            }
        }
        return false;
    }

    // A flow element starts or ends: a block boundary, unless its content folds into an
    // enclosing list item (joined by a space) or <pre> (kept verbatim).
    void boundary() {
        if (in_pre()) {
            return;
        }
        if (in_list_item()) {
            note_space(MarkNone, {});
        } else {
            flush();
        }
    }

    Block& ensure_block() {
        if (block_) {
            return *block_;
        }
        Block& block = block_.emplace();
        block.in_example = in_example();
        if (in_pre()) {
            block.kind = BlockKind::Preformatted;
            block.in_example = true;
            return block;
        }
        for (Element& element : open_ | std::views::reverse) {
            if (element.tag == "ul" || element.tag == "ol") {
                break;
            }
            if (element.tag != "li") {
                continue;
            }
            const auto lists = std::ranges::count_if(
                open_, [](const Element& e) { return e.tag == "ul" || e.tag == "ol"; });
            block.kind = BlockKind::ListItem;
            block.level = std::max(1, static_cast<int>(lists));
            block.marker =
                element.marker_used
                    ? std::string(static_cast<std::size_t>(unicode::display_width(element.marker)),
                                  ' ')
                    : element.marker;
            element.marker_used = true;
            return block;
        }
        const auto heading = std::ranges::find_if(
            open_ | std::views::reverse, [](const Element& e) { return is_heading(e.tag); });
        if (heading != open_.rend()) {
            block.kind = BlockKind::Heading;
            block.level = heading->tag[1] - '0';
        }
        return block;
    }

    void flush() {
        pending_space_ = false;
        if (!block_) {
            return;
        }
        Block block = std::move(*block_);
        block_.reset();
        trim(block.spans, block.kind != BlockKind::Preformatted);
        if (!block.spans.empty()) {
            doc_.blocks.push_back(std::move(block));
        }
    }

    [[nodiscard]] std::uint8_t marks() const {
        std::uint8_t out = MarkNone;
        for (const InlineMark& mark : inline_) {
            out |= mark.marks;
        }
        return out;
    }

    [[nodiscard]] std::string href() const {
        for (const InlineMark& mark : inline_ | std::views::reverse) {
            if ((mark.marks & MarkLink) != 0) {
                return mark.href;
            }
        }
        return {};
    }

    void text(std::string_view raw) {
        if (raw.empty()) {
            return;
        }
        const std::string s = clean(decode_entities(raw));
        if (script_depth_ > 0) {
            script_ += s;
        } else if (in_pre()) {
            pre_text(s);
        } else {
            flow_text(s);
        }
    }

    void pre_text(std::string_view s) {
        if (pre_fresh_ && s.starts_with('\n')) {
            s.remove_prefix(1);
        }
        pre_fresh_ = false;
        if (!s.empty()) {
            append(ensure_block(), s, marks(), href());
        }
    }

    void flow_text(std::string_view s) {
        std::size_t i = 0;
        while (i < s.size()) {
            if (is_space(s[i])) {
                note_space(marks(), href());
                while (i < s.size() && is_space(s[i])) {
                    ++i;
                }
                continue;
            }
            const std::size_t start = i;
            while (i < s.size() && !is_space(s[i])) {
                ++i;
            }
            word(s.substr(start, i - start));
        }
    }

    // Whitespace is emitted lazily, before the next word, so it never trails a line. The space
    // keeps only the marks shared by the whitespace and that word: "<b>a </b>b" has a plain
    // space, "<code>a b</code>" a code one.
    void note_space(std::uint8_t with_marks, const std::string& with_href) {
        if (!pending_space_) {
            pending_space_ = true;
            space_marks_ = with_marks;
            space_href_ = with_href;
            return;
        }
        space_marks_ &= with_marks;
        if (space_href_ != with_href) {
            space_marks_ &= static_cast<std::uint8_t>(~MarkLink);
        }
    }

    void word(std::string_view w) {
        Block& block = ensure_block();
        const std::uint8_t word_marks = marks();
        const std::string link = href();
        if (pending_space_ && !at_line_start(block)) {
            auto space = static_cast<std::uint8_t>(space_marks_ & word_marks);
            if (space_href_ != link) {
                space &= static_cast<std::uint8_t>(~MarkLink);
            }
            append(block, " ", space, link);
        }
        pending_space_ = false;
        append(block, w, word_marks, link);
    }

    void separator(std::string_view s) {
        append(ensure_block(), s, MarkNone, {});
        pending_space_ = false;
    }

    void line_break() {
        if (script_depth_ > 0) {
            script_ += ' ';
            return;
        }
        separator("\n");
    }

    void finish_script() {
        if (script_depth_ == 0) {
            return;
        }
        script_depth_ = 0;
        emit_script();
    }

    void emit_script() {
        const std::string raw = collapse(script_);
        script_.clear();
        const std::string out =
            script_sup_ ? to_script(raw, superscripts, '^') : to_script(raw, subscripts, '_');
        if (in_pre()) {
            pre_text(out);
        } else {
            flow_text(out);
        }
    }

    void image(const Tag& tag) {
        if (tag.src.empty() && tag.alt.empty()) {
            return;
        }
        flush();
        Block block{.kind = BlockKind::Image, .in_example = in_example(), .src = tag.src};
        if (!tag.alt.empty()) {
            block.spans.push_back({.text = tag.alt});
        }
        doc_.blocks.push_back(std::move(block));
    }

    std::string_view html_;
    Document doc_;
    std::optional<Block> block_; // block being filled; created lazily by the first content
    std::vector<Element> open_;
    std::vector<InlineMark> inline_;
    bool pending_space_ = false;
    std::uint8_t space_marks_ = MarkNone;
    std::string space_href_;
    bool pre_fresh_ = false; // a newline right after <pre> is dropped
    int script_depth_ = 0;   // inside <sup>/<sub>: text is buffered and converted at the end
    bool script_sup_ = false;
    std::string script_;
};

} // namespace

Document parse_html(std::string_view html) { return Parser(html).run(); }

std::string decode_entities(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t amp = text.find('&', pos);
        if (amp == std::string_view::npos) {
            out.append(text.substr(pos));
            break;
        }
        out.append(text.substr(pos, amp - pos));
        const Reference ref = parse_reference(text.substr(amp));
        if (ref.length == 0) {
            out.push_back('&');
            pos = amp + 1;
        } else {
            unicode::append_utf8(out, ref.cp);
            pos = amp + ref.length;
        }
    }
    return out;
}

} // namespace lv::content
