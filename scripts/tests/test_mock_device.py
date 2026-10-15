import base64
import json
import os
import socket
import sys
import threading
import time
import unittest
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import mock_device  # noqa: E402


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class MockDeviceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = free_port()
        cls.server = mock_device.serve(cls.port, "ready", tap_every=0, version="9.9.9")
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.base = f"http://127.0.0.1:{cls.port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def request(self, method, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.base + path, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=5) as response:
                return response.status, json.loads(response.read() or b"null")
        except urllib.error.HTTPError as error:
            return error.code, json.loads(error.read() or b"null")

    def test_status_has_the_documented_shape(self):
        status, body = self.request("GET", "/api/status")
        self.assertEqual(status, 200)
        for key in ("device", "version", "wifi", "mqtt", "reader", "update", "last_scan", "restart_required"):
            self.assertIn(key, body)
        self.assertEqual(body["wifi"]["state"], "connected")
        self.assertEqual(body["last_scan"]["kind"], "ha_tag")

    def test_config_patch_validates_like_the_firmware(self):
        status, body = self.request("PATCH", "/api/config", {"mqtt": {"port": 70000}})
        self.assertEqual(status, 422)
        self.assertEqual(body["error"]["details"][0]["path"], "mqtt.port")
        status, body = self.request("PATCH", "/api/config", {"pins": {"led": 21}})
        self.assertEqual(status, 422)
        self.assertIn("already used", body["error"]["details"][0]["message"])
        status, body = self.request("PATCH", "/api/config", {"pins": {"led": 25}})
        self.assertEqual(status, 200)
        self.assertTrue(body["restart_required"])

    def test_changes_must_say_they_are_json(self):
        req = urllib.request.Request(self.base + "/api/device/identify", data=b"{}", method="POST",
                                     headers={"Content-Type": "text/plain"})
        with self.assertRaises(urllib.error.HTTPError) as caught:
            urllib.request.urlopen(req, timeout=5)
        self.assertEqual(caught.exception.code, 415)
        self.assertEqual(json.loads(caught.exception.read())["error"]["code"], "unsupported_media_type")

    def test_unknown_endpoints_answer_json_404(self):
        status, body = self.request("GET", "/api/nope")
        self.assertEqual(status, 404)
        self.assertEqual(body["error"]["code"], "not_found")

    def test_static_files_get_the_version(self):
        with urllib.request.urlopen(self.base + "/", timeout=5) as response:
            html = response.read().decode()
        self.assertIn("app.js?v=9.9.9", html)
        self.assertNotIn("__TAPWAVE_VERSION__", html)

    def test_websocket_handshake_and_first_messages(self):
        key = base64.b64encode(os.urandom(16)).decode()
        with socket.create_connection(("127.0.0.1", self.port), timeout=5) as sock:
            sock.sendall(
                f"GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n".encode()
            )
            data = b""
            deadline = time.time() + 5
            while b"\r\n\r\n" not in data and time.time() < deadline:
                data += sock.recv(4096)
            head, _, rest = data.partition(b"\r\n\r\n")
            self.assertIn(b"101", head.split(b"\r\n")[0])
            self.assertIn(mock_device.ws_accept(key).encode(), head)
            while len(rest) < 2 and time.time() < deadline:
                rest += sock.recv(4096)
            self.assertEqual(rest[0], 0x81)  # final text frame

    def test_accept_key_matches_rfc6455_example(self):
        self.assertEqual(mock_device.ws_accept("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")

    def test_frames_use_the_right_length_encoding(self):
        self.assertEqual(mock_device.ws_frame("hi")[:2], bytes([0x81, 2]))
        long = mock_device.ws_frame("x" * 300)
        self.assertEqual(long[1], 126)
        self.assertEqual(int.from_bytes(long[2:4], "big"), 300)


class SignInTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = free_port()
        cls.server = mock_device.serve(cls.port, "ready", tap_every=0, admin_password="letmein")
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.base = f"http://127.0.0.1:{cls.port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def request(self, method, path, body=None, cookie=""):
        data = json.dumps(body).encode() if body is not None else None
        headers = {"Content-Type": "application/json"}
        if cookie:
            headers["Cookie"] = cookie
        req = urllib.request.Request(self.base + path, data=data, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=5) as response:
                return response.status, json.loads(response.read() or b"null"), response.headers
        except urllib.error.HTTPError as error:
            return error.code, json.loads(error.read() or b"null"), error.headers

    def test_status_stays_open(self):
        status, body, _ = self.request("GET", "/api/status")
        self.assertEqual(status, 200)
        self.assertTrue(body["auth_required"])

    def test_settings_need_a_session(self):
        status, body, _ = self.request("GET", "/api/config")
        self.assertEqual(status, 401)
        self.assertEqual(body["error"]["code"], "unauthorized")
        status, _, _ = self.request("PATCH", "/api/config", {"device": {"name": "x"}})
        self.assertEqual(status, 401)

    def test_wrong_password(self):
        status, body, _ = self.request("POST", "/api/session", {"password": "nope"})
        self.assertEqual(status, 401)
        self.assertEqual(body["error"]["code"], "wrong_password")

    def test_signing_in_opens_the_settings(self):
        status, body, headers = self.request("POST", "/api/session", {"password": "letmein"})
        self.assertEqual(status, 200)
        self.assertTrue(body["required"])
        cookie = headers["Set-Cookie"].split(";", 1)[0]
        status, body, _ = self.request("GET", "/api/config", cookie=cookie)
        self.assertEqual(status, 200)
        self.assertEqual(body["config"]["security"]["admin_password"], "********")


if __name__ == "__main__":
    unittest.main()
