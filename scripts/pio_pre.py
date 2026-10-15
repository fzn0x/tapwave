"""PlatformIO pre-script for the device environments.

Bakes the firmware version and environment name into the build (TAPWAVE_VERSION and
TAPWAVE_ENV) and regenerates the web UI filesystem contents from ui/, so
`pio run -t buildfs` always packs the current UI.
"""

import os
import sys

Import("env")  # noqa: F821  (provided by PlatformIO)

project_dir = env["PROJECT_DIR"]  # noqa: F821
sys.path.insert(0, os.path.join(project_dir, "scripts"))

import tapwave_build  # noqa: E402

version = tapwave_build.firmware_version(project_dir)
env.Append(  # noqa: F821
    CPPDEFINES=[
        ("TAPWAVE_VERSION", env.StringifyMacro(version)),  # noqa: F821
        ("TAPWAVE_ENV", env.StringifyMacro(env["PIOENV"])),  # noqa: F821
    ]
)

ui_dir = os.path.join(project_dir, "ui")
www_dir = os.path.join(env.subst("$PROJECT_DATA_DIR"), "www")  # noqa: F821
if os.path.isdir(ui_dir):
    files = tapwave_build.build_ui(ui_dir, www_dir, version)
    stored = sum(f.stored_bytes for f in files)
    print(f"Tapwave {version}: web UI {len(files)} files, {stored / 1024:.1f} KiB stored")
else:
    print(f"Tapwave {version}: no ui/ directory, filesystem image will be empty")
