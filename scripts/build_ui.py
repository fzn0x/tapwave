"""Builds the web UI filesystem contents by hand: python scripts/build_ui.py [dest]

PlatformIO runs the same step before every device build; this script is for inspecting the
output or packing the UI outside PlatformIO.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tapwave_build  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("dest", nargs="?", default=str(ROOT / ".pio" / "ui-data" / "www"))
    parser.add_argument("--version", default=None, help="version string to embed")
    parser.add_argument("--budget-kib", type=float, default=96.0,
                        help="fail if the gzipped UI is larger (default 96, fits the 4 MB layout)")
    args = parser.parse_args()

    version = args.version or tapwave_build.firmware_version(ROOT)
    files = tapwave_build.build_ui(ROOT / "ui", args.dest, version)
    total = sum(f.stored_bytes for f in files)
    for f in files:
        print(f"{f.stored_bytes:8d}  {f.path}")
    print(f"{total:8d}  total ({total / 1024:.1f} KiB, version {version})")
    if total > args.budget_kib * 1024:
        print(f"error: the UI is over its {args.budget_kib} KiB budget", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
