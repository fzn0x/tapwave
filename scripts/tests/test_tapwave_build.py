import gzip
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import tapwave_build  # noqa: E402


class VersionFromDescribeTest(unittest.TestCase):
    def test_build_on_a_tag_uses_the_tag(self):
        self.assertEqual(tapwave_build.version_from_describe("v0.1.0\n"), "0.1.0")

    def test_commits_after_a_tag_sort_above_it(self):
        self.assertEqual(
            tapwave_build.version_from_describe("v0.1.0-5-gabc1234"), "0.1.1-dev.5+gabc1234"
        )

    def test_dirty_tree_is_recorded_as_build_metadata(self):
        self.assertEqual(
            tapwave_build.version_from_describe("v0.1.0-5-gabc1234-dirty"),
            "0.1.1-dev.5+gabc1234.dirty",
        )
        self.assertEqual(tapwave_build.version_from_describe("v1.2.3-dirty"), "1.2.3+dirty")

    def test_prerelease_tags_keep_their_label(self):
        self.assertEqual(tapwave_build.version_from_describe("v0.2.0-rc.1"), "0.2.0-rc.1")
        self.assertEqual(
            tapwave_build.version_from_describe("v0.2.0-rc.1-3-gdeadbee"),
            "0.2.0-rc.1.dev.3+gdeadbee",
        )

    def test_repository_without_tags(self):
        self.assertEqual(tapwave_build.version_from_describe("abc1234"), "0.0.0-dev+gabc1234")
        self.assertEqual(
            tapwave_build.version_from_describe("abc1234-dirty"), "0.0.0-dev+gabc1234.dirty"
        )

    def test_garbage_is_rejected(self):
        with self.assertRaises(ValueError):
            tapwave_build.version_from_describe("not a version")


class FirmwareVersionTest(unittest.TestCase):
    def test_environment_override_wins_and_drops_the_v(self):
        with mock.patch.dict(os.environ, {"TAPWAVE_VERSION": "v1.4.0"}):
            self.assertEqual(tapwave_build.firmware_version("."), "1.4.0")

    def test_falls_back_when_git_is_unavailable(self):
        with tempfile.TemporaryDirectory() as empty, mock.patch.dict(
            os.environ, {"TAPWAVE_VERSION": ""}
        ):
            self.assertEqual(tapwave_build.firmware_version(empty), "0.0.0-dev")


class BuildUiTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.src = Path(self.tmp.name) / "ui"
        self.dest = Path(self.tmp.name) / "out"
        (self.src / "js").mkdir(parents=True)
        (self.src / "tests").mkdir()
        (self.src / "index.html").write_text("<p>v__TAPWAVE_VERSION__</p>")
        (self.src / "js" / "app.js").write_text("export const v = '__TAPWAVE_VERSION__';")
        (self.src / "js" / "format.test.js").write_text("test")
        (self.src / "tests" / "helper.js").write_text("test")
        (self.src / "icon.png").write_bytes(b"\x89PNG")

    def test_text_assets_are_gzipped_with_the_version_inlined(self):
        tapwave_build.build_ui(self.src, self.dest, "1.2.3")
        html = gzip.decompress((self.dest / "index.html.gz").read_bytes()).decode()
        self.assertEqual(html, "<p>v1.2.3</p>")
        js = gzip.decompress((self.dest / "js" / "app.js.gz").read_bytes()).decode()
        self.assertIn("'1.2.3'", js)
        self.assertFalse((self.dest / "index.html").exists())

    def test_binary_assets_are_copied_unchanged(self):
        tapwave_build.build_ui(self.src, self.dest, "1.2.3")
        self.assertEqual((self.dest / "icon.png").read_bytes(), b"\x89PNG")

    def test_tests_are_left_out(self):
        written = tapwave_build.build_ui(self.src, self.dest, "1.2.3")
        paths = {f.path for f in written}
        self.assertEqual(paths, {"index.html", "js/app.js", "icon.png"})

    def test_output_is_reproducible(self):
        tapwave_build.build_ui(self.src, self.dest, "1.2.3")
        first = (self.dest / "js" / "app.js.gz").read_bytes()
        tapwave_build.build_ui(self.src, self.dest, "1.2.3")
        self.assertEqual((self.dest / "js" / "app.js.gz").read_bytes(), first)

    def test_stale_files_are_removed(self):
        self.dest.mkdir()
        (self.dest / "old.js.gz").write_bytes(b"stale")
        tapwave_build.build_ui(self.src, self.dest, "1.2.3")
        self.assertFalse((self.dest / "old.js.gz").exists())


if __name__ == "__main__":
    unittest.main()
