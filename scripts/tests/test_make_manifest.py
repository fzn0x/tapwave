import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import make_manifest  # noqa: E402


class ManifestTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dist = Path(self.tmp.name)

    def write(self, name: str, data: bytes) -> None:
        (self.dist / name).write_bytes(data)

    def test_lists_every_environment_with_urls_sizes_and_hashes(self):
        self.write("tapwave-esp32-4mb-firmware.bin", b"fw4")
        self.write("tapwave-esp32-4mb-littlefs.bin", b"fs4")
        self.write("tapwave-esp32s3-firmware.bin", b"fws3")
        self.write("tapwave-esp32-4mb-factory.bin", b"not an update image")
        self.write("SHA256SUMS", b"ignored")
        result = make_manifest.manifest("v0.2.0", "fzn0x/tapwave", self.dist)
        self.assertEqual(result["version"], "0.2.0")
        self.assertEqual(result["release_url"], "https://github.com/fzn0x/tapwave/releases/tag/v0.2.0")
        self.assertEqual(set(result["builds"]), {"esp32-4mb", "esp32s3"})
        firmware = result["builds"]["esp32-4mb"]["firmware"]
        self.assertEqual(firmware, {
            "url": "https://github.com/fzn0x/tapwave/releases/download/v0.2.0/tapwave-esp32-4mb-firmware.bin",
            "size": 3,
            "sha256": hashlib.sha256(b"fw4").hexdigest(),
        })
        self.assertIn("filesystem", result["builds"]["esp32-4mb"])
        self.assertNotIn("filesystem", result["builds"]["esp32s3"])

    def test_matches_what_the_firmware_parser_requires(self):
        # src/core/ota_manifest.cpp: an http(s) URL, a size above zero, 64 lowercase hex digits.
        self.write("tapwave-esp32-16mb-firmware.bin", b"x" * 10)
        image = make_manifest.manifest("v1.0.0", "o/r", self.dist)["builds"]["esp32-16mb"]["firmware"]
        self.assertTrue(image["url"].startswith("https://"))
        self.assertGreater(image["size"], 0)
        self.assertRegex(image["sha256"], r"^[0-9a-f]{64}$")

    def test_prerelease_tags_keep_their_label(self):
        self.write("tapwave-esp32-4mb-firmware.bin", b"fw")
        self.assertEqual(make_manifest.manifest("v0.2.0-rc.1", "o/r", self.dist)["version"], "0.2.0-rc.1")

    def test_a_filesystem_without_firmware_is_an_error(self):
        self.write("tapwave-esp32-8mb-littlefs.bin", b"fs")
        with self.assertRaisesRegex(ValueError, "esp32-8mb"):
            make_manifest.manifest("v0.2.0", "o/r", self.dist)

    def test_an_empty_folder_is_an_error(self):
        with self.assertRaises(ValueError):
            make_manifest.manifest("v0.2.0", "o/r", self.dist)

    def test_writes_the_file(self):
        self.write("tapwave-esp32-4mb-firmware.bin", b"fw")
        out = self.dist / "site" / "ota" / "manifest.json"
        make_manifest.main(["--tag", "v0.2.0", "--repo", "o/r", "--dist", str(self.dist), "--out", str(out)])
        self.assertEqual(json.loads(out.read_text())["version"], "0.2.0")


if __name__ == "__main__":
    unittest.main()
