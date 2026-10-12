#include "app/app.hpp"

#include "app/home.hpp"
#include "app/overlays.hpp"
#include "app/settings.hpp"
#include "app/solve.hpp"
#include "app/stats.hpp"
#include "app/ui.hpp"
#include "content/html.hpp"
#include "core/account.hpp"
#include "core/editor.hpp"
#include "leetcode/credentials.hpp"
#include "util/log.hpp"
#include "util/paths.hpp"
#include "util/strings.hpp"
#include "util/unicode.hpp"

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <format>

// glibc's <unistd.h> declares this too, but macOS and other libcs don't.
extern char** environ; // NOLINT(readability-redundant-declaration)

namespace lv::app {

namespace fs = std::filesystem;
using leetcode::Status;
using tui::Key;
using tui::KeyEvent;
using tui::ModCtrl;

namespace {

constexpr int min_width = 60;
constexpr int min_height = 18;

/// Keys that put vim/nvim in normal mode from any mode (Ctrl-\ Ctrl-N).
constexpr std::string_view vim_normal = "\x1c\x0e";

/// Escapes a path for a vim command line.
std::string vim_escape(const std::string& path) {
    std::string out;
    for (const char c : path) {
        if (std::string_view(" \\%#|\"'").contains(c)) out += '\\';
        out += c;
    }
    return out;
}

/// Starts a detached helper (browser opener, clipboard tool). Returns its pid or -1.
pid_t spawn_quiet(const std::vector<std::string>& argv, int stdin_fd = -1) {
    posix_spawn_file_actions_t actions{};
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    if (stdin_fd >= 0) {
        posix_spawn_file_actions_adddup2(&actions, stdin_fd, STDIN_FILENO);
    } else {
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    }
    std::vector<std::string> args = argv;
    std::vector<char*> ptrs;
    ptrs.reserve(args.size() + 1);
    for (auto& a : args) ptrs.push_back(a.data());
    ptrs.push_back(nullptr);
    pid_t pid = -1;
    const int rc = posix_spawnp(&pid, ptrs[0], &actions, nullptr, ptrs.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    return rc == 0 ? pid : -1;
}

std::vector<pid_t>& helper_pids() {
    static std::vector<pid_t> pids;
    return pids;
}

void reap_helpers() {
    auto& pids = helper_pids();
    std::erase_if(pids, [](pid_t pid) {
        int status = 0;
        return waitpid(pid, &status, WNOHANG) != 0;
    });
}

std::string join_lines(const std::vector<std::string>& cases) {
    std::string out;
    for (const auto& c : cases) {
        if (!out.empty()) out += '\n';
        out += c;
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------- lifecycle

Bindings Bindings::from(const core::Config::Keys& keys) {
    // The config only holds valid specs; the defaults are a fallback for a Config built in code.
    const core::Config::Keys defaults;
    const auto spec = [](const std::string& text, const std::string& fallback) {
        return tui::KeySpec::parse(text).value_or(
            tui::KeySpec::parse(fallback).value_or(tui::KeySpec{}));
    };
    return {.prefix = spec(keys.prefix, defaults.prefix),
            .run = spec(keys.run, defaults.run),
            .submit = spec(keys.submit, defaults.submit),
            .results = spec(keys.results, defaults.results),
            .testcases = spec(keys.testcases, defaults.testcases),
            .zoom = spec(keys.zoom, defaults.zoom),
            .back = spec(keys.back, defaults.back),
            .help = spec(keys.help, defaults.help)};
}

App::App(core::Config config, fs::path config_path, std::vector<std::string> warnings)
    : config_(std::move(config)), config_path_(std::move(config_path)),
      warnings_(std::move(warnings)), theme_(&tui::theme_named(config_.ui.theme)),
      keys_(Bindings::from(config_.keys)), client_(leetcode::load_credentials()),
      cache_(paths::cache_dir()),
      workspace_(paths::expand_user(config_.solution.workspace), config_.solution.filename),
      home_(std::make_unique<HomeView>(*this)), stats_view_(std::make_unique<StatsView>(*this)),
      solve_view_(std::make_unique<SolveView>(*this)), current_(home_.get()),
      dispatcher_(std::make_unique<Dispatcher>(4)) {
    renderer_.set_truecolor(truecolor());
}

App::~App() {
    leetcode::Client::abort_all();
    dispatcher_->shutdown();
    sessions_.clear(); // terminates editors while the terminal is still ours
    terminal_.leave();
}

int App::run(const Options& options) {
    if (!terminal_.enter({.mouse = config_.ui.mouse})) {
        (void)std::fputs("leetvim: the TUI needs an interactive terminal\n", stderr);
        return 1;
    }
    size_ = tui::Terminal::size();
    renderer_.resize(size_.width, size_.height);
    tui::Terminal::set_title("leetvim");

    for (const auto& w : warnings_) toast("config: " + w, Level::Warning);
    load_cached_problems();
    sync_problems(false);
    refresh_user();
    welcome();

    if (options.open == "daily") {
        open_daily();
    } else if (!options.open.empty()) {
        if (const auto* p = find_problem(options.open)) {
            open_problem(p->slug);
        } else if (options.open.find_first_not_of("0123456789") == std::string::npos) {
            pending_open_ = options.open; // an id; resolve once the list arrives
        } else {
            open_problem(options.open);
        }
    }

    loop();
    return 0;
}

// ---------------------------------------------------------------- event loop

int App::poll_timeout() const {
    if (input_.pending()) return 20;
    bool animating = data_.syncing || data_.stats_loading || !data_.loading_questions.empty()
                     || !toasts_.empty() || quitting_;
    for (const auto& [slug, s] : sessions_) {
        if (s->job == JobState::Saving) return 15;
        animating = animating || s->job == JobState::Running || s->submissions_loading;
    }
    if (animating) return 80;
    const auto now = std::chrono::system_clock::now();
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return static_cast<int>(1000 - (ms % 1000)) + 1;
}

void App::loop() {
    std::array<char, 4096> buf{};
    while (!quit_) {
        std::vector<pollfd> fds;
        fds.push_back({.fd = tui::Terminal::input_fd(), .events = POLLIN, .revents = 0});
        fds.push_back({.fd = tui::Terminal::signal_fd(), .events = POLLIN, .revents = 0});
        fds.push_back({.fd = dispatcher_->wake_fd(), .events = POLLIN, .revents = 0});
        for (const auto& [slug, s] : sessions_) {
            for (const auto* pane : {s->editor.get(), s->test_editor.get()}) {
                if (pane != nullptr && pane->running()) {
                    fds.push_back({.fd = pane->fd(), .events = POLLIN, .revents = 0});
                }
            }
        }
        const int ready = poll(fds.data(), fds.size(), poll_timeout());
        if (ready < 0 && errno != EINTR) break;

        if ((fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) quit_ = true;
        if ((fds[1].revents & POLLIN) != 0) {
            while (const int sig = tui::Terminal::read_signal()) handle_signal(sig);
        }
        if ((fds[0].revents & POLLIN) != 0) {
            const ssize_t n = read(tui::Terminal::input_fd(), buf.data(), buf.size());
            if (n > 0) {
                input_.feed({buf.data(), static_cast<std::size_t>(n)});
            } else if (n < 0 && errno == EIO) {
                quit_ = true; // the terminal went away (with VMIN=0, n == 0 just means "no data")
            }
        } else if (ready == 0 && input_.pending()) {
            input_.flush(); // a lone ESC that no sequence followed
        }
        while (auto ev = input_.next()) {
            handle_event(*ev);
            dirty_ = true;
        }
        pump_terminals();
        if (dispatcher_->drain() > 0) dirty_ = true;

        if (current_->on_tick()) dirty_ = true;
        if (tick_sessions()) dirty_ = true;

        const auto now = Clock::now();
        if (now - last_tick_ > std::chrono::milliseconds(80)) {
            ++tick_;
            last_tick_ = now;
            const bool animating = poll_timeout() <= 80;
            if (animating) dirty_ = true;
        }
        const auto second = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        if (second != last_clock_second_) {
            last_clock_second_ = second;
            dirty_ = true;
        }
        const auto expired =
            std::erase_if(toasts_, [&](const Toast& t) { return t.expires < now; });
        if (expired > 0) dirty_ = true;

        if (quitting_ && (live_editors() == 0 || now > quit_deadline_)) quit_ = true;
        if (dirty_ && !quit_) render();
    }
}

void App::handle_signal(int sig) {
    switch (sig) {
    case SIGWINCH:
        size_ = tui::Terminal::size();
        renderer_.resize(size_.width, size_.height);
        dirty_ = true;
        break;
    case SIGTERM:
    case SIGHUP: quit_ = true; break;
    case SIGCONT:
        terminal_.resume();
        renderer_.invalidate();
        dirty_ = true;
        break;
    case SIGCHLD:
        reap_helpers();
        for (auto& [slug, s] : sessions_) {
            if (s->editor) s->editor->reap();
            if (s->test_editor) s->test_editor->reap();
        }
        break;
    default: break;
    }
}

void App::pump_terminals() {
    for (auto& [slug, s] : sessions_) {
        if (s->editor && s->editor->running()) {
            if (!s->editor->pump() || !s->editor->running()) {
                dirty_ = true;
                if (slug == active_session_ && current_ == solve_view_.get()) {
                    go_home();
                    toast(std::format("closed {}", s->file.filename().string()));
                }
            }
            if (s->editor && s->editor->take_dirty()) dirty_ = true;
        }
        if (s->test_editor) {
            if (s->test_editor->running()) s->test_editor->pump();
            if (!s->test_editor->running()) {
                s->test_editor.reset();
                s->panel_tab = 1;
                if (slug == active_session_) {
                    solve_view_->set_focus(SolveView::Focus::Panel);
                }
                toast("testcases updated", Level::Success);
            } else if (s->test_editor->take_dirty()) {
                dirty_ = true;
            }
        }
    }
}

void App::handle_event(const tui::Event& ev) {
    if (const auto* key = std::get_if<KeyEvent>(&ev)) {
        handle_key(*key);
    } else if (const auto* mouse = std::get_if<tui::MouseEvent>(&ev)) {
        if (overlays_.empty()) current_->on_mouse(*mouse);
    } else if (const auto* paste = std::get_if<tui::PasteEvent>(&ev)) {
        if (!overlays_.empty()) {
            overlays_.back()->on_paste(paste->text);
        } else {
            current_->on_paste(paste->text);
        }
    } else if (const auto* focus = std::get_if<tui::FocusEvent>(&ev);
               focus != nullptr && current_ == solve_view_.get()) {
        Session* s = session(active_session_);
        if (s != nullptr && s->editor && solve_view_->focus() == SolveView::Focus::Editor) {
            s->editor->set_focus(focus->gained);
        }
    }
}

void App::handle_key(const KeyEvent& ev) {
    if (ev.key == Key::None) return;
    if (!overlays_.empty()) {
        overlays_.back()->on_key(ev);
        std::erase_if(overlays_, [](const auto& o) { return o->closed(); });
        return;
    }
    if (current_->on_key(ev)) return;

    if (ev.is('q')) {
        if (current_ == home_.get()) {
            request_quit();
        } else {
            go_home();
        }
    } else if (ev.is('c', ModCtrl)) {
        request_quit();
    } else if (ev.is(':')) {
        push_overlay(std::make_unique<CommandOverlay>(*this));
    } else if (ev.is('?')) {
        push_overlay(std::make_unique<HelpOverlay>(*this));
    } else if (ev.is('L')) {
        run_command("lang");
    } else if (ev.is('T')) {
        run_command("theme");
    } else if (ev.is(',')) {
        push_overlay(std::make_unique<SettingsOverlay>(*this));
    } else if (ev.is('z', ModCtrl)) {
        terminal_.stop_process();
        renderer_.invalidate();
    } else if (ev.is('l', ModCtrl)) {
        renderer_.invalidate();
    }
}

void App::render() {
    dirty_ = false;
    auto& canvas = renderer_.canvas();
    canvas.clear(tui::Style{.fg = {}, .bg = base_bg()});
    const tui::Painter p(canvas, canvas.bounds());
    const tui::Rect area = canvas.bounds();
    const auto& t = *theme_;

    if (area.w < min_width || area.h < min_height) {
        const std::array lines{
            std::string("Terminal size too small:"),
            std::format("Width = {} Height = {}", area.w, area.h),
            std::format("Needed: Width = {} Height = {}", min_width, min_height),
        };
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const int y = (area.h / 2) - 1 + static_cast<int>(i);
            p.text(std::max(0, (area.w - unicode::display_width(lines.at(i))) / 2), y, lines.at(i),
                   ui::fg(i == 0 ? t.title : t.main_fg, i == 0 ? tui::AttrBold : tui::AttrNone));
        }
        tui::Terminal::write(renderer_.present(std::nullopt));
        return;
    }

    current_->draw(p, area);
    for (const auto& o : overlays_) o->draw(p, area);

    // Toasts stack in the bottom-right corner.
    int y = area.bottom() - 2;
    for (auto it = toasts_.rbegin(); it != toasts_.rend() && y > 2; ++it) {
        const tui::Color color = it->level == Level::Error     ? t.error
                                 : it->level == Level::Warning ? t.warning
                                 : it->level == Level::Success ? t.success
                                                               : t.hi_fg;
        const int w = std::min(area.w - 4, unicode::display_width(it->text) + 4);
        const tui::Rect r{area.right() - w - 2, y - 2, w, 3};
        p.clear(r, base_bg());
        tui::draw_box(p, r, ui::fg(color), config_.ui.rounded_corners);
        p.text(r.x + 2, r.y + 1, it->text, ui::fg(t.title), r.w - 4);
        y -= 3;
    }

    const auto cursor = overlays_.empty() ? current_->cursor() : overlays_.back()->cursor();
    tui::Terminal::write(renderer_.present(cursor));
}

// ---------------------------------------------------------------- data

std::string App::username() const {
    if (data_.user && data_.user->signed_in) return data_.user->username;
    return config_.account.username;
}

Session* App::session(const std::string& slug) {
    const auto it = sessions_.find(slug);
    return it == sessions_.end() ? nullptr : it->second.get();
}

std::size_t App::live_editors() const {
    std::size_t n = 0;
    for (const auto& [slug, s] : sessions_) {
        if (s->editor && s->editor->running()) ++n;
        if (s->test_editor && s->test_editor->running()) ++n;
    }
    return n;
}

const leetcode::ProblemSummary* App::problem(const std::string& slug) const {
    const auto it = data_.by_slug.find(slug);
    return it == data_.by_slug.end() ? nullptr : &data_.problems[it->second];
}

const leetcode::ProblemSummary* App::find_problem(std::string_view query) const {
    const std::string q(str::trim(query));
    if (q.empty()) return nullptr;
    if (const auto* p = problem(q)) return p;
    for (const auto& p : data_.problems) {
        if (p.frontend_id == q || str::iequals(p.title, q)) return &p;
    }
    return nullptr;
}

void App::load_cached_problems() {
    if (auto cached = cache_.load_problems()) {
        data_.synced_at = cached->fetched_at;
        set_problems(std::move(cached->problems));
    }
}

void App::set_problems(std::vector<leetcode::ProblemSummary> problems) {
    data_.problems = std::move(problems);
    data_.by_slug.clear();
    for (std::size_t i = 0; i < data_.problems.size(); ++i)
        data_.by_slug[data_.problems[i].slug] = i;
    ++data_.problems_version;
    dirty_ = true;
    if (!pending_open_.empty() && pending_open_ != "daily") {
        if (const auto* p = find_problem(pending_open_)) {
            const std::string slug = p->slug;
            pending_open_.clear();
            open_problem(slug);
        }
    }
}

void App::mark_status(const std::string& slug, Status status) {
    const auto it = data_.by_slug.find(slug);
    if (it == data_.by_slug.end()) return;
    auto& p = data_.problems[it->second];
    if (status == Status::Attempted && p.status == Status::Solved) return;
    if (p.status == status) return;
    p.status = status;
    ++data_.problems_version;
    dispatcher_->run([cache = cache_, problems = data_.problems](const std::stop_token&) {
        [[maybe_unused]] const bool ok = cache.save_problems(problems);
    });
}

void App::sync_problems(bool force) {
    if (data_.syncing) return;
    const auto age = std::chrono::system_clock::now() - data_.synced_at;
    if (!force && !data_.problems.empty()
        && age < std::chrono::hours(config_.account.refresh_hours))
        return;
    data_.syncing = true;
    data_.sync_done = 0;
    data_.sync_total = 0;
    data_.sync_error.clear();
    dispatcher_->run([this, client = client_, cache = cache_, force](const std::stop_token& stop) {
        auto result = client.problems(
            [this](int done, int total) {
                dispatcher_->post([this, done, total] {
                    data_.sync_done = done;
                    data_.sync_total = total;
                });
            },
            stop);
        if (result) [[maybe_unused]]
            const bool ok = cache.save_problems(*result);
        dispatcher_->post([this, result = std::move(result), force] mutable {
            data_.syncing = false;
            if (!result) {
                data_.sync_error = "sync failed: " + result.error().message;
                toast(data_.sync_error, Level::Error);
                return;
            }
            data_.synced_at = std::chrono::system_clock::now();
            const auto count = result->size();
            set_problems(std::move(*result));
            // A status snapshot that arrived during the sync was applied to the old list.
            if (signed_in()) refresh_statuses();
            if (force) toast(std::format("synced {} problems", count), Level::Success);
        });
    });
}

std::shared_ptr<const leetcode::Question> App::question(const std::string& slug) {
    if (const auto it = data_.questions.find(slug); it != data_.questions.end()) return it->second;
    if (data_.loading_questions.contains(slug)) return nullptr;
    data_.loading_questions.insert(slug);
    data_.question_errors.erase(slug);
    dispatcher_->run([this, slug = slug, client = client_, cache = cache_](const std::stop_token&) {
        std::optional<leetcode::Question> q = cache.load_question(slug);
        std::string error;
        if (!q) {
            if (auto r = client.question(slug)) {
                q = std::move(*r);
                [[maybe_unused]] const bool ok = cache.save_question(*q);
            } else {
                error = r.error().message;
            }
        }
        dispatcher_->post([this, slug = slug, q = std::move(q), error = error] mutable {
            data_.loading_questions.erase(slug);
            descriptions_.erase(slug);
            if (!q) {
                data_.question_errors[slug] = error.empty() ? "question not found" : error;
                if (slug == active_session_ && current_ == solve_view_.get()) {
                    toast(data_.question_errors[slug], Level::Error);
                }
                return;
            }
            auto shared = std::make_shared<const leetcode::Question>(std::move(*q));
            data_.questions[slug] = shared;
            if (Session* s = session(slug); s != nullptr && !s->question) setup_session(*s);
        });
    });
    return nullptr;
}

const std::vector<content::Line>& App::description(const leetcode::Question& q, int width,
                                                   bool hints) {
    auto& d = descriptions_[q.slug];
    if (d.width == width && d.hints == hints) return d.lines;
    content::Document doc = content::parse_html(q.content_html);
    using content::Block;
    using content::BlockKind;
    using content::Span;
    const auto heading = [&](std::string text) {
        doc.blocks.push_back(Block{
            .kind = BlockKind::Heading, .spans = {Span{.text = std::move(text)}}, .level = 3});
    };
    if (!q.tags.empty()) {
        heading("Topics");
        Block tags{.kind = BlockKind::Paragraph};
        for (const auto& tag : q.tags) {
            if (!tags.spans.empty()) tags.spans.push_back(Span{.text = "  "});
            tags.spans.push_back(Span{.text = tag.name, .marks = content::MarkCode});
        }
        doc.blocks.push_back(std::move(tags));
    }
    if (!q.hints.empty()) {
        heading("Hints");
        if (hints) {
            for (std::size_t i = 0; i < q.hints.size(); ++i) {
                Block item{
                    .kind = BlockKind::ListItem, .level = 1, .marker = std::format("{}.", i + 1)};
                for (const auto& b : content::parse_html(q.hints[i]).blocks) {
                    if (!item.spans.empty()) item.spans.push_back(Span{.text = " "});
                    item.spans.insert(item.spans.end(), b.spans.begin(), b.spans.end());
                }
                doc.blocks.push_back(std::move(item));
            }
        } else {
            doc.blocks.push_back(Block{
                .kind = BlockKind::Paragraph,
                .spans = {Span{.text = std::format("{} hint{} hidden, press H to reveal",
                                                   q.hints.size(), q.hints.size() == 1 ? "" : "s"),
                               .marks = content::MarkItalic}}});
        }
    }
    if (!q.similar.empty()) {
        heading("Similar Questions");
        for (const auto& sim : q.similar) {
            const auto* p = problem(sim.slug);
            std::string text =
                p != nullptr ? std::format("{}. {}", p->frontend_id, sim.title) : sim.title;
            doc.blocks.push_back(Block{
                .kind = BlockKind::ListItem,
                .spans = {Span{.text = std::move(text)},
                          Span{.text = std::format(" ({})", leetcode::to_string(sim.difficulty)),
                               .marks = content::MarkItalic}},
                .level = 1,
                .marker = p != nullptr ? std::string(ui::status_icon(p->status)) == " "
                                             ? "•"
                                             : std::string(ui::status_icon(p->status))
                                       : "•"});
        }
    }
    d.lines = content::layout(doc, std::max(width, 10));
    d.width = width;
    d.hints = hints;
    return d.lines;
}

void App::refresh_user() {
    fetch_daily();
    if (!client_.has_credentials()) {
        data_.user.reset();
        fetch_stats();
        return;
    }
    dispatcher_->run([this, client = client_, base = client_.base_url()](const std::stop_token&) {
        auto status = client.user_status();
        // An expired session imported from a browser is renewed from that browser.
        std::optional<core::Account> refreshed;
        if (status && !status->signed_in)
            refreshed = core::refresh_account(client.credentials(), base);
        dispatcher_->post([this, status = std::move(status), refreshed = std::move(refreshed)] {
            if (refreshed) {
                apply_account(*refreshed, true);
                return;
            }
            if (!status) {
                toast("could not reach LeetCode: " + status.error().message, Level::Error);
                fetch_stats();
                return;
            }
            data_.user = *status;
            if (status->signed_in) {
                refresh_statuses();
            } else {
                toast("your LeetCode session expired: sign in on leetcode.com, then :login",
                      Level::Warning);
            }
            fetch_stats();
        });
    });
}

void App::refresh_statuses() {
    if (!client_.has_credentials()) return;
    dispatcher_->run([this, client = client_](const std::stop_token&) {
        auto snapshot = client.statuses();
        if (!snapshot) return;
        dispatcher_->post([this, snapshot = std::move(*snapshot)] {
            int changed = 0;
            for (const auto& [slug, status] : snapshot.statuses) {
                const auto it = data_.by_slug.find(slug);
                if (it == data_.by_slug.end()) continue;
                auto& p = data_.problems[it->second];
                if (p.status != status) {
                    p.status = status;
                    ++changed;
                }
            }
            if (changed == 0) return;
            ++data_.problems_version;
            dispatcher_->run([cache = cache_, problems = data_.problems](const std::stop_token&) {
                [[maybe_unused]] const bool ok = cache.save_problems(problems);
            });
        });
    });
}

void App::fetch_stats() {
    const std::string name = username();
    if (name.empty()) return;
    data_.stats_loading = true;
    data_.stats_error.clear();
    const bool signed_in = this->signed_in();
    dispatcher_->run([this, client = client_, name = name, signed_in](const std::stop_token&) {
        auto stats = client.user_stats(name);
        std::optional<leetcode::StreakInfo> streak;
        if (signed_in) {
            if (auto s = client.streak()) streak = *s;
        }
        dispatcher_->post([this, stats = std::move(stats), streak] {
            data_.stats_loading = false;
            if (!stats) {
                data_.stats_error = "stats: " + stats.error().message;
                return;
            }
            data_.stats = *stats;
            if (streak) data_.streak = streak;
        });
    });
}

void App::fetch_daily() {
    dispatcher_->run([this, client = client_](const std::stop_token&) {
        auto daily = client.daily();
        dispatcher_->post([this, daily = std::move(daily)] {
            if (!daily) {
                if (pending_open_ == "daily") {
                    pending_open_.clear();
                    toast("could not load the daily question: " + daily.error().message,
                          Level::Error);
                }
                return;
            }
            data_.daily = *daily;
            if (pending_open_ == "daily") {
                pending_open_.clear();
                open_problem(daily->problem.slug);
            }
        });
    });
}

// ---------------------------------------------------------------- navigation

void App::open_problem(const std::string& slug) {
    auto& slot = sessions_[slug];
    if (!slot) {
        slot = std::make_unique<Session>();
        slot->slug = slug;
        slot->lang = core::find_language(config_.solution.lang);
        if (slot->lang == nullptr) slot->lang = core::find_language("cpp");
    }
    Session& s = *slot;
    s.last_used = Clock::now();
    active_session_ = slug;
    current_ = solve_view_.get();
    solve_view_->set_focus(SolveView::Focus::Editor);
    if (!s.question) {
        if (question(slug) != nullptr) setup_session(s);
    } else if (!s.editor || !s.editor->running()) {
        respawn_editor(s);
    }
    retire_idle_editors();
    const auto* p = problem(slug);
    tui::Terminal::set_title(
        p != nullptr ? std::format("leetvim · {}. {}", p->frontend_id, p->title) : "leetvim");
    dirty_ = true;
}

void App::retire_idle_editors() {
    // Background sessions keep their editor so returning is instant; past a few, the
    // least recently used one saves and exits (vim) or is closed, and respawns on reopen.
    constexpr std::size_t max_background = 4;
    std::vector<Session*> idle;
    for (auto& [slug, s] : sessions_) {
        if (slug != active_session_ && s->editor && s->editor->running()) idle.push_back(s.get());
    }
    if (idle.size() <= max_background) return;
    std::ranges::sort(idle, {}, &Session::last_used);
    for (std::size_t i = 0; i + max_background < idle.size(); ++i) {
        Session& s = *idle[i];
        if (core::is_vim_like(s.editor_argv)) {
            s.editor->send_bytes(std::string(vim_normal) + ":wqa\r");
        } else {
            s.editor->terminate();
        }
    }
}

void App::setup_session(Session& s) {
    const auto it = data_.questions.find(s.slug);
    if (it == data_.questions.end()) return;
    s.question = it->second;
    const auto& q = *s.question;
    if (q.snippets.empty()) {
        toast(q.paid_only ? "premium question: sign in with a premium account"
                          : "this problem has no code snippets",
              Level::Warning);
        return;
    }
    if (q.snippet(s.lang->slug) == nullptr) {
        const core::Language* fallback = nullptr;
        for (const auto* name : {"python3", "cpp", "java", "mysql", "bash"}) {
            if (q.snippet(name) != nullptr) {
                fallback = core::find_language(name);
                break;
            }
        }
        if (fallback == nullptr) fallback = core::find_language(q.snippets.front().lang_slug);
        if (fallback == nullptr) {
            toast("no supported language for this problem", Level::Error);
            return;
        }
        toast(std::format("{} isn't available here, using {}", s.lang->name, fallback->name),
              Level::Warning);
        s.lang = fallback;
    }
    respawn_editor(s);
}

void App::respawn_editor(Session& s) {
    if (!s.question) return;
    std::string error;
    if (!spawn_editor(s, error)) toast(error, Level::Error);
    dirty_ = true;
}

bool App::spawn_editor(Session& s, std::string& error) {
    const auto& q = *s.question;
    const auto lang_slug = std::string(s.lang->slug);
    const auto before = config_.solution.inject_before.find(lang_slug);
    const auto after = config_.solution.inject_after.find(lang_slug);
    auto file = workspace_.ensure_solution(
        q, *s.lang, client_.problem_url(q.slug),
        before != config_.solution.inject_before.end() ? before->second : "",
        after != config_.solution.inject_after.end() ? after->second : "");
    if (!file) {
        error = file.error();
        return false;
    }
    s.file = *file;
    s.editor_argv = core::resolve_editor(config_.editor.command);
    if (s.editor_argv.empty()) {
        error = "no editor found: install nvim/vim or set editor.command in the config";
        return false;
    }
    s.sync_file = fs::temp_directory_path() / std::format("leetvim-{}-{}.sync", getpid(), s.slug);

    auto argv = s.editor_argv;
    argv.push_back(s.file.string());
    const auto* solve = solve_view_.get();
    const tui::Rect inner = solve->layout(screen(), s).editor.inset(1);
    s.editor = std::make_unique<tui::TermPane>();
    return s.editor->spawn({.argv = argv,
                            .cwd = workspace_.root(),
                            .env = {{"LEETVIM", "1"},
                                    {"LEETVIM_SLUG", s.slug},
                                    {"LEETVIM_FILE", s.file.string()},
                                    {"LEETVIM_SYNC", s.sync_file.string()}},
                            .rows = std::max(inner.h, 2),
                            .cols = std::max(inner.w, 10)},
                           error);
}

void App::open_daily() {
    if (data_.daily) {
        open_problem(data_.daily->problem.slug);
        return;
    }
    pending_open_ = "daily";
    toast("loading today's question…");
    fetch_daily();
}

void App::go_home() {
    current_ = home_.get();
    if (!active_session_.empty()) home_->select_slug(active_session_);
    tui::Terminal::set_title("leetvim");
    dirty_ = true;
}

void App::show_stats() {
    current_ = stats_view_.get();
    if (!data_.stats && !data_.stats_loading) refresh_user();
    dirty_ = true;
}

void App::push_overlay(std::unique_ptr<Overlay> overlay) {
    overlays_.push_back(std::move(overlay));
    dirty_ = true;
}

void App::toast(std::string text, Level level) {
    log::debug("toast: {}", text);
    const auto ttl = level == Level::Error ? std::chrono::seconds(6) : std::chrono::seconds(3);
    toasts_.push_back({.text = std::move(text), .level = level, .expires = Clock::now() + ttl});
    if (toasts_.size() > 4) toasts_.erase(toasts_.begin());
    dirty_ = true;
}

void App::request_quit() {
    const std::size_t live = live_editors();
    if (live == 0) {
        quit_ = true;
        return;
    }
    push_overlay(std::make_unique<ConfirmOverlay>(
        *this, "quit",
        std::vector<std::string>{
            std::format("{} editor{} still open.", live, live == 1 ? " is" : "s are")},
        std::vector<ConfirmOverlay::Choice>{
            {.key = 'w', .label = "save all and quit", .action = [this] { finish_quit(); }},
            {.key = 'q', .label = "quit without saving", .action = [this] { quit_ = true; }},
        }));
}

void App::finish_quit() {
    for (auto& [slug, s] : sessions_) {
        for (auto* pane : {s->editor.get(), s->test_editor.get()}) {
            if (pane == nullptr || !pane->running()) continue;
            if (core::is_vim_like(s->editor_argv)) {
                pane->send_bytes(std::string(vim_normal) + ":wqa\r");
            } else {
                pane->terminate();
            }
        }
    }
    quitting_ = true;
    quit_deadline_ = Clock::now() + std::chrono::seconds(3);
}

// ---------------------------------------------------------------- settings

bool App::truecolor() const {
    return config_.ui.truecolor == "auto" ? tui::Terminal::truecolor_supported()
                                          : config_.ui.truecolor == "true";
}

void App::persist(const core::Option& option) {
    if (auto err =
            core::update_config_file(config_path_, option, core::toml_value(config_, option));
        !err.empty()) {
        toast(err, Level::Error);
    }
}

void App::set_theme(std::string_view name, bool persist_it) {
    set_option("ui.theme", name, persist_it);
}

void App::set_language(std::string_view slug, bool persist_it) {
    const auto* lang = core::find_language(slug);
    if (lang == nullptr) {
        toast(std::format("unknown language '{}'", slug), Level::Error);
        return;
    }
    set_option("solution.lang", lang->slug, persist_it);
    toast(std::format("default language: {}", lang->name), Level::Success);
}

void App::set_option(std::string_view key, std::string_view value, bool persist_it) {
    const auto* option = core::find_option(str::trim(key));
    if (option == nullptr) {
        toast(std::format("unknown option '{}'", str::trim(key)), Level::Error);
        return;
    }
    if (auto err = core::set_option(config_, *option, str::trim(value)); !err.empty()) {
        toast(err, Level::Error);
        return;
    }
    apply_option(*option);
    if (persist_it) persist(*option);
}

void App::apply_option(const core::Option& option) {
    const std::string key = option.key();
    if (key == "ui.theme") {
        theme_ = &tui::theme_named(config_.ui.theme);
    } else if (key == "ui.truecolor") {
        renderer_.set_truecolor(truecolor());
    } else if (key == "ui.mouse") {
        terminal_.set_mouse(config_.ui.mouse);
    } else if (key == "solution.workspace" || key == "solution.filename") {
        workspace_ = core::Workspace(paths::expand_user(config_.solution.workspace),
                                     config_.solution.filename);
    } else if (key == "account.username") {
        if (!signed_in()) fetch_stats();
    } else if (option.section == "keys") {
        keys_ = Bindings::from(config_.keys);
        for (auto& conflict : core::key_conflicts(config_))
            toast(std::move(conflict), Level::Warning);
    } else if (option.section == "list") {
        home_->apply_option(option.name);
    }
    renderer_.invalidate();
    dirty_ = true;
}

void App::link_account() {
    toast("looking for your LeetCode session in your browsers…");
    dispatcher_->run([this, base = client_.base_url()](const std::stop_token&) {
        auto found = core::discover_accounts(
            base, {.domain = core::cookie_domain(base),
                   .only = {},
                   .on_status = [this](std::string_view s) {
                       dispatcher_->post([this, msg = std::string(s)] { toast(msg); });
                   }});
        dispatcher_->post(
            [this, found = std::move(found)] mutable { finish_link(std::move(found)); });
    });
}

void App::finish_link(core::Discovery found) {
    if (found.accounts.size() == 1) {
        apply_account(found.accounts.front(), false);
        return;
    }
    if (found.accounts.empty()) {
        std::vector<std::string> message{
            found.searched.empty() ? std::string("No supported browser was found.")
                                   : std::format("No signed-in LeetCode session in {}.",
                                                 str::join(found.searched, ", ")),
            "Sign in at leetcode.com in your browser and run :login again, or paste your Cookie "
            "header (DevTools → Network → any leetcode.com request → Cookie):"};
        for (std::size_t i = 0; i < found.notes.size() && i < 3; ++i)
            message.push_back("· " + found.notes[i]);
        push_overlay(std::make_unique<PromptOverlay>(
            *this, "link your account", std::move(message),
            [this](const std::string& v) { login(v); }, true));
        return;
    }
    auto accounts = std::make_shared<std::vector<core::Account>>(std::move(found.accounts));
    std::vector<PickerOverlay::Item> items;
    for (std::size_t i = 0; i < accounts->size(); ++i) {
        const auto& acc = (*accounts)[i];
        items.push_back({.label = acc.status.username + (acc.status.premium ? " ★" : ""),
                         .detail = acc.from,
                         .value = std::to_string(i),
                         .color = {}});
    }
    push_overlay(std::make_unique<PickerOverlay>(
        *this, "link which account?", std::move(items),
        PickerOverlay::Callbacks{.on_pick =
                                     [this, accounts](const std::string& v) {
                                         apply_account((*accounts)[std::stoul(v)], false);
                                     },
                                 .on_done = {},
                                 .on_highlight = {},
                                 .on_cancel = {}}));
}

void App::apply_account(const core::Account& account, bool refreshed) {
    if (!leetcode::save_credentials(account.creds)) {
        toast("signed in, but could not save the session", Level::Warning);
    }
    client_ = leetcode::Client(account.creds);
    data_.user = account.status;
    toast(refreshed ? std::format("refreshed your LeetCode session from {}", account.from)
                    : std::format("linked {} from {}", account.status.username, account.from),
          Level::Success);
    refresh_statuses();
    fetch_stats();
    fetch_daily();
}

void App::login(const std::string& cookie) {
    const auto creds = leetcode::parse_cookie_string(cookie);
    if (!creds) {
        toast("that doesn't contain LEETCODE_SESSION and csrftoken", Level::Error);
        return;
    }
    toast("checking your session…");
    dispatcher_->run([this, creds = *creds, base = client_.base_url()](const std::stop_token&) {
        auto account = core::verify_account(creds, base, "a pasted cookie");
        dispatcher_->post([this, account = std::move(account)] {
            if (!account) {
                toast("login failed: " + account.error().message, Level::Error);
                return;
            }
            apply_account(*account, false);
        });
    });
}

void App::welcome() {
    // Offer to link an account once, on the first launch without one.
    const auto marker = paths::state_dir() / "welcomed";
    std::error_code ec;
    if (client_.has_credentials() || std::filesystem::exists(marker, ec)) return;
    [[maybe_unused]] const bool ok = paths::write_file(marker, "");
    push_overlay(std::make_unique<ConfirmOverlay>(
        *this, "welcome to leetvim",
        std::vector<std::string>{
            "Link your LeetCode account to run and submit solutions, track what you've solved "
            "and see your stats.",
            "leetvim uses the session of the browser you're already signed in with: nothing to "
            "copy or paste. (macOS may ask once to unlock the browser's cookie key; choose "
            "Always Allow so leetvim can renew the session by itself.)"},
        std::vector<ConfirmOverlay::Choice>{
            {.key = 'l', .label = "link from my browser", .action = [this] { link_account(); }},
            {.key = 'p',
             .label = "paste a cookie instead",
             .action = [this] { run_command("login paste"); }},
            {.key = 's', .label = "skip for now (:login any time)", .action = [] {}},
        }));
}

void App::logout() {
    leetcode::clear_credentials();
    client_ = leetcode::Client();
    data_.user.reset();
    data_.streak.reset();
    toast("signed out");
    fetch_stats();
}

void App::run_command(std::string_view line) {
    line = str::trim(line);
    const auto space = line.find(' ');
    const std::string cmd(line.substr(0, space));
    const std::string arg(space == std::string_view::npos ? "" : str::trim(line.substr(space + 1)));
    Session* active = current_ == solve_view_.get() ? session(active_session_) : nullptr;
    auto* home = home_.get();

    if (cmd.empty()) return;
    if (cmd == "q" || cmd == "quit" || cmd == "qa" || cmd == "exit") {
        request_quit();
    } else if (cmd == "q!" || cmd == "qa!") {
        quit_ = true;
    } else if (cmd == "daily" || cmd == "today") {
        open_daily();
    } else if (cmd == "random") {
        go_home();
        home->pick_random();
    } else if (cmd == "open" || cmd == "o" || cmd == "e" || cmd == "edit") {
        if (const auto* p = find_problem(arg)) {
            open_problem(p->slug);
        } else if (!arg.empty() && !arg.contains(' ')) {
            open_problem(arg);
        } else {
            toast(std::format("no problem '{}'", arg), Level::Error);
        }
    } else if (cmd.find_first_not_of("0123456789") == std::string::npos) {
        if (const auto* p = find_problem(cmd)) {
            open_problem(p->slug);
        } else {
            toast(std::format("no problem #{}", cmd), Level::Error);
        }
    } else if (cmd == "lang" || cmd == "language") {
        if (!arg.empty()) {
            set_language(arg, true);
            if (active != nullptr) switch_language(*active, arg);
            return;
        }
        std::vector<PickerOverlay::Item> items;
        for (const auto& lang : core::languages()) {
            items.push_back({.label = std::string(lang.name),
                             .detail = std::string(lang.slug),
                             .value = std::string(lang.slug),
                             .color = {}});
        }
        const std::string slug = active != nullptr ? active->slug : "";
        push_overlay(std::make_unique<PickerOverlay>(
            *this, "default language", std::move(items),
            PickerOverlay::Callbacks{.on_pick =
                                         [this, slug = slug](const std::string& v) {
                                             set_language(v, true);
                                             if (Session* s = session(slug)) switch_language(*s, v);
                                         },
                                     .on_done = {},
                                     .on_highlight = {},
                                     .on_cancel = {}},
            false, std::set<std::string>{}, config_.solution.lang));
    } else if (cmd == "theme" || cmd == "colorscheme" || cmd == "colo") {
        if (!arg.empty()) {
            set_theme(arg, true);
            return;
        }
        std::vector<PickerOverlay::Item> items;
        for (const auto& th : tui::builtin_themes()) {
            items.push_back({.label = std::string(th.name),
                             .detail = "",
                             .value = std::string(th.name),
                             .color = th.hi_fg});
        }
        const std::string original = config_.ui.theme;
        push_overlay(std::make_unique<PickerOverlay>(
            *this, "theme", std::move(items),
            PickerOverlay::Callbacks{
                .on_pick = [this](const std::string& v) { set_theme(v, true); },
                .on_done = {},
                .on_highlight = [this](const std::string& v) { set_theme(v, false); },
                .on_cancel = [this, original = original] { set_theme(original, false); }},
            false, std::set<std::string>{}, config_.ui.theme));
    } else if (cmd == "sync") {
        sync_problems(true);
    } else if (cmd == "login") {
        if (arg.empty()) {
            link_account();
            return;
        }
        if (arg != "paste") {
            login(arg);
            return;
        }
        push_overlay(std::make_unique<PromptOverlay>(
            *this, "login",
            std::vector<std::string>{
                "Paste your leetcode.com cookie. In the browser, open DevTools → Network, "
                "click any request to leetcode.com and copy the Cookie request header.",
                "It needs LEETCODE_SESSION and csrftoken. It is stored with 0600 permissions "
                "in "
                    + leetcode::credentials_path().string() + "."},
            [this](const std::string& v) { login(v); }, true));
    } else if (cmd == "logout") {
        logout();
    } else if (cmd == "stats") {
        show_stats();
    } else if (cmd == "home" || cmd == "list") {
        go_home();
    } else if (cmd == "set" || cmd == "se") {
        const auto eq = arg.find('=');
        const auto sep = eq != std::string::npos ? eq : arg.find(' ');
        if (arg.empty()) {
            push_overlay(std::make_unique<SettingsOverlay>(*this));
        } else if (sep != std::string::npos) {
            set_option(arg.substr(0, sep), arg.substr(sep + 1));
        } else if (const auto* option = core::find_option(arg)) {
            toast(std::format("{} = {}", option->key(), core::toml_value(config_, *option)));
        } else {
            toast(std::format("unknown option '{}'", arg), Level::Error);
        }
    } else if (cmd == "settings" || cmd == "options") {
        push_overlay(std::make_unique<SettingsOverlay>(*this, arg));
    } else if (cmd == "filter" && arg == "clear") {
        home->filter() = Filter{};
        home->refilter();
    } else if (cmd == "help" || cmd == "h") {
        push_overlay(std::make_unique<HelpOverlay>(*this));
    } else if ((cmd == "run" || cmd == "test") && active != nullptr) {
        start_job(*active, false);
    } else if (cmd == "submit" && active != nullptr) {
        start_job(*active, true);
    } else if (cmd == "config") {
        toast(config_path_.string());
    } else {
        toast(std::format("unknown command: {}", cmd), Level::Error);
    }
}

// ---------------------------------------------------------------- judge

void App::start_job(Session& s, bool submit) {
    if (s.job == JobState::Saving || s.job == JobState::Running) {
        toast("already running, hang on", Level::Warning);
        return;
    }
    if (!s.question) return;
    s.panel_open = true;
    s.panel_tab = 0;
    s.panel_scroll = 0;
    s.job_is_submit = submit;
    s.result.reset();
    s.detail.reset();
    s.error.clear();
    s.job_status.clear();
    if (!client_.has_credentials()) {
        s.job = JobState::Failed;
        s.error = "not signed in: run :login to run and submit code";
        return;
    }
    if (config_.editor.save_before_run && s.editor && s.editor->running()
        && core::is_vim_like(s.editor_argv)) {
        // Ask vim to write every buffer, then touch the sync file so we know it's done.
        std::error_code ec;
        fs::remove(s.sync_file, ec);
        s.editor->send_bytes(std::string(vim_normal)
                             + ":silent! wall | silent! call writefile([], $LEETVIM_SYNC) | "
                               "echo ''\r");
        s.job = JobState::Saving;
        s.save_deadline = Clock::now() + std::chrono::milliseconds(1500);
        return;
    }
    begin_job(s);
}

bool App::tick_sessions() {
    bool changed = false;
    for (auto& [slug, s] : sessions_) {
        if (s->job != JobState::Saving) continue;
        std::error_code ec;
        const bool saved = fs::exists(s->sync_file, ec);
        if (saved || Clock::now() > s->save_deadline) {
            if (!saved)
                toast("the editor didn't confirm the save; using the file on disk", Level::Warning);
            fs::remove(s->sync_file, ec);
            begin_job(*s);
            changed = true;
        }
    }
    return changed;
}

void App::begin_job(Session& s) {
    std::string content;
    if (!paths::read_file(s.file, content)) {
        s.job = JobState::Failed;
        s.error = "cannot read " + s.file.string();
        return;
    }
    const std::string code = core::extract_code(content);
    if (str::trim(code).empty()) {
        s.job = JobState::Failed;
        s.error = "the solution is empty";
        return;
    }
    s.job = JobState::Running;
    s.job_id = next_job_id_++;
    s.job_stop = std::stop_source{};
    s.cases_run = workspace_.testcases(*s.question);
    s.selected_case = 0;

    dispatcher_->run([this, slug = s.slug, id = s.job_id, stop = s.job_stop.get_token(),
                      client = client_, base = client_.base_url(), q = s.question,
                      lang = std::string(s.lang->slug), code = code,
                      input = join_lines(s.cases_run),
                      submit = s.job_is_submit](const std::stop_token&) mutable {
        const auto post_fail = [&](const leetcode::Error& e) {
            dispatcher_->post([this, slug = slug, id, msg = e.message, kind = e.kind] {
                Session* sess = session(slug);
                if (sess == nullptr || sess->job_id != id) return;
                sess->job = JobState::Failed;
                sess->error =
                    kind == leetcode::Error::Kind::Auth
                        ? "LeetCode refused the request (sign in again with :login): " + msg
                        : msg;
            });
        };
        const auto start = [&] {
            return submit ? client.submit(*q, lang, code) : client.run(*q, lang, code, input);
        };
        auto started = start();
        if (!started && started.error().kind == leetcode::Error::Kind::Auth) {
            // The session may simply have expired: renew it from the browser and retry once.
            if (auto account = core::refresh_account(client.credentials(), base)) {
                client = leetcode::Client(account->creds, base);
                dispatcher_->post(
                    [this, account = std::move(*account)] { apply_account(account, true); });
                started = start();
            }
        }
        if (!started) {
            post_fail(started.error());
            return;
        }
        auto result =
            client.wait(*started, slug, stop, [this, slug, id](const leetcode::RunResult& r) {
                dispatcher_->post([this, slug, id, state = r.state] {
                    if (Session* sess = session(slug); sess != nullptr && sess->job_id == id) {
                        sess->job_status = state;
                    }
                });
            });
        if (!result) {
            post_fail(result.error());
            return;
        }
        dispatcher_->post([this, slug, id, r = std::move(*result)] mutable {
            Session* sess = session(slug);
            if (sess == nullptr || sess->job_id != id) return;
            finish_job(*sess, std::move(r));
        });
    });
}

void App::finish_job(Session& s, leetcode::RunResult r) {
    s.job = JobState::Done;
    s.panel_scroll = 0;
    // Jump to the first failing case.
    for (std::size_t i = 0; i < r.compare_result.size(); ++i) {
        if (r.compare_result[i] == '0') {
            s.selected_case = static_cast<int>(i);
            break;
        }
    }
    const bool ok = r.accepted() && (r.is_submission || r.correct_answer);
    if (r.is_submission) {
        if (r.accepted()) {
            mark_status(s.slug, Status::Solved);
            if (data_.daily && data_.daily->problem.slug == s.slug) data_.daily->completed = true;
            toast(std::format("Accepted · {} · {}", r.runtime, r.memory), Level::Success);
            if (!r.submission_id.empty()) {
                dispatcher_->run([this, slug = s.slug, id = s.job_id, client = client_,
                                  sub = r.submission_id](const std::stop_token&) {
                    auto detail = client.submission_detail(sub);
                    if (!detail) return;
                    dispatcher_->post([this, slug, id, d = std::move(*detail)] mutable {
                        if (Session* sess = session(slug); sess != nullptr && sess->job_id == id) {
                            sess->detail = std::move(d);
                        }
                    });
                });
            }
            fetch_stats();
        } else {
            mark_status(s.slug, Status::Attempted);
            toast(r.status_msg, Level::Error);
        }
        s.submissions_loaded = false; // refresh on next view
    } else {
        const int passed = static_cast<int>(std::ranges::count(r.compare_result, '1'));
        toast(ok ? std::format("all {} cases passed", r.compare_result.size())
                 : std::format("{} · {}/{} passed", r.status_msg, passed, r.compare_result.size()),
              ok ? Level::Success : Level::Error);
    }
    s.result = std::move(r);
}

void App::edit_testcases(Session& s) {
    if (!s.question || s.test_editor) return;
    auto path = workspace_.ensure_testcases(*s.question);
    if (!path) {
        toast(path.error(), Level::Error);
        return;
    }
    auto argv = core::resolve_editor(config_.editor.command);
    if (argv.empty()) {
        toast("no editor found", Level::Error);
        return;
    }
    argv.push_back(path->string());
    s.panel_open = true;
    const auto* solve = solve_view_.get();
    const tui::Rect inner = solve->layout(screen(), s).panel.inset(1);
    s.test_editor = std::make_unique<tui::TermPane>();
    std::string error;
    if (!s.test_editor->spawn({.argv = argv,
                               .cwd = workspace_.root(),
                               .env = {{"LEETVIM", "1"}},
                               .rows = std::max(inner.h, 2),
                               .cols = std::max(inner.w, 10)},
                              error)) {
        s.test_editor.reset();
        toast(error, Level::Error);
    }
}

void App::reset_testcases(Session& s) {
    std::error_code ec;
    if (fs::remove(workspace_.testcases_path(s.slug), ec)) {
        toast("testcases reset to the examples");
    }
}

void App::load_submissions(Session& s, bool force) {
    if (!client_.has_credentials() || s.submissions_loading || (s.submissions_loaded && !force)) {
        return;
    }
    s.submissions_loading = true;
    dispatcher_->run([this, slug = s.slug, client = client_](const std::stop_token&) {
        auto list = client.submissions(slug, 40);
        dispatcher_->post([this, slug, list = std::move(list)] mutable {
            Session* sess = session(slug);
            if (sess == nullptr) return;
            sess->submissions_loading = false;
            sess->submissions_loaded = true;
            if (list) {
                sess->submissions = std::move(*list);
            } else {
                toast("submissions: " + list.error().message, Level::Error);
            }
        });
    });
}

void App::switch_language(Session& s, std::string_view slug) {
    const auto* lang = core::find_language(slug);
    if (lang == nullptr || !s.question) return;
    if (!s.question->snippets.empty() && s.question->snippet(lang->slug) == nullptr) {
        toast(std::format("{} isn't available for this problem", lang->name), Level::Error);
        return;
    }
    if (lang == s.lang) return;
    s.lang = lang;
    if (s.editor && s.editor->running() && core::is_vim_like(s.editor_argv)) {
        // Same vim, new buffer: keeps the user's session (registers, undo, splits).
        const auto before = config_.solution.inject_before.find(std::string(lang->slug));
        const auto after = config_.solution.inject_after.find(std::string(lang->slug));
        auto file = workspace_.ensure_solution(
            *s.question, *lang, client_.problem_url(s.slug),
            before != config_.solution.inject_before.end() ? before->second : "",
            after != config_.solution.inject_after.end() ? after->second : "");
        if (!file) {
            toast(file.error(), Level::Error);
            return;
        }
        s.file = *file;
        s.editor->send_bytes(std::string(vim_normal) + ":silent! wall | edit "
                             + vim_escape(s.file.string()) + "\r");
    } else {
        if (s.editor) s.editor->terminate();
        respawn_editor(s);
    }
    dirty_ = true;
}

void App::open_url(const std::string& url) {
#ifdef __APPLE__
    const pid_t pid = spawn_quiet({"open", url});
#else
    const pid_t pid = spawn_quiet({"xdg-open", url});
#endif
    if (pid > 0) {
        helper_pids().push_back(pid);
        toast("opened in the browser");
    } else {
        yank(url, "link (no browser opener found)");
    }
}

void App::yank(const std::string& text, std::string_view what) {
    tui::Terminal::copy_to_clipboard(text);
    // OSC 52 isn't universal; also feed the platform clipboard tool when present.
    for (const auto& tool : std::vector<std::vector<std::string>>{
             {"pbcopy"}, {"wl-copy"}, {"xclip", "-selection", "clipboard"}}) {
        if (core::find_executable(tool.front()).empty()) continue;
        std::array<int, 2> fds{};
        if (pipe(fds.data()) != 0) break;
        const pid_t pid = spawn_quiet(tool, fds[0]);
        close(fds[0]);
        if (pid > 0) {
            [[maybe_unused]] const ssize_t n = write(fds[1], text.data(), text.size());
            helper_pids().push_back(pid);
        }
        close(fds[1]);
        break;
    }
    toast(std::format("copied {}", what));
}

} // namespace lv::app
