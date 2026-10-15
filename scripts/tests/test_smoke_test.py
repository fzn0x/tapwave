import argparse
import socket
import struct
import sys
import threading
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import mock_device  # noqa: E402
import smoke_test  # noqa: E402


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def args(host: str, **overrides) -> argparse.Namespace:
    values = {"host": host, "password": "", "tap": 0, "mqtt": "", "mqtt_user": "",
              "mqtt_password": "", "timeout": 5}
    values.update(overrides)
    return argparse.Namespace(**values)


class MqttCodecTest(unittest.TestCase):
    def test_remaining_length_matches_the_spec_examples(self):
        self.assertEqual(smoke_test.mqtt_length(0), b"\x00")
        self.assertEqual(smoke_test.mqtt_length(127), b"\x7f")
        self.assertEqual(smoke_test.mqtt_length(128), b"\x80\x01")
        self.assertEqual(smoke_test.mqtt_length(16383), b"\xff\x7f")
        self.assertEqual(smoke_test.mqtt_length(16384), b"\x80\x80\x01")

    def test_connect_with_credentials(self):
        packet = smoke_test.mqtt_connect("c", "u", "p")
        self.assertEqual(packet, bytes([0x10, 19]) + b"\x00\x04MQTT\x04\xc2\x00\x78"
                         + b"\x00\x01c\x00\x01u\x00\x01p")

    def test_subscribe(self):
        packet = smoke_test.mqtt_subscribe(1, ["a/#"])
        self.assertEqual(packet, bytes([0x82, 8]) + b"\x00\x01\x00\x03a/#\x00")

    def test_publish_is_split_and_parsed(self):
        body = struct.pack(">H", 5) + b"x/tag" + b"ON"
        data = bytes([0x31]) + smoke_test.mqtt_length(len(body)) + body + b"\x30"
        first, packet, used = smoke_test.mqtt_packet(data)
        self.assertEqual(used, len(data) - 1)
        self.assertEqual(smoke_test.mqtt_publish(first, packet), ("x/tag", b"ON", True))

    def test_qos1_publish_skips_the_packet_id(self):
        body = struct.pack(">H", 1) + b"t" + b"\x00\x07" + b"hi"
        self.assertEqual(smoke_test.mqtt_publish(0x32, body), ("t", b"hi", False))

    def test_incomplete_packets_wait_for_more(self):
        self.assertIsNone(smoke_test.mqtt_packet(b"\x30"))
        self.assertIsNone(smoke_test.mqtt_packet(b"\x30\x05ab"))


class FakeBroker:
    """Accepts one client, answers CONNECT and SUBSCRIBE, then sends retained messages."""

    def __init__(self, messages: list[tuple[str, bytes]], refuse: int = 0):
        self.server = socket.socket()
        self.server.bind(("127.0.0.1", 0))
        self.server.listen(1)
        self.port = self.server.getsockname()[1]
        self.messages = messages
        self.refuse = refuse
        threading.Thread(target=self.serve, daemon=True).start()

    def close(self):
        self.server.close()
        if getattr(self, "conn", None):
            self.conn.close()

    def serve(self):
        conn, _ = self.server.accept()
        self.conn = conn
        buffer = b""
        while smoke_test.mqtt_packet(buffer) is None:
            buffer += conn.recv(1024)
        conn.sendall(bytes([0x20, 2, 0, self.refuse]))
        if self.refuse:
            return
        buffer = buffer[smoke_test.mqtt_packet(buffer)[2]:]
        while smoke_test.mqtt_packet(buffer) is None:
            buffer += conn.recv(1024)
        conn.sendall(bytes([0x90, 3, 0, 1, 0]))
        for topic, payload in self.messages:
            body = struct.pack(">H", len(topic)) + topic.encode() + payload
            conn.sendall(bytes([0x31]) + smoke_test.mqtt_length(len(body)) + body)


class MqttWatcherTest(unittest.TestCase):
    def test_collects_retained_messages(self):
        broker = FakeBroker([("tapwave/a1b2c3/status", b"online")])
        self.addCleanup(broker.close)
        watcher = smoke_test.MqttWatcher("127.0.0.1", broker.port, "u", "p", ["tapwave/a1b2c3/#"])
        self.addCleanup(watcher.close)
        self.assertEqual(watcher.wait_for("tapwave/a1b2c3/status", 0, 5), [b"online"])

    def test_explains_a_refused_login(self):
        broker = FakeBroker([], refuse=4)
        self.addCleanup(broker.close)
        with self.assertRaisesRegex(ConnectionError, "bad user name or password"):
            smoke_test.MqttWatcher("127.0.0.1", broker.port, "u", "wrong", ["#"])


class WebSocketFrameTest(unittest.TestCase):
    def test_reads_the_frames_the_box_sends(self):
        for size in (5, 300, 70000):
            frame = mock_device.ws_frame("x" * size)
            opcode, payload, used = smoke_test.read_frame(frame + b"\x81")
            self.assertEqual((opcode, len(payload), used), (1, size, len(frame)))

    def test_incomplete_frames_wait_for_more(self):
        frame = mock_device.ws_frame("x" * 300)
        self.assertIsNone(smoke_test.read_frame(frame[:3]))
        self.assertIsNone(smoke_test.read_frame(frame[:-1]))


class UidFormatTest(unittest.TestCase):
    def test_tagreader_format_only(self):
        self.assertTrue(smoke_test.UID.match("04-A2-1B-3C-4D-5E-80"))
        self.assertTrue(smoke_test.UID.match("DE-AD-BE-EF"))
        self.assertFalse(smoke_test.UID.match("04A21B3C"))
        self.assertFalse(smoke_test.UID.match("de-ad-be-ef"))


class AgainstTheMockBoxTest(unittest.TestCase):
    def serve(self, **options):
        port = free_port()
        server = mock_device.serve(port, "ready", **options)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        return f"127.0.0.1:{port}"

    def test_a_working_box_passes_with_a_tap(self):
        host = self.serve(tap_every=1.5)
        report = smoke_test.run(args(host, tap=6))
        self.assertEqual([r.name for r in report.results if not r.ok], [])
        self.assertIn("lifting the card is reported", [r.name for r in report.results])

    def test_a_password_protected_box(self):
        host = self.serve(tap_every=0, admin_password="letmein")
        without = smoke_test.run(args(host))
        self.assertEqual([r.name for r in without.results if not r.ok], ["an admin password was given"])
        report = smoke_test.run(args(host, password="letmein"))
        self.assertEqual([r.name for r in report.results if not r.ok], [])

    def test_a_box_with_problems_fails(self):
        port = free_port()
        server = mock_device.serve(port, "problems", tap_every=0)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        failed = [r.name for r in smoke_test.run(args(f"127.0.0.1:{port}")).results if not r.ok]
        self.assertIn("a reader was found", failed)
        self.assertIn("Wi-Fi is connected", failed)

    def test_an_unreachable_box(self):
        report = smoke_test.run(args(f"127.0.0.1:{free_port()}", timeout=1))
        self.assertEqual(report.failed, 1)


if __name__ == "__main__":
    unittest.main()
