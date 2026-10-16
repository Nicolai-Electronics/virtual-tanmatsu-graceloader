#!/usr/bin/env python3
"""Install an app into this emulator's virtual card the way the launcher does.

    tools/stage-app.py <app-repo> [--int DIR] [--sd DIR]

WHY. An app finds its own assets by appending to graceloader_get_install_basepath(),
which the real graceloader sets to the directory it loaded app.so from -- a VFS
directory like /sd/apps/at.cavac.synthminer, with textures/ and music/ beside it.
Running build/app.so straight out of the app's checkout instead reports that
checkout's build directory, which holds a binary and nothing else, so every
asset path the app builds misses and it comes up with flat colours and no
music. Staging is this emulator's version of the app's own `make install`.

WHAT LANDS WHERE. metadata.json's application[].assets list is the device
install's manifest, so it is read off each app repo instead of being hardcoded
here. Its target_file half is authoritative; its source_file half is written by
the apps' own generators and is not always the path in the checkout (SynthMiner's
music is assets/music/*.mid here and music/*.mid on a card), so each target is
looked for by name under the checkout instead.

The last line printed is the guest path of the staged app.so. That is what to
hand the emulator, and it is what graceloader_get_install_basepath() will then
report -- the same string the app sees on a device.
"""

import json
import os
import re
import shutil
import sys
from pathlib import Path

# The launcher's own files in an install directory. Not in the asset list,
# because the launcher reads them rather than the app, but they are part of what
# an install puts there.
LAUNCHER_FILES = ("metadata.json", "icon16.png", "icon32.png", "icon64.png")

# A staging directory is ours to replace. Without this marker next door, an
# existing directory is left alone.
MARKER = ".staged-by-virtual-graceloader"


def makefile_var(repo: Path, name: str) -> str | None:
    """Read `NAME ?= value` out of the app's own Makefile."""
    try:
        text = (repo / "Makefile").read_text()
    except OSError:
        return None
    match = re.search(rf"^{name}\s*\??=\s*(\S+)", text, re.MULTILINE)
    return match.group(1) if match else None


def app_entry(repo: Path) -> dict:
    """The metadata.json entry that describes the Tanmatsu build."""
    meta = json.loads((repo / "metadata" / "metadata.json").read_text())
    entries = meta.get("application") or []
    if not entries:
        raise SystemExit(f"{repo}/metadata/metadata.json lists no application")
    for entry in entries:
        if "tanmatsu" in entry.get("targets", ["tanmatsu"]):
            return entry
    return entries[0]


def file_index(repo: Path) -> dict[str, list[str]]:
    """Every file in the checkout, by file name, as checkout-relative paths."""
    index: dict[str, list[str]] = {}
    for dirpath, dirnames, filenames in os.walk(repo):
        dirnames[:] = [d for d in dirnames if d not in (".git", "__pycache__")]
        for name in filenames:
            rel = os.path.relpath(os.path.join(dirpath, name), repo)
            index.setdefault(name, []).append(rel)
    return index


def find_source(target: str, index: dict[str, list[str]]) -> str | None:
    """Where in the checkout the file that belongs at `target` actually is.

    Prefers the same path, then the same path tail (assets/music/x.mid for a
    target of music/x.mid), then the same file name. app.so resolves to the
    built one under build/ by the same rule that resolves any other asset.
    """
    same_name = index.get(os.path.basename(target), [])
    tails = [p for p in same_name if p == target or p.endswith("/" + target)]
    if tails:
        return min(tails, key=lambda p: (p.count("/"), len(p)))
    if same_name:
        return min(same_name, key=lambda p: (p.count("/"), len(p)))
    return None


def main() -> int:
    args = sys.argv[1:]
    if not args:
        print(__doc__, file=sys.stderr)
        return 2

    repo = Path(args[0]).resolve()
    int_dir, sd_dir = "int", "sd"
    for flag, value in zip(args[1::2], args[2::2]):
        if flag == "--int":
            int_dir = value
        elif flag == "--sd":
            sd_dir = value
    if not (repo / "metadata" / "metadata.json").is_file():
        raise SystemExit(f"{repo} has no metadata/metadata.json to stage from")

    entry = app_entry(repo)
    slug = makefile_var(repo, "APP_SLUG_NAME") or repo.name
    install_base = makefile_var(repo, "APP_INSTALL_BASE_PATH") or (
        "/sd/apps/" if entry.get("external_only") else "/int/apps/"
    )
    guest_dir = install_base.rstrip("/") + "/" + slug
    if guest_dir.startswith("/int"):
        host_dir = Path(int_dir) / guest_dir[len("/int"):].lstrip("/")
    elif guest_dir.startswith("/sd"):
        host_dir = Path(sd_dir) / guest_dir[len("/sd"):].lstrip("/")
    else:
        raise SystemExit(f"{guest_dir}: not under /int or /sd, so this emulator cannot host it")

    if host_dir.is_dir() and not (host_dir / MARKER).exists():
        raise SystemExit(f"{host_dir} already exists and was not staged by this tool; not touching it")
    if host_dir.exists():
        shutil.rmtree(host_dir)
    host_dir.mkdir(parents=True)
    (host_dir / MARKER).write_text("staged by tools/stage-app.py -- safe to delete\n")

    index = file_index(repo)
    copied, missing = 0, []
    for asset in entry.get("assets", []) + [{"target_file": f} for f in LAUNCHER_FILES]:
        target = asset["target_file"]
        source = find_source(target, index)
        if source is None:
            missing.append(target)
            continue
        dest = host_dir / target
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(repo / source, dest)
        copied += 1

    print(f"staged {repo.name} -> {guest_dir} ({copied} files)")
    for target in missing:
        print(f"  no {target} in {repo}, not staged")
    print(guest_dir + "/" + entry.get("executable", "app.so"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
