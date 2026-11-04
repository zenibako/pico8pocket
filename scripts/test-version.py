#!/usr/bin/env python3
"""Print the core.json version for a test build of the next release.

Test builds report <next>-rc.<N>, where N counts the commits since the newest
release tag (vX.Y.Z) reachable from HEAD. That sorts above the last release and
below the next one, and every machine gets the same N for the same commit.

<next> is NEXT_VERSION when set, else the committed core.json version when a
release prep commit already bumped it, else the last release with its patch
number bumped.
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STABLE = re.compile(r"v?(\d+)\.(\d+)\.(\d+)")


def git(*args):
    return subprocess.run(["git", "-C", str(ROOT), *args], check=True,
                          capture_output=True, text=True).stdout.strip()


def parse(version, what):
    match = STABLE.fullmatch(version)
    if not match:
        raise SystemExit(f"{what} is not MAJOR.MINOR.PATCH: {version!r}")
    return tuple(int(part) for part in match.groups())


def main():
    committed = json.loads((ROOT / "dist/core/core.json").read_text())["core"]["metadata"]["version"]
    tags = [tag for tag in git("tag", "--merged", "HEAD", "--list", "v*").split() if STABLE.fullmatch(tag)]
    if not tags:
        raise SystemExit("no vX.Y.Z release tag reachable from HEAD; run `git fetch --tags` (`git fetch --unshallow --tags` in a shallow clone)")
    tag = max(tags, key=lambda name: parse(name, "tag"))
    last = parse(tag, "tag")
    if os.environ.get("NEXT_VERSION"):
        next_version = parse(os.environ["NEXT_VERSION"], "NEXT_VERSION")
        if next_version <= last:
            raise SystemExit(f"NEXT_VERSION must be newer than {tag}")
    elif parse(committed, "dist/core/core.json version") > last:
        next_version = parse(committed, "dist/core/core.json version")
    else:
        next_version = (last[0], last[1], last[2] + 1)
    count = int(git("rev-list", "--count", f"{tag}..HEAD"))
    version = "%d.%d.%d-rc.%d" % (*next_version, count)
    if len(version) > 31:
        raise SystemExit(f"version is longer than the 31 characters core.json allows: {version}")
    print(version)


if __name__ == "__main__":
    sys.exit(main())
