"""Prints how much of its OTA slot and filesystem a build uses, as a Markdown table.

    python scripts/size_report.py esp32-4mb

CI appends the output to the job summary, so a pull request that eats into the 4 MB boards'
headroom is visible before it is merged.
"""

from __future__ import annotations

import argparse
import configparser
import csv
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def partition_file(env: str, ini: Path = ROOT / "platformio.ini") -> Path:
    """The partition table a PlatformIO environment uses, following `extends`."""
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(ini, encoding="utf-8")
    section = f"env:{env}"
    seen = set()
    while section and section not in seen:
        seen.add(section)
        if not parser.has_section(section):
            break
        if parser.has_option(section, "board_build.partitions"):
            return ini.parent / parser.get(section, "board_build.partitions").strip()
        section = parser.get(section, "extends", fallback="").strip()
    raise KeyError(f"no partition table for {env}")


def partitions(csv_path: Path) -> dict[str, int]:
    """Partition name to size in bytes."""
    sizes = {}
    with open(csv_path, encoding="utf-8") as handle:
        for row in csv.reader(line for line in handle if line.strip() and not line.startswith("#")):
            fields = [f.strip() for f in row]
            sizes[fields[0]] = int(fields[4], 0)
    return sizes


def tree_size(path: Path) -> int:
    return sum(f.stat().st_size for f in path.rglob("*") if f.is_file())


def report(env: str, build_dir: Path, data_dir: Path) -> str:
    """The firmware against its OTA slot, and the packed web UI files against the filesystem
    partition (the filesystem image itself is always padded to the partition size)."""
    sizes = partitions(partition_file(env))
    firmware = build_dir / "firmware.bin"
    rows = [
        ("Firmware", firmware.stat().st_size if firmware.exists() else None, sizes["app0"]),
        ("Web UI files", tree_size(data_dir) if data_dir.exists() else None, sizes["spiffs"]),
    ]
    lines = [f"### {env}", "", "| Image | Size | Space | Used |", "| --- | ---: | ---: | ---: |"]
    for name, size, space in rows:
        if size is None:
            lines.append(f"| {name} | not built | {space // 1024} KB | |")
            continue
        lines.append(f"| {name} | {size / 1024:.0f} KB | {space // 1024} KB | {size * 100 / space:.1f} % |")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("env")
    parser.add_argument("--build-dir", default=None)
    parser.add_argument("--data-dir", default=str(ROOT / ".pio" / "ui-data"))
    args = parser.parse_args()
    build_dir = Path(args.build_dir) if args.build_dir else ROOT / ".pio" / "build" / args.env
    sys.stdout.write(report(args.env, build_dir, Path(args.data_dir)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
