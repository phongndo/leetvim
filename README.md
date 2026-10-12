# leetvim

LeetCode in your terminal, solved in your own vim.

leetvim is a btop-style TUI for LeetCode: browse and filter the whole problem set, read
problems rendered as rich text, and solve them in a split where the right pane is your real
`nvim`/`vim` (your config, plugins and LSP) running inside leetvim. Nothing is installed into
your editor: leetvim embeds it through a pseudo-terminal.

- **Dashboard**: submission activity graph, solved meters per difficulty, streak, today's
  daily question and a preview of the selected problem.
- **Problem list**: all problems with vim motions, fuzzy search, filters (difficulty, status,
  premium, topics), sorting and random pick.
- **Solve view**: description (with hints, topics and similar questions) beside your editor;
  run the testcases or submit without leaving vim; per-case results, compile/runtime errors,
  runtime and memory percentiles with the runtime distribution, past submissions, and custom
  testcases edited in a second embedded editor.
- **Stats**: profile, contest rating, a GitHub-style heatmap, languages and skills.
- **Themes**: `default` (btop's palette), `tokyo-night`, `gruvbox`, `catppuccin`, `nord`, `light`.

## Install

From a checkout:

```sh
nix run .                  # try it
nix profile install .      # put `leetvim` on your PATH
```

leetvim needs `nvim`, `vim` or `vi` on `PATH` (or set `editor` in the config).

## Quick start

```sh
leetvim login    # link your account from the browser you're signed in with
leetvim daily    # straight into today's question
leetvim          # the dashboard
```

## Your account

LeetCode has no API tokens or OAuth for third-party apps; everything, including running and
submitting, works with the website's session cookies. So leetvim links your account by using
the session your browser already has:

1. Sign in at leetcode.com in your browser (as you normally would: password, Google, GitHub…).
2. Run `leetvim login`, or press `l` on the welcome screen the first time the TUI starts
   (`:login` later).

leetvim finds `LEETCODE_SESSION` and `csrftoken` in Chrome, Helium, Arc, Brave, Edge, Vivaldi,
Opera, Chromium, Firefox, Zen, LibreWolf, Safari or Orion, checks with LeetCode which sessions
are signed in, and links the account (asking which one if several are). Chromium-based browsers
encrypt cookies with a key in the macOS Keychain, so macOS asks once to unlock it; choose
**Always Allow**. Safari's cookies need Full Disk Access for your terminal.

Sessions expire every couple of weeks. Because leetvim remembers which browser profile the
session came from, it renews it from there automatically when LeetCode rejects it (at
startup, and before a run or submit); as long as you stay signed in in the browser you never
log in to leetvim again.

Other ways in: `leetvim login --browser helium` limits the search to one browser,
`leetvim login --paste` (or `:login paste`) takes a pasted `Cookie` header from DevTools,
and `LEETCODE_SESSION` / `LEETCODE_CSRF_TOKEN` environment variables override everything.
The session is stored in `$XDG_DATA_HOME/leetvim/credentials` with mode 0600;
`leetvim logout` deletes it.

### What leetvim keeps track of

LeetCode is the source of truth for your progress; leetvim reads it rather than keeping a
second copy:

| Data | Where it lives | When it updates |
| --- | --- | --- |
| Solved / attempted per problem | LeetCode; cached in `$XDG_CACHE_HOME/leetvim/problems.json` | every launch (one request), after each submit |
| Submissions, runtime/memory percentiles | LeetCode | the results panel's submissions tab |
| Stats, streak, calendar, contest rating, skills | LeetCode | launch, after accepted submissions, `R` |
| Daily question and whether you did it | LeetCode | launch, after accepted submissions |
| Your solution code | `<workspace>/<id>.<slug>.<ext>` (default `~/leetcode`) | you edit it; leetvim never overwrites it |
| Custom testcases | `<workspace>/.leetvim/testcases/<slug>.txt` | the testcases tab |
| Session | `$XDG_DATA_HOME/leetvim/credentials` | login, automatic renewal |
| Settings | `$XDG_CONFIG_HOME/leetvim/config.toml` | `leetvim config --edit`, theme/language pickers |

Problems you solve on the website show up the next time leetvim starts. The workspace is a
plain directory of solution files, so it works well as a git repository.

## Keys

Press `?` anywhere for the full list.

**Problem list**

| Key | Action |
| --- | --- |
| `j` `k` `gg` `G` `C-d` `C-u` | move |
| `Enter` / `l` | solve the selected problem |
| `/` | fuzzy search by title or id |
| `f` / `t` | filters / topics |
| `o` / `O` | cycle sort / reverse |
| `d` / `r` | daily question / random pick |
| `J` `K` | scroll the preview |
| `1` `2` | toggle the stats / preview boxes |
| `s` | stats dashboard |
| `y` / `gx` | copy the link / open in the browser |
| `L` / `T` | default language / theme |
| `R` | re-sync problems and stats |

**Solve view**: while the editor has focus every key goes to vim, so leetvim's own keys
follow a prefix (`ctrl-g` by default, like tmux's leader):

| Key | Action |
| --- | --- |
| `C-g t` | run the testcases on LeetCode |
| `C-g s` | submit |
| `C-g h` / `C-g j` / `C-g l` | focus description / results / editor |
| `C-g r` | toggle the results panel |
| `C-g c` | testcases |
| `C-g z` | zoom the editor |
| `C-g q` | back to the list (the editor keeps running) |
| `C-g C-g` | send `C-g` to vim |

In the description: `j` `k` `C-d` `C-u` `gg` `G` scroll, `H` reveals hints, `<` `>` resize the
split, `L` switches the solution language (in the same vim, via `:edit`). In the results
panel: `h` `l` or `1`–`9` pick a testcase, `Tab` cycles result / testcases / submissions,
`e` edits custom testcases, `x` resets them to the examples.

Before running or submitting, leetvim asks vim to save (`:wall`), so you don't have to.

**Commands** (`:`): `:daily`, `:random`, `:open <id|slug|title>` (or just `:42`), `:lang <slug>`,
`:theme <name>`, `:set <key>=<value>`, `:login`, `:logout`, `:sync`, `:stats`, `:run`,
`:submit`, `:q`.

## Command line

```text
leetvim                      the TUI
leetvim daily [--print]      open (or print) today's question
leetvim open <problem>       open a problem by id, slug or title
leetvim show <problem> [-m]  print the description (-m: Markdown)
leetvim list [-d hard] [-s todo] [-t dynamic-programming] [-q query] [-n 20]
leetvim test <file>          run a solution file's testcases (exit 1 on failure)
leetvim submit <file>        submit a solution file
leetvim stats [username]     print profile statistics
leetvim login [-b browser] [-p]  link your account (see "Your account")
leetvim logout | sync
leetvim config [--init|--edit]
```

`test` and `submit` work from any editor, e.g. `:!leetvim test %` in vim.

## Solutions and testcases

Solution files live in the workspace (`~/leetcode` by default) as `<id>.<slug>.<ext>`:

```python
# @leetvim id=1 slug=two-sum lang=python3
# 1. Two Sum (Easy)
# https://leetcode.com/problems/two-sum/

# @lc code=start
class Solution:
    def twoSum(self, nums: list[int], target: int) -> list[int]:
        ...
# @lc code=end
```

Only the code between the markers is sent to LeetCode (leetcode.nvim's `@leet start` /
`@leet end` markers work too), so helpers or includes for your LSP can live outside them; the
`inject_before` / `inject_after` config tables add such text to new files. Custom testcases are
saved in `<workspace>/.leetvim/testcases/<slug>.txt`, one input line per parameter.

## Configuration

`leetvim config --init` writes a commented `$XDG_CONFIG_HOME/leetvim/config.toml` with every
option: `lang`, `editor`, `workspace`, `prefix`, `save_before_run`, `theme`,
`theme_background`, `rounded_corners`, `truecolor`, `mouse`, `split`, `show_paid`, `username`,
`refresh_hours`, `[inject_before]` and `[inject_after]`. Picking a theme or default language
in the TUI updates this file in place.

The problem list and question details are cached in `$XDG_CACHE_HOME/leetvim` and refreshed
every `refresh_hours` (24 by default) or on `R` / `leetvim sync`.

Set `LEETVIM_LOG=/path/to/file` to log HTTP requests and notifications.

## Development

The flake provides the toolchain: the latest clang from nixpkgs (with clangd, clang-tidy and
clang-format), CMake, Ninja, treefmt, nixfmt and gersemi.

```sh
nix develop
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLEETVIM_SANITIZE=ON
cmake --build build && ctest --test-dir build
LEETVIM_LIVE_TESTS=1 ./build/test_leetcode   # also hit the real public API
nix fmt                                      # clang-format, nixfmt, gersemi
nix flake check                              # build + tests, clang-tidy, format check
```

`src/` is split into layered libraries, each with tests under `tests/<module>/`:

| Module | Role |
| --- | --- |
| `util` | UTF-8 and display width, strings, fuzzy matching, XDG paths, logging |
| `content` | LeetCode HTML → document model → terminal lines or Markdown |
| `leetcode` | API client (GraphQL + judge endpoints over libcurl), credentials, cache |
| `tui` | raw terminal, input parsing, diffing renderer, btop-style widgets, themes, and the pty + libvterm pane that hosts the editor |
| `core` | config, languages, workspace files, editor resolution |
| `app` | the event loop, views, overlays and the command line |

Only leetcode.com is supported (not leetcode.cn).
