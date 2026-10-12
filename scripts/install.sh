#!/bin/sh
# Installs a leetvim release:
#   curl -fsSL https://raw.githubusercontent.com/phongndo/leetvim/main/scripts/install.sh | sh
# Each release goes in its own directory under LEETVIM_DATA_DIR/releases, and the command is a
# symlink to it in LEETVIM_INSTALL_DIR. Every release ships this script; `leetvim update` reruns it.
set -eu

version=${LEETVIM_VERSION:-latest}
install_dir=${LEETVIM_INSTALL_DIR:-"$HOME/.local/bin"}
data_dir=${LEETVIM_DATA_DIR:-"$HOME/.local/share/leetvim"}

fail() {
  printf 'leetvim install: %s\n' "$*" >&2
  exit 1
}
for tool in curl tar mktemp; do
  command -v "$tool" >/dev/null 2>&1 || fail "missing required command: $tool"
done
if command -v sha256sum >/dev/null 2>&1; then
  checksum() { sha256sum "$1" | awk '{print $1}'; }
elif command -v shasum >/dev/null 2>&1; then
  checksum() { shasum -a 256 "$1" | awk '{print $1}'; }
else
  fail "sha256sum or shasum is required to verify the download"
fi

case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) target=aarch64-apple-darwin ;;
  Linux-x86_64 | Linux-amd64) target=x86_64-unknown-linux-musl ;;
  Linux-aarch64 | Linux-arm64) target=aarch64-unknown-linux-musl ;;
  *) fail "releases are built for Apple Silicon macOS and x86_64 or ARM64 Linux; elsewhere, build leetvim from source" ;;
esac
case "$install_dir:$data_dir" in
  /*:/*) ;;
  *) fail "LEETVIM_INSTALL_DIR and LEETVIM_DATA_DIR must be absolute paths" ;;
esac
case "$install_dir$data_dir" in
  *"
"*) fail "LEETVIM_INSTALL_DIR and LEETVIM_DATA_DIR must not contain newlines" ;;
esac

# Replace only a command this installer put there: Homebrew's, Nix's, mise's or your own stays.
destination="$install_dir/leetvim"
if [ -e "$destination" ] || [ -L "$destination" ]; then
  [ -L "$destination" ] || fail "$destination already exists; remove it or set LEETVIM_INSTALL_DIR"
  case "$(readlink "$destination")" in
    "$data_dir"/releases/*/bin/leetvim) ;;
    *) fail "$destination belongs to another installation; update that one with the tool that installed it, or set LEETVIM_INSTALL_DIR" ;;
  esac
fi

if [ "$version" = latest ]; then
  # GitHub redirects to the latest release's page, which ends in its tag.
  latest=$(curl -fsSLI -o /dev/null -w '%{url_effective}' https://github.com/phongndo/leetvim/releases/latest) ||
    fail "could not look up the latest release"
  case "$latest" in
    */releases/tag/*) version=${latest##*/} ;;
    *) fail "no release has been published yet" ;;
  esac
fi
case "$version" in
  v*) tag=$version ;;
  *) tag="v$version" ;;
esac
printf '%s\n' "$tag" | LC_ALL=C grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$' || fail "invalid release version: $version"

# `leetvim update` says which release is running.
if [ "${LEETVIM_CURRENT_RELEASE:-}" = "$tag" ] && [ -x "$destination" ]; then
  printf 'leetvim %s is already installed at %s\n' "$tag" "$destination"
  exit 0
fi

package="leetvim-$tag-$target"
asset="$package.tar.gz"
base_url=${LEETVIM_RELEASE_BASE_URL:-"https://github.com/phongndo/leetvim/releases/download/$tag"}
temporary=$(mktemp -d)
pending=""
cleanup() {
  rm -rf "$temporary"
  if [ -n "$pending" ]; then rm -rf "$pending"; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

printf 'Installing leetvim %s for %s\n' "$tag" "$target"
curl -fL --progress-bar --retry 3 "$base_url/$asset" -o "$temporary/$asset" || fail "could not download $asset"
curl -fsSL --retry 3 "$base_url/$asset.sha256" -o "$temporary/$asset.sha256" || fail "could not download the checksum"
expected=$(awk -v name="$asset" 'NF == 2 && $2 == name && length($1) == 64 && $1 !~ /[^0-9a-f]/ {print $1}' "$temporary/$asset.sha256")
if [ -z "$expected" ] || [ "$(checksum "$temporary/$asset")" != "$expected" ]; then
  fail "checksum verification failed"
fi
tar -xzf "$temporary/$asset" -C "$temporary"
[ -x "$temporary/$package/bin/leetvim" ] || fail "the archive has no bin/leetvim"

mkdir -p "$data_dir/releases" "$install_dir"
pending=$(mktemp -d "$data_dir/releases/$tag-$target.XXXXXX")
cp -R "$temporary/$package/." "$pending/"
# The receipt is how `leetvim update` knows this installer owns the installation.
mkdir -p "$pending/share/leetvim"
printf 'release=%s\ninstall_dir=%s\ndata_dir=%s\n' "$tag" "$install_dir" "$data_dir" \
  >"$pending/share/leetvim/installation"
"$pending/bin/leetvim" --version >/dev/null || fail "the downloaded leetvim does not run on this system"
# Renamed into place, so the command is never missing or half-written.
ln -s "$pending/bin/leetvim" "$install_dir/.leetvim.$$"
mv -f "$install_dir/.leetvim.$$" "$destination"
installed=$pending
pending=""

# A running leetvim keeps its executable open, so earlier releases can go now.
for release in "$data_dir"/releases/v*; do
  [ "$release" = "$installed" ] || rm -rf "$release"
done

printf '\nInstalled leetvim %s at %s\n' "$tag" "$destination"
case ":$PATH:" in
  *":$install_dir:"*) ;;
  *) printf 'Add %s to your PATH to run it.\n' "$install_dir" ;;
esac
if ! command -v nvim >/dev/null 2>&1 && ! command -v vim >/dev/null 2>&1; then
  printf 'leetvim solves problems in nvim or vim; install one of them too.\n'
fi
printf 'Get started with: leetvim login\nUpdate with: leetvim update\n'
