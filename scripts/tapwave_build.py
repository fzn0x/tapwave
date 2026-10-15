"""Build helpers shared by the PlatformIO pre-script, the release workflow and the tests.

Only the Python standard library is used, so the helpers run anywhere PlatformIO runs.
"""

from __future__ import annotations

import gzip
import os
import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

# Files in ui/ that are sources for tests or documentation, not part of the device image.
UI_EXCLUDE = re.compile(r"(^|/)(tests?/|README\.md$|package\.json$)|\.test\.m?js$")

# Text assets are stored gzipped; the HTTP server sends them with Content-Encoding: gzip.
UI_GZIP_SUFFIXES = {".html", ".css", ".js", ".mjs", ".svg", ".json", ".webmanifest", ".txt"}

# Text assets where the placeholder is replaced with the firmware version.
UI_VERSION_PLACEHOLDER = "__TAPWAVE_VERSION__"
UI_VERSIONED_SUFFIXES = {".html", ".js", ".mjs"}

_DESCRIBE = re.compile(
    r"^v?(?P<tag>\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+?)??)"
    r"(?:-(?P<distance>\d+)-g(?P<sha>[0-9a-f]+))?"
    r"(?P<dirty>-dirty)?$"
)
_BARE_SHA = re.compile(r"^(?P<sha>[0-9a-f]{4,40})(?P<dirty>-dirty)?$")


def version_from_describe(describe: str) -> str:
    """Turns `git describe --tags --always --dirty` output into a semantic version.

    A build on a tag gets the tag's version. A build after a tag sorts above it: the patch
    number goes up and the commit distance becomes a pre-release (`0.1.1-dev.5+gabc1234`).
    A repository without tags builds `0.0.0-dev+g<sha>`.
    """
    describe = describe.strip()
    match = _DESCRIBE.match(describe)
    if match:
        tag = match["tag"]
        build = []
        if match["sha"]:
            build.append("g" + match["sha"])
        if match["dirty"]:
            build.append("dirty")
        if match["distance"]:
            core, _, pre = tag.partition("-")
            if pre:
                version = f"{tag}.dev.{match['distance']}"
            else:
                major, minor, patch = (int(part) for part in core.split("."))
                version = f"{major}.{minor}.{patch + 1}-dev.{match['distance']}"
        else:
            version = tag
        return version + ("+" + ".".join(build) if build else "")

    match = _BARE_SHA.match(describe)
    if match:
        build = ["g" + match["sha"]] + (["dirty"] if match["dirty"] else [])
        return "0.0.0-dev+" + ".".join(build)

    raise ValueError(f"unrecognised git describe output: {describe!r}")


def firmware_version(project_dir: str | os.PathLike[str]) -> str:
    """The version to bake into the firmware: $TAPWAVE_VERSION, else git, else a fallback."""
    override = os.environ.get("TAPWAVE_VERSION", "").strip()
    if override:
        return override.removeprefix("v")
    try:
        describe = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty", "--match", "v[0-9]*"],
            cwd=project_dir,
            capture_output=True,
            text=True,
            check=True,
        ).stdout
        return version_from_describe(describe)
    except (OSError, subprocess.CalledProcessError, ValueError):
        return "0.0.0-dev"


@dataclass(frozen=True)
class UiFile:
    path: str
    source_bytes: int
    stored_bytes: int


def build_ui(src: str | os.PathLike[str], dest: str | os.PathLike[str], version: str) -> list[UiFile]:
    """Copies the web UI from `src` into `dest`, gzipping text assets.

    `dest` is emptied first. Gzip output uses a fixed timestamp so builds are reproducible.
    Returns what was written, for size reports.
    """
    src_path, dest_path = Path(src), Path(dest)
    if dest_path.exists():
        shutil.rmtree(dest_path)
    dest_path.mkdir(parents=True)

    written: list[UiFile] = []
    for file in sorted(p for p in src_path.rglob("*") if p.is_file()):
        relative = file.relative_to(src_path).as_posix()
        if UI_EXCLUDE.search(relative):
            continue
        data = file.read_bytes()
        if file.suffix in UI_VERSIONED_SUFFIXES:
            data = data.replace(UI_VERSION_PLACEHOLDER.encode(), version.encode())
        target = dest_path / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if file.suffix in UI_GZIP_SUFFIXES:
            stored = gzip.compress(data, compresslevel=9, mtime=0)
            target = target.with_name(target.name + ".gz")
        else:
            stored = data
        target.write_bytes(stored)
        written.append(UiFile(relative, len(data), len(stored)))
    return written
