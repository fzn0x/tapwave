import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import size_report  # noqa: E402


class SizeReportTest(unittest.TestCase):
    def test_every_environment_has_a_partition_table(self):
        for env, slot in (("esp32-4mb", 0x1E0000), ("esp32-8mb", 0x330000),
                          ("esp32-16mb", 0x400000), ("esp32s3", 0x400000)):
            sizes = size_report.partitions(size_report.partition_file(env))
            self.assertEqual(sizes["app0"], slot, env)
            self.assertEqual(sizes["app1"], slot, env)
            self.assertIn("spiffs", sizes)

    def test_unknown_environment(self):
        with self.assertRaises(KeyError):
            size_report.partition_file("esp8266")

    def test_report_shows_usage_and_missing_images(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp)
            (build / "firmware.bin").write_bytes(b"\0" * (0x1E0000 // 2))
            text = size_report.report("esp32-4mb", build, build / "no-ui")
        self.assertIn("| Firmware | 960 KB | 1920 KB | 50.0 % |", text)
        self.assertIn("| Web UI files | not built | 128 KB | |", text)

    def test_web_ui_files_are_summed(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / "data"
            (data / "www" / "lib").mkdir(parents=True)
            (data / "www" / "index.html.gz").write_bytes(b"x" * 16384)
            (data / "www" / "lib" / "a.js.gz").write_bytes(b"x" * 16384)
            text = size_report.report("esp32-4mb", Path(tmp), data)
        self.assertIn("| Web UI files | 32 KB | 128 KB | 25.0 % |", text)


if __name__ == "__main__":
    unittest.main()
