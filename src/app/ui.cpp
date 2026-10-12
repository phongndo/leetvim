#include "app/ui.hpp"

#include "util/strings.hpp"
#include "util/unicode.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <format>

namespace lv::app::ui {

using leetcode::Difficulty;
using leetcode::Status;
using tui::Color;
using tui::Style;

Color difficulty_color(const tui::Theme& t, Difficulty d) {
    switch (d) {
    case Difficulty::Easy: return t.easy;
    case Difficulty::Medium: return t.medium;
    case Difficulty::Hard: return t.hard;
    case Difficulty::Unknown: break;
    }
    return t.inactive_fg;
}

Color status_color(const tui::Theme& t, Status s) {
    switch (s) {
    case Status::Solved: return t.solved;
    case Status::Attempted: return t.attempted;
    case Status::Todo: break;
    }
    return t.inactive_fg;
}

std::string_view status_icon(Status s) {
    switch (s) {
    case Status::Solved: return "✓";
    case Status::Attempted: return "◐";
    case Status::Todo: break;
    }
    return " ";
}

Color acceptance_color(const tui::Theme& t, double percent) {
    // 20% or less reads as "hard to pass", 70%+ as easy.
    const double v = std::clamp((percent - 20.0) / 50.0, 0.0, 1.0);
    return tui::gradient(t.grad_end, t.grad_mid, t.grad_start, v);
}

std::string superscript(int n) {
    static constexpr std::array<std::string_view, 10> digits{"⁰", "¹", "²", "³", "⁴",
                                                             "⁵", "⁶", "⁷", "⁸", "⁹"};
    std::string out;
    for (const char c : std::to_string(n)) out += digits.at(static_cast<std::size_t>(c - '0'));
    return out;
}

tui::Segments title(const tui::Theme& t, std::string_view name, int number, bool active) {
    tui::Segments out;
    if (number > 0) out.push_back({superscript(number), fg(active ? t.hi_fg : t.inactive_fg)});
    out.push_back({std::string(name), fg(active ? t.title : t.inactive_fg, tui::AttrBold)});
    return out;
}

tui::Segments hint(const tui::Theme& t, std::string_view key, std::string_view action) {
    const tui::Style hot = fg(t.hi_fg, tui::AttrBold);
    const tui::Style text = fg(t.main_fg);
    // btop style: a letter key that occurs in the word is highlighted in place ("filter",
    // "sort"), keeping the key's case so shifted keys read as such ("Hints").
    if (key.size() == 1 && std::isalpha(static_cast<unsigned char>(key[0])) != 0) {
        const auto lower = [](char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        };
        const auto* const it = std::ranges::find(action, lower(key[0]), lower);
        if (it != action.end()) {
            const auto pos = static_cast<std::size_t>(it - action.begin());
            return {{std::string(action.substr(0, pos)), text},
                    {std::string(key), hot},
                    {std::string(action.substr(pos + 1)), text}};
        }
    }
    tui::Segments out{{std::string(key), hot}};
    if (!action.empty()) out.push_back({" " + std::string(action), text});
    return out;
}

tui::Segments label(std::string_view text, Color color, std::uint8_t attrs) {
    return {{std::string(text), fg(color, attrs)}};
}

tui::Rect box(const tui::Painter& p, const tui::Theme& t, const BoxSpec& spec) {
    const tui::Rect r = spec.rect;
    if (r.w < 4 || r.h < 2) return {r.x, r.y, 0, 0};
    const Style border = fg(spec.focused ? spec.color : t.div_line);
    tui::draw_box(p, r, border, spec.rounded);

    // Right titles first so left titles know where to stop.
    int right_limit = r.right() - 2;
    for (const auto& seg : spec.right_titles) {
        const int w = tui::segments_width(seg) + 2;
        if (right_limit - w < r.x + 2) break;
        right_limit = tui::top_label_right(p, right_limit, r.y, seg, border) - 1;
    }
    int x = r.x + 2; // btop style: "╭─┐title┌"
    for (const auto& seg : spec.titles) {
        const int w = tui::segments_width(seg) + 2;
        if (x + w > right_limit) break;
        x = tui::top_label(p, x, r.y, seg, border);
    }
    x = r.x + 2;
    for (const auto& seg : spec.hints) {
        const int w = tui::segments_width(seg) + 2;
        if (x + w > r.right() - 1) break;
        x = tui::bottom_label(p, x, r.bottom() - 1, seg, border);
    }
    return r.inset(1);
}

void draw_document(const tui::Painter& p, tui::Rect r, const std::vector<content::Line>& lines,
                   int scroll, const tui::Theme& t) {
    using content::Tone;
    const tui::Painter clip = p.clipped(r);
    for (int row = 0; row < r.h; ++row) {
        const auto index = static_cast<std::size_t>(scroll) + static_cast<std::size_t>(row);
        if (index >= lines.size()) break;
        int x = r.x;
        for (const auto& span : lines[index].spans) {
            Style s = fg(t.main_fg);
            switch (span.tone) {
            case Tone::Heading: s = fg(t.title, tui::AttrBold); break;
            case Tone::CodeBlock: s = fg(t.code_fg); break;
            case Tone::Gutter: s = fg(t.div_line); break;
            case Tone::Bullet: s = fg(t.hi_fg); break;
            case Tone::Rule: s = fg(t.div_line); break;
            case Tone::Image: s = fg(t.graph_text, tui::AttrItalic); break;
            case Tone::Normal: break;
            }
            if ((span.marks & content::MarkCode) != 0 && span.tone != Tone::CodeBlock) {
                s.fg = t.accent;
            }
            if ((span.marks & content::MarkBold) != 0) {
                s.attrs |= tui::AttrBold;
                if (span.tone == Tone::Normal || span.tone == Tone::CodeBlock) s.fg = t.title;
            }
            if ((span.marks & content::MarkItalic) != 0) s.attrs |= tui::AttrItalic;
            if ((span.marks & content::MarkUnderline) != 0) s.attrs |= tui::AttrUnderline;
            if ((span.marks & content::MarkStrike) != 0) s.attrs |= tui::AttrStrike;
            if ((span.marks & content::MarkLink) != 0) {
                s.fg = t.link;
                s.attrs |= tui::AttrUnderline;
            }
            x = clip.text(x, r.y + row, span.text, s);
        }
    }
}

std::vector<std::string> wrap(std::string_view text, int width) {
    // Word wrap at spaces; words wider than the line are split by grapheme.
    std::vector<std::string> out;
    width = std::max(width, 1);
    for (const auto raw_line : str::split(text, '\n')) {
        std::string line;
        int used = 0;
        const auto flush = [&] {
            while (line.ends_with(' ')) line.pop_back();
            out.push_back(std::move(line));
            line.clear();
            used = 0;
        };
        for (const auto word : str::split(str::replace_all(raw_line, "\t", "    "), ' ')) {
            const int w = unicode::display_width(word);
            if (used > 0 && used + 1 + w > width) flush();
            if (used > 0) {
                line += ' ';
                ++used;
            }
            std::size_t pos = 0;
            while (pos < word.size()) {
                const auto g = unicode::next_grapheme(word, pos);
                if (used + g.width > width && used > 0) flush();
                line.append(g.bytes);
                used += g.width;
            }
        }
        flush();
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

int draw_wrapped(const tui::Painter& p, tui::Rect r, std::string_view text, Style style,
                 int scroll) {
    const auto lines = wrap(text, r.w);
    int row = 0;
    for (std::size_t i = static_cast<std::size_t>(std::max(scroll, 0)); i < lines.size(); ++i) {
        if (row >= r.h) break;
        p.text(r.x, r.y + row, lines[i], style);
        ++row;
    }
    return static_cast<int>(lines.size());
}

std::string thousands(std::int64_t n) {
    std::string digits = std::to_string(n < 0 ? -n : n);
    for (auto i = static_cast<std::ptrdiff_t>(digits.size()) - 3; i > 0; i -= 3) {
        digits.insert(static_cast<std::size_t>(i), ",");
    }
    return n < 0 ? "-" + digits : digits;
}

std::string time_ago(std::int64_t unix_seconds) {
    using namespace std::chrono;
    const auto now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    const auto diff = now - unix_seconds;
    if (diff < 60) return "just now";
    if (diff < 3600) return std::format("{}m ago", diff / 60);
    if (diff < 86400) return std::format("{}h ago", diff / 3600);
    if (diff < 86400LL * 30) return std::format("{}d ago", diff / 86400);
    const sys_seconds tp{seconds{unix_seconds}};
    return std::format("{:%Y-%m-%d}", floor<days>(tp));
}

} // namespace lv::app::ui
