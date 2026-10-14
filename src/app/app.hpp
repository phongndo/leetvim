#pragma once

#include "app/dispatcher.hpp"
#include "content/layout.hpp"
#include "core/account.hpp"
#include "core/config.hpp"
#include "core/languages.hpp"
#include "core/workspace.hpp"
#include "leetcode/cache.hpp"
#include "leetcode/client.hpp"
#include "leetcode/models.hpp"
#include "tui/input.hpp"
#include "tui/painter.hpp"
#include "tui/renderer.hpp"
#include "tui/term_pane.hpp"
#include "tui/terminal.hpp"
#include "tui/theme.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lv::app {

class HomeView;
class SolveView;
class StatsView;

using Clock = std::chrono::steady_clock;

enum class Level : std::uint8_t { Info, Success, Warning, Error };

struct Toast {
    std::string text;
    Level level = Level::Info;
    Clock::time_point expires;
};

/// Something drawn full-screen (home, solve, stats) or as a modal overlay.
class View {
public:
    View() = default;
    virtual ~View() = default;
    View(const View&) = delete;
    View& operator=(const View&) = delete;
    View(View&&) = delete;
    View& operator=(View&&) = delete;

    virtual void draw(const tui::Painter& p, tui::Rect area) = 0;
    /// Returns true if the key was consumed.
    virtual bool on_key(const tui::KeyEvent& ev) = 0;
    virtual bool on_mouse(const tui::MouseEvent& /*ev*/) { return false; }
    virtual bool on_paste(const std::string& /*text*/) { return false; }
    [[nodiscard]] virtual std::optional<tui::Cursor> cursor() const { return std::nullopt; }
    /// Called every loop iteration; return true to request a redraw.
    virtual bool on_tick() { return false; }
};

/// A modal view on top of the current screen. Close it by setting closed_.
class Overlay : public View {
public:
    [[nodiscard]] bool closed() const { return closed_; }

protected:
    bool closed_ = false;
};

/// The [keys] settings, parsed.
struct Bindings {
    tui::KeySpec prefix;
    tui::KeySpec run;
    tui::KeySpec submit;
    tui::KeySpec results;
    tui::KeySpec testcases;
    tui::KeySpec zoom;
    tui::KeySpec back;
    tui::KeySpec help;

    static Bindings from(const core::Config::Keys& keys);
};

/// Run/submit lifecycle of a session.
enum class JobState : std::uint8_t { Idle, Saving, Running, Done, Failed };

/// Everything about one opened problem: its question, solution file, editor process
/// and judge results. Sessions outlive the solve view so editors keep running.
struct Session {
    std::string slug;
    std::shared_ptr<const leetcode::Question> question;
    const core::Language* lang = nullptr;
    std::filesystem::path file;
    std::filesystem::path sync_file; // touched by vim after it saved, see App::save_editor
    std::vector<std::string> editor_argv;
    std::unique_ptr<tui::TermPane> editor;
    Clock::time_point last_used;
    std::unique_ptr<tui::TermPane> test_editor; // editing the custom testcases

    // Judge state.
    JobState job = JobState::Idle;
    bool job_is_submit = false;
    Clock::time_point save_deadline;
    std::string job_status; // "PENDING", "STARTED", ...
    std::uint64_t job_id = 0;
    std::stop_source job_stop;
    std::optional<leetcode::RunResult> result;
    std::string error;
    std::vector<std::string> cases_run;
    std::optional<leetcode::SubmissionDetail> detail;
    std::vector<leetcode::SubmissionSummary> submissions;
    bool submissions_loading = false;
    bool submissions_loaded = false;

    // View state.
    int desc_scroll = 0;
    int panel_scroll = 0;
    int selected_case = 0;
    int panel_tab = 0; // 0 result, 1 testcases, 2 submissions
    bool panel_open = false;
    bool show_hints = false;
    bool zoomed = false;
};

/// Rendered description of a question (problem HTML plus topics, hints and similar
/// questions), laid out for one width.
struct Description {
    std::vector<content::Line> lines;
    int width = -1;
    bool hints = false;
};

struct Data {
    std::vector<leetcode::ProblemSummary> problems;
    std::unordered_map<std::string, std::size_t> by_slug;
    std::uint64_t problems_version = 0; // bumps whenever the list changes
    bool syncing = false;
    int sync_done = 0;
    int sync_total = 0;
    std::chrono::system_clock::time_point synced_at;
    std::string sync_error;

    std::optional<leetcode::DailyChallenge> daily;
    std::optional<leetcode::UserStatus> user;
    std::optional<leetcode::UserStats> stats;
    bool stats_loading = false;
    std::string stats_error;
    std::optional<leetcode::StreakInfo> streak;

    std::unordered_map<std::string, std::shared_ptr<const leetcode::Question>> questions;
    std::unordered_set<std::string> loading_questions;
    std::unordered_map<std::string, std::string> question_errors;
};

class App {
public:
    struct Options {
        std::string open; // problem to open at start: slug, id or "daily"
    };

    App(core::Config config, std::filesystem::path config_path, std::vector<std::string> warnings);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    int run(const Options& options);

    // --- state for views ---
    [[nodiscard]] const core::Config& config() const { return config_; }
    [[nodiscard]] const std::filesystem::path& config_path() const { return config_path_; }
    [[nodiscard]] const tui::Theme& theme() const { return *theme_; }
    /// Screen background: the theme's when theme_background is on, else the terminal's.
    [[nodiscard]] tui::Color base_bg() const {
        return config_.ui.theme_background ? theme_->main_bg : tui::Color::none();
    }
    [[nodiscard]] Data& data() { return data_; }
    [[nodiscard]] const leetcode::Client& client() const { return client_; }
    [[nodiscard]] const core::Workspace& workspace() const { return workspace_; }
    [[nodiscard]] Dispatcher& dispatcher() { return *dispatcher_; }
    [[nodiscard]] int tick() const { return tick_; }
    [[nodiscard]] const Bindings& keys() const { return keys_; }
    [[nodiscard]] bool signed_in() const { return data_.user && data_.user->signed_in; }
    [[nodiscard]] std::string username() const;
    [[nodiscard]] Session* session(const std::string& slug);
    [[nodiscard]] std::size_t live_editors() const;

    /// The question, starting a background fetch if needed (nullptr until loaded).
    std::shared_ptr<const leetcode::Question> question(const std::string& slug);
    /// Description lines for a question at `width` (cached).
    const std::vector<content::Line>& description(const leetcode::Question& q, int width,
                                                  bool hints);
    [[nodiscard]] const leetcode::ProblemSummary* problem(const std::string& slug) const;
    /// Finds a problem by slug, frontend id or exact title.
    [[nodiscard]] const leetcode::ProblemSummary* find_problem(std::string_view query) const;

    // --- actions ---
    void open_problem(const std::string& slug);
    void open_daily();
    void go_home();
    void show_stats();
    void push_overlay(std::unique_ptr<Overlay> overlay);
    void toast(std::string text, Level level = Level::Info);
    void request_quit();
    void quit_now() { quit_ = true; }
    void redraw() { dirty_ = true; }

    void sync_problems(bool force);
    void refresh_user();
    /// Links the account whose session a browser has (asking if there are several).
    void link_account();
    /// Links pasted cookie text.
    void login(const std::string& cookie);
    void logout();
    void set_theme(std::string_view name, bool persist);
    void set_language(std::string_view slug, bool persist);
    /// Validates and applies an option ("ui.theme" or a unique bare name), saving it to the
    /// config file unless `persist` is false. Problems are shown as toasts.
    void set_option(std::string_view key, std::string_view value, bool persist = true);
    void run_command(std::string_view line);

    // Solve actions on a session.
    void start_job(Session& s, bool submit);
    void edit_testcases(Session& s);
    void reset_testcases(Session& s);
    void load_submissions(Session& s, bool force = false);
    void switch_language(Session& s, std::string_view slug);
    void respawn_editor(Session& s);
    void open_url(const std::string& url);
    void yank(const std::string& text, std::string_view what);

    /// The whole screen; used to size pseudo-terminals before their first frame.
    [[nodiscard]] tui::Rect screen() const { return {0, 0, size_.width, size_.height}; }
    /// The session shown by the solve view, if any.
    [[nodiscard]] Session* active_session() { return session(active_session_); }

private:
    void loop();
    void handle_signal(int sig);
    void handle_event(const tui::Event& ev);
    void handle_key(const tui::KeyEvent& ev);
    void render();
    void pump_terminals();
    bool tick_sessions();
    int poll_timeout() const;
    void apply_theme();
    void load_cached_problems();
    void set_problems(std::vector<leetcode::ProblemSummary> problems);
    void mark_status(const std::string& slug, leetcode::Status status);
    void fetch_stats();
    void fetch_daily();
    void setup_session(Session& s);
    void finish_link(core::Discovery found);
    void apply_account(const core::Account& account, bool refreshed);
    void refresh_statuses();
    void welcome();
    void retire_idle_editors();
    bool spawn_editor(Session& s, std::string& error);
    /// The solution file for `q` in `lang`, created if new, with the workspace's support
    /// files for `lang` (warning when they can't be written).
    std::expected<std::filesystem::path, std::string> ensure_solution(const leetcode::Question& q,
                                                                      const core::Language& lang);
    void begin_job(Session& s);
    void finish_job(Session& s, leetcode::RunResult r);
    void finish_quit();
    void apply_option(const core::Option& option);
    void persist(const core::Option& option);
    [[nodiscard]] bool truecolor() const;

    core::Config config_;
    std::filesystem::path config_path_;
    std::vector<std::string> warnings_;
    const tui::Theme* theme_ = nullptr;
    Bindings keys_;
    leetcode::Client client_;
    leetcode::Cache cache_;
    core::Workspace workspace_;
    Data data_;
    std::unordered_map<std::string, Description> descriptions_;

    tui::Terminal terminal_;
    tui::Renderer renderer_;
    tui::InputParser input_;
    tui::TermSize size_;

    std::unique_ptr<HomeView> home_;
    std::unique_ptr<StatsView> stats_view_;
    std::unique_ptr<SolveView> solve_view_;
    View* current_ = nullptr;
    std::vector<std::unique_ptr<Overlay>> overlays_;
    std::map<std::string, std::unique_ptr<Session>> sessions_;
    std::string active_session_;
    std::string pending_open_; // "daily" or a problem id to open once data arrives

    std::vector<Toast> toasts_;
    int tick_ = 0;
    bool dirty_ = true;
    bool quit_ = false;
    bool quitting_ = false; // waiting for editors to save and exit
    Clock::time_point quit_deadline_;
    Clock::time_point last_tick_;
    std::int64_t last_clock_second_ = 0;
    std::uint64_t next_job_id_ = 1;

    // Declared last so it is destroyed first: workers may post into the members above.
    std::unique_ptr<Dispatcher> dispatcher_;
};

} // namespace lv::app
