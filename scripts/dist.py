#!/usr/bin/env python3
"""Release tooling: the tag to release, the Homebrew formula for a release's archives, and an
end-to-end check of scripts/install.sh and `leetvim update` against a built archive."""

from __future__ import annotations

import argparse
import hashlib
import os
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = "phongndo/leetvim"
TARGETS = {
    ("Darwin", "arm64"): "aarch64-apple-darwin",
    ("Linux", "aarch64"): "aarch64-unknown-linux-musl",
    ("Linux", "x86_64"): "x86_64-unknown-linux-musl",
}


def release_tag() -> str:
    """v + the version in CMakeLists.txt's project(), which every build reports."""
    match = re.search(
        r"project\(leetvim VERSION ([0-9]+\.[0-9]+\.[0-9]+)\b",
        (ROOT / "CMakeLists.txt").read_text(),
    )
    if match is None:
        raise SystemExit("CMakeLists.txt has no project(leetvim VERSION x.y.z)")
    return f"v{match[1]}"


def checksum(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_checksum(path: Path) -> str:
    match = re.fullmatch(r"([0-9a-f]{64})  (\S+)\n?", path.read_text())
    if match is None or match[2] != path.name.removesuffix(".sha256"):
        raise SystemExit(f"not a checksum file for its archive: {path}")
    return match[1]


def write_checksum(archive: Path) -> None:
    Path(f"{archive}.sha256").write_text(f"{checksum(archive)}  {archive.name}\n")


def formula(checksums: Path, base_url: str) -> str:
    tag = release_tag()

    def source(target: str, indent: str) -> list[str]:
        asset = f"leetvim-{tag}-{target}.tar.gz"
        digest = read_checksum(checksums / f"{asset}.sha256")
        return [f'{indent}url "{base_url}/{asset}"', f'{indent}sha256 "{digest}"']

    return "\n".join(
        [
            "class Leetvim < Formula",
            '  desc "LeetCode in your terminal, solved in your own vim"',
            f'  homepage "https://github.com/{REPOSITORY}"',
            f'  version "{tag[1:]}"',
            '  license "MIT"',
            "",
            "  on_macos do",
            "    depends_on arch: :arm64",
            "    depends_on macos: :sonoma",
            "",
            "    on_arm do",
            *source("aarch64-apple-darwin", "      "),
            "    end",
            "  end",
            "",
            "  on_linux do",
            "    on_arm do",
            *source("aarch64-unknown-linux-musl", "      "),
            "    end",
            "",
            "    on_intel do",
            *source("x86_64-unknown-linux-musl", "      "),
            "    end",
            "  end",
            "",
            "  def install",
            '    bin.install "bin/leetvim"',
            '    doc.install Dir["share/doc/leetvim/*"]',
            "  end",
            "",
            "  test do",
            '    assert_match "leetvim #{version}", shell_output("#{bin}/leetvim --version")',
            '    assert_match "leetvim/config.toml", shell_output("#{bin}/leetvim config")',
            "  end",
            "end",
            "",
        ]
    )


def extract(archive: Path, directory: Path) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    subprocess.run(["tar", "-xzf", str(archive), "-C", str(directory)], check=True)
    return directory / archive.name.removesuffix(".tar.gz")


def retag(archive: Path, tag: str, output: Path) -> Path:
    """The same archive published as another release, to exercise updating to it."""
    new = archive.name.removesuffix(".tar.gz").replace(f"-{release_tag()}-", f"-{tag}-")
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        extracted = extract(archive, Path(temporary))
        retagged = output / f"{new}.tar.gz"
        with tarfile.open(retagged, "w:gz") as tar:
            tar.add(extracted, arcname=new)
    write_checksum(retagged)
    return retagged


def check(archive: Path, online: bool) -> None:
    """Installs the archive as a user would, then updates, reinstalls and fails to install."""
    tag = release_tag()
    if read_checksum(Path(f"{archive}.sha256")) != checksum(archive):
        raise SystemExit(f"{archive.name} does not match its checksum")
    with tempfile.TemporaryDirectory(prefix="leetvim-install-") as temporary:
        root = Path(temporary)
        home = root / "home"
        home.mkdir()
        environment = {
            "PATH": os.environ["PATH"],
            "HOME": str(home),
            "TERM": "dumb",
            "LEETVIM_VERSION": tag,
            # Spaces, to catch unquoted paths in the installer.
            "LEETVIM_INSTALL_DIR": str(root / "command dir"),
            "LEETVIM_DATA_DIR": str(root / "data dir"),
            "LEETVIM_RELEASE_BASE_URL": archive.parent.as_uri(),
        }
        command = root / "command dir/leetvim"
        releases = root / "data dir/releases"

        def run(*arguments: str, ok: bool = True, **overrides: str) -> str:
            result = subprocess.run(
                arguments,
                env=environment | overrides,
                capture_output=True,
                text=True,
                timeout=120,
                check=False,
            )
            if (result.returncode == 0) != ok:
                raise SystemExit(
                    f"{' '.join(arguments)} exited {result.returncode}:\n"
                    f"{result.stdout}{result.stderr}"
                )
            return result.stdout + result.stderr

        def installed() -> Path:
            """The release the command runs, after checking it is the only one kept."""
            target = command.readlink()
            if not run(str(command), "--version").startswith("leetvim "):
                raise SystemExit(f"{command} does not run")
            if [path.name for path in releases.iterdir()] != [target.parents[1].name]:
                raise SystemExit(f"expected only the installed release in {releases}")
            return target

        installer = str(ROOT / "scripts/install.sh")
        run("sh", installer)
        first = installed()
        if run(str(command), "--version").strip() != f"leetvim {tag[1:]}":
            raise SystemExit(f"{command} is not leetvim {tag}")
        if "already installed" not in run(str(command), "update", tag):
            raise SystemExit("leetvim update reinstalled the running release")
        run("sh", installer)  # a reinstall replaces the release directory
        if installed() == first:
            raise SystemExit("reinstalling kept the old release directory")

        # `leetvim update` reruns the installer shipped in the release, which drops the old one.
        newer = retag(archive, "v99.0.0", root / "newer")
        run(str(command), "update", "99.0.0", LEETVIM_RELEASE_BASE_URL=newer.parent.as_uri())
        if not installed().parents[1].name.startswith("v99.0.0-"):
            raise SystemExit("leetvim update did not install the requested release")

        # A download that fails its checksum leaves the installation alone.
        before = command.readlink()
        broken = root / "broken"
        broken.mkdir()
        shutil.copy2(archive, broken / archive.name)
        Path(broken / f"{archive.name}.sha256").write_text(f"{'0' * 64}  {archive.name}\n")
        output = run("sh", installer, ok=False, LEETVIM_RELEASE_BASE_URL=broken.as_uri())
        if "checksum verification failed" not in output or command.readlink() != before:
            raise SystemExit("a corrupt download changed the installation")

        # The installer leaves commands it did not install alone, and `leetvim update` only
        # manages its own installations.
        foreign = root / "foreign"
        foreign.mkdir()
        (foreign / "leetvim").write_text("#!/bin/sh\n")
        run("sh", installer, ok=False, LEETVIM_INSTALL_DIR=str(foreign))
        extracted = extract(archive, root / "extracted") / "bin/leetvim"
        if "brew upgrade leetvim" not in run(str(extracted), "update", ok=False):
            raise SystemExit("leetvim update offered to manage an installation it did not make")

        if online:
            # The static build finds the system's CA certificates.
            if "Two Sum" not in run(str(command), "show", "two-sum"):
                raise SystemExit("leetvim show could not fetch a problem")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("version", help="print the tag to release")
    formula_parser = commands.add_parser("formula", help="write the Homebrew formula")
    formula_parser.add_argument("--checksums", type=Path, required=True)
    formula_parser.add_argument(
        "--base-url",
        default=f"https://github.com/{REPOSITORY}/releases/download/{release_tag()}",
    )
    formula_parser.add_argument("--output", type=Path, required=True)
    check_parser = commands.add_parser("check", help="exercise install.sh with an archive")
    check_parser.add_argument("archives", type=Path, help="directory of built archives")
    check_parser.add_argument("--online", action="store_true", help="also fetch a problem")
    args = parser.parse_args()

    if args.command == "version":
        print(release_tag())
    elif args.command == "formula":
        args.output.write_text(formula(args.checksums, args.base_url.rstrip("/")))
    elif args.command == "check":
        target = TARGETS[(platform.system(), platform.machine())]
        archive = args.archives.resolve() / f"leetvim-{release_tag()}-{target}.tar.gz"
        check(archive, args.online)
        print(f"{archive.name}: install, update and refusal checks passed")


if __name__ == "__main__":
    main()
