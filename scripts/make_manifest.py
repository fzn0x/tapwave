"""Writes the release manifest the boxes update from (ARCHITECTURE.md section 14).

    python scripts/make_manifest.py --tag v0.2.0 --repo fzn0x/tapwave --dist dist \\
        --out site/ota/manifest.json

`dist` holds the release files, named tapwave-<environment>-firmware.bin and
tapwave-<environment>-littlefs.bin. Each image is listed with its download URL on the GitHub
release, its size and its SHA-256, which the box checks while it writes the image.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

FILE = re.compile(r"^tapwave-(?P<env>[a-z0-9-]+?)-(?P<kind>firmware|littlefs)\.bin$")


def image(path: Path, url: str) -> dict:
    data = path.read_bytes()
    return {"url": url, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def manifest(tag: str, repo: str, dist: Path) -> dict:
    """The manifest for every environment that has a firmware image in `dist`."""
    version = tag.removeprefix("v")
    base = f"https://github.com/{repo}/releases/download/{tag}"
    builds: dict[str, dict] = {}
    for path in sorted(dist.iterdir()):
        match = FILE.match(path.name)
        if not match:
            continue
        key = "firmware" if match["kind"] == "firmware" else "filesystem"
        builds.setdefault(match["env"], {})[key] = image(path, f"{base}/{path.name}")
    missing = [env for env, build in builds.items() if "firmware" not in build]
    if missing:
        raise ValueError(f"no firmware image for {', '.join(missing)}")
    if not builds:
        raise ValueError(f"no release images in {dist}")
    return {
        "version": version,
        "release_url": f"https://github.com/{repo}/releases/tag/{tag}",
        "builds": builds,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tag", required=True, help="the release tag, for example v0.2.0")
    parser.add_argument("--repo", required=True, help="owner/name on GitHub")
    parser.add_argument("--dist", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args(argv)
    result = manifest(args.tag, args.repo, args.dist)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"{args.out}: {result['version']} for {', '.join(result['builds'])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
