# Installation

Releases are built for macOS 14 or newer on Apple Silicon, and for x86_64 and ARM64 Linux. The
Linux executable is static, so it runs on any distribution, NixOS and Alpine included. leetvim
solves problems in your own `nvim` or `vim` (or `vi`), which you install separately.

| Method | Install | Update |
| --- | --- | --- |
| [Homebrew](#homebrew) | `brew install phongndo/tap/leetvim` | `brew upgrade leetvim` |
| [Install script](#install-script) | `curl -fsSL …/install.sh \| sh` | `leetvim update` |
| [mise](#mise) | `mise use -g github:phongndo/leetvim` | `mise upgrade github:phongndo/leetvim` |
| [Nix](#nix) | `nix profile add github:phongndo/leetvim` | `nix profile upgrade leetvim` |

Each method manages its own copy, so update leetvim with the one that installed it. Your
settings, session and solutions live elsewhere (see
[What leetvim keeps track of](../README.md#what-leetvim-keeps-track-of)) and stay when you
reinstall or switch methods.

## Homebrew

```sh
brew install phongndo/tap/leetvim
```

The tap picks up a new release within six hours of its publication.

## Install script

```sh
curl -fsSL https://raw.githubusercontent.com/phongndo/leetvim/main/scripts/install.sh | sh
```

The script needs `curl`, `tar`, and `sha256sum` or `shasum`. It downloads the release for your
system, checks its SHA-256 checksum, unpacks it under `~/.local/share/leetvim/releases`, and
links `~/.local/bin/leetvim` to it. If your shell doesn't find `leetvim` afterwards, add that
directory to your `PATH`:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Update to the latest release, or to a particular one:

```sh
leetvim update
leetvim update 0.1.0
```

`leetvim update` reruns the install script that came with your release, in the same
directories, and does nothing if that release is already installed. A failed download or
checksum leaves the installed leetvim as it was. `leetvim update` refuses to touch a leetvim
that Homebrew, mise or Nix installed, and the script won't replace a `leetvim` command that it
didn't put there.

Environment variables choose a release or other directories:

| Variable | Default | Meaning |
| --- | --- | --- |
| `LEETVIM_VERSION` | the latest release | the release to install, such as `0.1.0` |
| `LEETVIM_INSTALL_DIR` | `~/.local/bin` | where the `leetvim` command goes |
| `LEETVIM_DATA_DIR` | `~/.local/share/leetvim` | releases are unpacked in its `releases/` |
| `LEETVIM_RELEASE_BASE_URL` | the GitHub release | a mirror holding the release's archives and `.sha256` files |

```sh
curl -fsSL https://raw.githubusercontent.com/phongndo/leetvim/main/scripts/install.sh |
  LEETVIM_VERSION=0.1.0 sh
```

`leetvim update` remembers the directories. Both must be absolute paths.

## mise

[mise](https://mise.jdx.dev) installs the release archive from GitHub:

```sh
mise use -g github:phongndo/leetvim
```

Pin a version with `github:phongndo/leetvim@0.1.0`, globally or in a project's `mise.toml`.

## Nix

Nix builds leetvim from source with the dependencies pinned in `flake.lock`, running the test
suite on the way, so the first install takes a few minutes. Try it without installing:

```sh
nix run github:phongndo/leetvim
```

Install it into your profile (Lix and Nix before 2.30 spell `add` as `install`):

```sh
nix profile add github:phongndo/leetvim
```

Or add the flake to a NixOS, nix-darwin or Home Manager configuration:

```nix
{
  inputs.leetvim.url = "github:phongndo/leetvim";

  # In a NixOS or nix-darwin module:
  environment.systemPackages = [ inputs.leetvim.packages.${pkgs.stdenv.hostPlatform.system}.default ];
  # Or in Home Manager:
  home.packages = [ inputs.leetvim.packages.${pkgs.stdenv.hostPlatform.system}.default ];
}
```

and update it with `nix flake update leetvim` and a rebuild.

## From source

The Nix build is the tested one: in a checkout, `nix build` puts leetvim in `./result`.
Elsewhere you need a C++23 compiler and standard library, CMake 3.28+, Ninja, pkg-config, and
the development packages for libcurl, nlohmann_json 3.11+, CLI11, SQLite 3, OpenSSL (libcrypto)
and libvterm, plus the toml++ headers from a commit newer than 3.4.0, whose parser has bugs on
malformed files (`nix/tomlplusplus.nix` has the one leetvim is tested with); doctest too unless
you pass `-DLEETVIM_BUILD_TESTS=OFF`.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/leetvim
```

## Removal

- Homebrew: `brew uninstall leetvim`
- Install script: `rm ~/.local/bin/leetvim` and `rm -r ~/.local/share/leetvim/releases`, or the
  directories you chose
- mise: `mise unuse -g github:phongndo/leetvim`
- Nix: `nix profile remove leetvim`, or remove the flake input and package

To remove your data too, delete `~/.config/leetvim`, `~/.cache/leetvim`, `~/.local/state/leetvim`
and `~/.local/share/leetvim` (or their `$XDG_*` equivalents); your solutions stay in the
workspace.
