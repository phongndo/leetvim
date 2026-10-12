#include "app/cli.hpp"

#include "app/app.hpp"
#include "app/ui.hpp"
#include "app/update.hpp"
#include "content/html.hpp"
#include "content/layout.hpp"
#include "content/markdown.hpp"
#include "core/account.hpp"
#include "core/config.hpp"
#include "core/editor.hpp"
#include "core/languages.hpp"
#include "core/workspace.hpp"
#include "leetcode/cache.hpp"
#include "leetcode/client.hpp"
#include "leetcode/credentials.hpp"
#include "util/paths.hpp"
#include "util/strings.hpp"
#include "util/unicode.hpp"

#include <CLI/CLI.hpp>

#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iostream>
#include <print>
#include <utility>

namespace lv::app {

namespace fs = std::filesystem;
using leetcode::Difficulty;
using leetcode::Status;

namespace {

#ifndef LEETVIM_VERSION
#define LEETVIM_VERSION "dev"
#endif

/// ANSI styling for plain command output; disabled when piped or NO_COLOR is set.
struct Ansi {
    bool on = isatty(STDOUT_FILENO) != 0 && std::getenv("NO_COLOR") == nullptr;

    [[nodiscard]] std::string wrap(std::string_view text, std::string_view sgr) const {
        if (!on) return std::string(text);
        return std::format("\x1b[{}m{}\x1b[0m", sgr, text);
    }
    [[nodiscard]] std::string rgb(std::string_view text, tui::Color c, bool bold = false) const {
        return wrap(text, std::format("{}38;2;{};{};{}", bold ? "1;" : "", c.r, c.g, c.b));
    }
    [[nodiscard]] std::string bold(std::string_view t) const { return wrap(t, "1"); }
    [[nodiscard]] std::string dim(std::string_view t) const { return wrap(t, "2"); }
};

const tui::Theme& theme() { return tui::theme_named("default"); }

std::string difficulty(const Ansi& a, Difficulty d) {
    return a.rgb(leetcode::to_string(d), ui::difficulty_color(theme(), d), true);
}

int terminal_width() {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
    return 100;
}

struct Context {
    core::Config config;
    fs::path config_path;
    leetcode::Client client;
    leetcode::Cache cache;
    core::Workspace workspace;
};

Context make_context(const core::LoadedConfig& loaded, fs::path config_path) {
    for (const auto& w : loaded.warnings) std::println(stderr, "leetvim: config: {}", w);
    return {.config = loaded.config,
            .config_path = std::move(config_path),
            .client = leetcode::Client(leetcode::load_credentials()),
            .cache = leetcode::Cache(paths::cache_dir()),
            .workspace = core::Workspace(paths::expand_user(loaded.config.solution.workspace),
                                         loaded.config.solution.filename)};
}

std::vector<leetcode::ProblemSummary> problems(const Context& ctx, bool quiet = false) {
    if (auto cached = ctx.cache.load_problems()) return std::move(cached->problems);
    if (!quiet) std::print(stderr, "downloading the problem list…");
    auto result = ctx.client.problems([&](int done, int total) {
        if (!quiet) std::print(stderr, "\rdownloading the problem list… {}/{}", done, total);
    });
    if (!quiet) std::println(stderr, "");
    if (!result) {
        std::println(stderr, "leetvim: {}", result.error().message);
        return {};
    }
    [[maybe_unused]] const bool ok = ctx.cache.save_problems(*result);
    return std::move(*result);
}

/// Resolves "1", "two-sum" or "Two Sum" to a slug.
std::string resolve_slug(const Context& ctx, const std::string& query) {
    if (query.find_first_not_of("0123456789") != std::string::npos && !query.contains(' ')) {
        return query; // already a slug
    }
    for (const auto& p : problems(ctx)) {
        if (p.frontend_id == query || str::iequals(p.title, query)) return p.slug;
    }
    return {};
}

std::optional<leetcode::Question> load_question(const Context& ctx, const std::string& slug) {
    if (auto q = ctx.cache.load_question(slug)) return q;
    auto q = ctx.client.question(slug);
    if (!q) {
        std::println(stderr, "leetvim: {}: {}", slug, q.error().message);
        return std::nullopt;
    }
    [[maybe_unused]] const bool ok = ctx.cache.save_question(*q);
    return std::move(*q);
}

std::string render_lines(const Ansi& a, const std::vector<content::Line>& lines) {
    using content::Tone;
    const auto& t = theme();
    std::string out;
    for (const auto& line : lines) {
        for (const auto& span : line.spans) {
            if (!a.on) {
                out += span.text;
                continue;
            }
            std::string sgr;
            tui::Color c = t.main_fg;
            if (span.tone == Tone::Heading) {
                c = t.title;
                sgr += "1;";
            } else if (span.tone == Tone::CodeBlock) {
                c = t.code_fg;
            } else if (span.tone == Tone::Gutter || span.tone == Tone::Rule) {
                c = t.inactive_fg;
            } else if (span.tone == Tone::Bullet) {
                c = t.hi_fg;
            }
            if ((span.marks & content::MarkCode) != 0 && span.tone != Tone::CodeBlock) c = t.accent;
            if ((span.marks & content::MarkBold) != 0) sgr += "1;";
            if ((span.marks & content::MarkItalic) != 0) sgr += "3;";
            if ((span.marks & content::MarkUnderline) != 0
                || (span.marks & content::MarkLink) != 0) {
                sgr += "4;";
            }
            out += a.wrap(span.text, std::format("{}38;2;{};{};{}", sgr, c.r, c.g, c.b));
        }
        out += '\n';
    }
    return out;
}

std::string bar(const Ansi& a, double fraction, int width, tui::Color c) {
    const auto filled = static_cast<int>(std::lround(fraction * width));
    std::string on;
    std::string off;
    for (int i = 0; i < width; ++i) (i < filled ? on : off) += "■";
    return a.rgb(on, c) + a.rgb(off, theme().meter_bg);
}

// ---------------------------------------------------------------- commands

int cmd_show(const Context& ctx, const std::string& query, bool markdown) {
    const std::string slug = resolve_slug(ctx, query);
    if (slug.empty()) {
        std::println(stderr, "leetvim: no problem '{}'", query);
        return 1;
    }
    const auto q = load_question(ctx, slug);
    if (!q) return 1;
    const auto doc = content::parse_html(q->content_html);
    if (markdown) {
        std::println("# {}. {}\n", q->frontend_id, q->title);
        std::println("**{}** · {} · {}\n", leetcode::to_string(q->difficulty), q->ac_rate,
                     ctx.client.problem_url(q->slug));
        std::print("{}", content::to_markdown(doc));
        return 0;
    }
    const Ansi a;
    std::println("{} {}", a.bold(std::format("{}. {}", q->frontend_id, q->title)),
                 difficulty(a, q->difficulty));
    std::println("{}\n", a.dim(ctx.client.problem_url(q->slug)));
    std::print("{}", render_lines(a, content::layout(doc, std::min(terminal_width(), 100))));
    return 0;
}

int cmd_list(const Context& ctx, const std::vector<std::string>& levels,
             const std::vector<std::string>& statuses, const std::vector<std::string>& tags,
             const std::string& query, int limit) {
    const Ansi a;
    int shown = 0;
    for (const auto& p : problems(ctx)) {
        if (!levels.empty() && std::ranges::none_of(levels, [&](const auto& l) {
                return leetcode::parse_difficulty(l) == p.difficulty;
            })) {
            continue;
        }
        if (!statuses.empty() && std::ranges::none_of(statuses, [&](const auto& s) {
                return (s == "solved" && p.status == Status::Solved)
                       || (s == "attempted" && p.status == Status::Attempted)
                       || (s == "todo" && p.status == Status::Todo);
            })) {
            continue;
        }
        if (std::ranges::any_of(tags, [&](const auto& tag) {
                return std::ranges::none_of(p.tags, [&](const auto& t) {
                    return t.slug == tag || str::iequals(t.name, tag);
                });
            })) {
            continue;
        }
        if (!query.empty() && !str::icontains(p.title, query) && p.frontend_id != query) continue;
        const auto& t = theme();
        std::println("{} {} {} {} {}",
                     a.rgb(ui::status_icon(p.status), ui::status_color(t, p.status)),
                     a.dim(unicode::pad_left(p.frontend_id, 5)), unicode::pad_right(p.title, 60),
                     a.rgb(unicode::pad_right(leetcode::to_string(p.difficulty), 7),
                           ui::difficulty_color(t, p.difficulty)),
                     a.rgb(str::percent(p.ac_rate), ui::acceptance_color(t, p.ac_rate)));
        if (limit > 0 && ++shown >= limit) break;
    }
    return 0;
}

struct Target {
    leetcode::Question question;
    const core::Language* lang = nullptr;
    std::string code;
};

std::optional<Target> load_target(const Context& ctx, const fs::path& file) {
    std::string content;
    if (!paths::read_file(file, content)) {
        std::println(stderr, "leetvim: cannot read {}", file.string());
        return std::nullopt;
    }
    auto meta = core::parse_header(content);
    if (!meta) meta = core::parse_filename(file);
    if (!meta) {
        std::println(stderr, "leetvim: {} has no @leetvim header and isn't named <id>.<slug>.<ext>",
                     file.string());
        return std::nullopt;
    }
    const auto* lang = core::find_language(meta->lang);
    if (lang == nullptr) lang = core::language_for_extension(file.extension().string());
    if (lang == nullptr) {
        std::println(stderr, "leetvim: unknown language for {}", file.string());
        return std::nullopt;
    }
    auto q = load_question(ctx, meta->slug);
    if (!q) return std::nullopt;
    return Target{.question = std::move(*q), .lang = lang, .code = core::extract_code(content)};
}

int cmd_judge(const Context& ctx, const fs::path& file, bool submit) {
    if (!ctx.client.has_credentials()) {
        std::println(stderr, "leetvim: not signed in, run `leetvim login` first");
        return 2;
    }
    const auto target = load_target(ctx, file);
    if (!target) return 2;
    const auto& q = target->question;
    const Ansi a;
    const auto cases = ctx.workspace.testcases(q);
    std::string input;
    for (const auto& c : cases) input += (input.empty() ? "" : "\n") + c;

    std::println(stderr, "{} {}. {} ({})", submit ? "submitting" : "running", q.frontend_id,
                 q.title, target->lang->name);
    leetcode::Client client = ctx.client;
    const auto start = [&] {
        return submit ? client.submit(q, target->lang->slug, target->code)
                      : client.run(q, target->lang->slug, target->code, input);
    };
    auto id = start();
    if (!id && id.error().kind == leetcode::Error::Kind::Auth) {
        // An expired session imported from a browser is renewed from it, then retried once.
        if (const auto account =
                core::refresh_account(client.credentials(), ctx.client.base_url())) {
            (void)leetcode::save_credentials(account->creds);
            std::println(stderr, "{}",
                         a.dim("refreshed your LeetCode session from " + account->from));
            client = leetcode::Client(account->creds);
            id = start();
        }
    }
    if (!id) {
        std::println(stderr, "leetvim: {}", id.error().message);
        return 2;
    }
    auto r = client.wait(*id, q.slug);
    if (!r) {
        std::println(stderr, "leetvim: {}", r.error().message);
        return 2;
    }
    const auto& t = theme();
    const bool ok = r->accepted() && (r->is_submission || r->correct_answer);
    std::println("{}{}",
                 a.rgb(ok ? "✓ Accepted" : "✗ " + r->status_msg, ok ? t.success : t.error, true),
                 r->total_testcases > 0
                     ? a.dim(std::format("  {}/{} testcases", r->total_correct, r->total_testcases))
                     : "");
    if (!r->compile_error.empty()) std::println("{}", a.rgb(r->compile_error, t.error));
    if (!r->runtime_error.empty()) std::println("{}", a.rgb(r->runtime_error, t.error));
    if (submit) {
        if (r->accepted()) {
            if (r->runtime_percentile) {
                std::println("runtime  {:>9}  beats {:5.1f}%  {}", r->runtime,
                             *r->runtime_percentile,
                             bar(a, *r->runtime_percentile / 100, 20, t.success));
            }
            if (r->memory_percentile) {
                std::println("memory   {:>9}  beats {:5.1f}%  {}", r->memory, *r->memory_percentile,
                             bar(a, *r->memory_percentile / 100, 20, t.accent));
            }
        } else if (!r->last_testcase.empty()) {
            std::println("{}\n{}", a.dim("input"), r->last_testcase);
            std::println("{} {}", a.dim("output  "), a.rgb(r->code_output, t.error));
            std::println("{} {}", a.dim("expected"), a.rgb(r->expected_output, t.success));
            if (!r->std_output.empty()) std::println("{}\n{}", a.dim("stdout"), r->std_output);
        }
        return r->accepted() ? 0 : 1;
    }
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const bool pass = i < r->compare_result.size() && r->compare_result[i] == '1';
        const auto at = [&](const std::vector<std::string>& v) { return i < v.size() ? v[i] : ""; };
        std::println("\n{} {}", a.rgb(pass ? "✓" : "✗", pass ? t.success : t.error, true),
                     a.bold(std::format("case {}", i + 1)));
        for (const auto line : str::lines(cases[i])) std::println("  {}", a.dim(line));
        std::println("  output   {}", a.rgb(at(r->answers), pass ? t.success : t.error));
        std::println("  expected {}", at(r->expected_answers));
        if (const auto so = at(r->stdout_per_case); !so.empty()) std::println("  stdout   {}", so);
    }
    if (!r->runtime.empty()) std::println("\n{}", a.dim(r->runtime));
    return ok ? 0 : 1;
}

std::string read_secret(const std::string& prompt) {
    std::print(stderr, "{}", prompt);
    termios saved{};
    const bool tty = isatty(STDIN_FILENO) != 0 && tcgetattr(STDIN_FILENO, &saved) == 0;
    if (tty) {
        termios quiet = saved;
        quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &quiet);
    }
    std::string line;
    std::getline(std::cin, line);
    if (tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
        std::println(stderr, "");
    }
    return line;
}

void link(const leetcode::Credentials& creds, const leetcode::UserStatus& status,
          std::string_view from) {
    const Ansi a;
    if (!leetcode::save_credentials(creds)) {
        std::println(stderr, "leetvim: could not write {}", leetcode::credentials_path().string());
        return;
    }
    std::println("{} linked {}{} from {}", a.rgb("✓", theme().success, true),
                 a.bold(status.username), status.premium ? " (premium)" : "", from);
    if (!creds.source.empty()) {
        std::println("{}",
                     a.dim("leetvim refreshes the session from that browser when it expires."));
    }
}

int login_with_cookie(const Context& ctx, std::string cookie) {
    if (cookie.empty()) {
        std::println(stderr,
                     "Paste your leetcode.com Cookie header (browser DevTools → Network → any "
                     "leetcode.com request → Request Headers → Cookie).");
        cookie = read_secret("cookie: ");
    }
    const auto creds = leetcode::parse_cookie_string(cookie);
    if (!creds) {
        std::println(stderr, "leetvim: the cookie must contain LEETCODE_SESSION and csrftoken");
        return 1;
    }
    const auto account = core::verify_account(*creds, ctx.client.base_url(), "a pasted cookie");
    if (!account) {
        std::println(stderr, "leetvim: {}", account.error().message);
        return 1;
    }
    link(account->creds, account->status, account->from);
    return 0;
}

int cmd_login(const Context& ctx, const std::string& cookie, bool paste,
              const std::string& browser) {
    if (paste || !cookie.empty()) return login_with_cookie(ctx, cookie);

    const Ansi a;
    std::println(stderr, "Looking for your LeetCode session in your browsers…");
    const auto found = core::discover_accounts(
        ctx.client.base_url(),
        {.domain = core::cookie_domain(ctx.client.base_url()),
         .only = browser,
         .on_status = [&](std::string_view s) { std::println(stderr, "  {}", a.dim(s)); }});

    if (found.accounts.empty()) {
        if (!found.searched.empty()) {
            std::println(stderr, "Checked {}.", str::join(found.searched, ", "));
        }
        for (const auto& note : found.notes) std::println(stderr, "  {}", a.dim(note));
        std::println(stderr,
                     "No signed-in LeetCode session found. Sign in at {} in your browser and run "
                     "`leetvim login` again, or `leetvim login --paste` to paste a cookie.",
                     ctx.client.base_url());
        return 1;
    }
    std::size_t pick = 0;
    if (found.accounts.size() > 1 && isatty(STDIN_FILENO) != 0) {
        for (std::size_t i = 0; i < found.accounts.size(); ++i) {
            const auto& acc = found.accounts[i];
            std::println(stderr, "  [{}] {}{}  {}", i + 1, a.bold(acc.status.username),
                         acc.status.premium ? " (premium)" : "", a.dim(acc.from));
        }
        std::print(stderr, "Link which account? [1] ");
        std::string line;
        std::getline(std::cin, line);
        std::size_t n = 0;
        const auto [ptr, ec] = std::from_chars(line.data(), line.data() + line.size(), n);
        if (ec == std::errc{} && n >= 1 && n <= found.accounts.size()) {
            pick = n - 1;
        }
    }
    const auto& acc = found.accounts[pick];
    link(acc.creds, acc.status, acc.from);
    return 0;
}

int cmd_stats(const Context& ctx, std::string username) {
    if (username.empty()) {
        if (ctx.client.has_credentials()) {
            if (auto st = ctx.client.user_status(); st && st->signed_in) username = st->username;
        }
        if (username.empty()) username = ctx.config.account.username;
    }
    if (username.empty()) {
        std::println(stderr, "leetvim: give a username, set `username` in the config, or log in");
        return 1;
    }
    const auto stats = ctx.client.user_stats(username);
    if (!stats) {
        std::println(stderr, "leetvim: {}", stats.error().message);
        return 1;
    }
    const Ansi a;
    const auto& t = theme();
    std::println("{}  rank {}  streak {}d  active {}d", a.bold(stats->username),
                 ui::thousands(stats->ranking), stats->streak, stats->total_active_days);
    for (const auto d : {Difficulty::Easy, Difficulty::Medium, Difficulty::Hard}) {
        const int got = stats->solved.get(d);
        const int of = stats->questions.get(d);
        std::println(
            "{} {} {}", unicode::pad_right(difficulty(a, d), a.on ? 6 + 25 : 7),
            bar(a, of > 0 ? static_cast<double>(got) / of : 0, 30, ui::difficulty_color(t, d)),
            std::format("{}/{}", got, of));
    }
    std::println("total  {}/{}", stats->solved.all, stats->questions.all);
    if (stats->contest) {
        std::println("contest rating {:.0f} · top {:.1f}% · {} contests", stats->contest->rating,
                     stats->contest->top_percentage, stats->contest->attended);
    }
    return 0;
}

int cmd_sync(const Context& ctx) {
    auto result = ctx.client.problems(
        [](int done, int total) { std::print(stderr, "\rsyncing {}/{}", done, total); });
    std::println(stderr, "");
    if (!result) {
        std::println(stderr, "leetvim: {}", result.error().message);
        return 1;
    }
    [[maybe_unused]] const bool ok = ctx.cache.save_problems(*result);
    std::println("{} problems cached in {}", result->size(), ctx.cache.dir().string());
    return 0;
}

int cmd_config(const Context& ctx, bool init, bool edit) {
    std::error_code ec;
    if ((init || edit) && !fs::exists(ctx.config_path, ec)
        && !paths::write_file(ctx.config_path, core::default_config_text())) {
        std::println(stderr, "leetvim: cannot write {}", ctx.config_path.string());
        return 1;
    }
    if (edit) {
        auto argv = core::resolve_editor(ctx.config.editor.command);
        if (argv.empty()) return 1;
        argv.push_back(ctx.config_path.string());
        std::vector<char*> ptrs;
        ptrs.reserve(argv.size() + 1);
        for (auto& s : argv) ptrs.push_back(s.data());
        ptrs.push_back(nullptr);
        execvp(ptrs[0], ptrs.data());
        return 127;
    }
    std::println("{}", ctx.config_path.string());
    return 0;
}

} // namespace

int run_cli(int argc, const char* const* argv) {
    CLI::App cli{"leetvim: LeetCode in your terminal, solved in your own vim", "leetvim"};
    cli.set_version_flag("-V,--version", std::string("leetvim ") + LEETVIM_VERSION);
    cli.require_subcommand(0, 1);
    std::string config_file = core::default_config_path().string();
    cli.add_option("-c,--config", config_file, "config file")->capture_default_str();

    auto* daily = cli.add_subcommand("daily", "open today's question in the TUI");
    bool daily_print = false;
    daily->add_flag("-p,--print", daily_print, "print the description instead");

    auto* open = cli.add_subcommand("open", "open a problem in the TUI");
    std::string open_query;
    open->add_option("problem", open_query, "id, slug or title")->required();

    auto* show = cli.add_subcommand("show", "print a problem's description");
    std::string show_query;
    bool show_md = false;
    show->add_option("problem", show_query, "id, slug or title")->required();
    show->add_flag("-m,--markdown", show_md, "print Markdown");

    auto* list = cli.add_subcommand("list", "list problems");
    std::vector<std::string> levels;
    std::vector<std::string> statuses;
    std::vector<std::string> tags;
    std::string query;
    int limit = 0;
    list->add_option("-d,--difficulty", levels, "easy, medium, hard");
    list->add_option("-s,--status", statuses, "todo, attempted, solved");
    list->add_option("-t,--tag", tags, "topic slug or name");
    list->add_option("-q,--query", query, "title substring or id");
    list->add_option("-n,--limit", limit, "at most this many rows");

    auto* test = cli.add_subcommand("test", "run a solution file's testcases on LeetCode");
    std::string test_file;
    test->add_option("file", test_file, "solution file")->required()->check(CLI::ExistingFile);
    auto* submit = cli.add_subcommand("submit", "submit a solution file");
    std::string submit_file;
    submit->add_option("file", submit_file, "solution file")->required()->check(CLI::ExistingFile);

    auto* login = cli.add_subcommand(
        "login", "link your LeetCode account (imported from your browser's session)");
    std::string cookie;
    std::string browser;
    bool paste = false;
    login->add_option("-b,--browser", browser, "only look in this browser (e.g. chrome, helium)");
    login->add_flag("-p,--paste", paste, "paste a Cookie header instead");
    login->add_option("--cookie", cookie, "Cookie header value, non-interactively");
    const auto* logout = cli.add_subcommand("logout", "forget the saved session");

    auto* stats = cli.add_subcommand("stats", "print profile statistics");
    std::string username;
    stats->add_option("username", username, "defaults to you");

    const auto* sync = cli.add_subcommand("sync", "re-download the problem list");

    auto* config = cli.add_subcommand("config", "print the config path");
    bool config_init = false;
    bool config_edit = false;
    config->add_flag("--init", config_init, "write a commented default config");
    config->add_flag("--edit", config_edit, "open the config in your editor");

    auto* update = cli.add_subcommand("update", "update a leetvim installed by the install script");
    std::string update_version;
    update->add_option("version", update_version, "a release such as 0.2.0; the latest by default");

    try {
        cli.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return cli.exit(e);
    }
    if (*update) return cmd_update(update_version);

    const fs::path config_path = paths::expand_user(config_file);
    const auto loaded = core::load_config(config_path);
    const auto ctx = [&] { return make_context(loaded, config_path); };

    if (*show) return cmd_show(ctx(), show_query, show_md);
    if (*list) return cmd_list(ctx(), levels, statuses, tags, query, limit);
    if (*test) return cmd_judge(ctx(), test_file, false);
    if (*submit) return cmd_judge(ctx(), submit_file, true);
    if (*login) return cmd_login(ctx(), cookie, paste, browser);
    if (*logout) {
        leetcode::clear_credentials();
        std::println("signed out");
        return 0;
    }
    if (*stats) return cmd_stats(ctx(), username);
    if (*sync) return cmd_sync(ctx());
    if (*config) return cmd_config(ctx(), config_init, config_edit);
    if (*daily && daily_print) {
        const auto c = ctx();
        const auto d = c.client.daily();
        if (!d) {
            std::println(stderr, "leetvim: {}", d.error().message);
            return 1;
        }
        return cmd_show(c, d->problem.slug, false);
    }

    App::Options options;
    if (*daily) options.open = "daily";
    if (*open) options.open = open_query;
    App app(loaded.config, config_path, loaded.warnings);
    return app.run(options);
}

} // namespace lv::app
