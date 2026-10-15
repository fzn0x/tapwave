"""Checks a Tapwave box on the network from end to end.

    python scripts/smoke_test.py tapwave-a1b2c3.local
    python scripts/smoke_test.py 192.168.1.50 --password secret --tap 30 \\
        --mqtt homeassistant.local --mqtt-user tapwave --mqtt-password secret

Run it after flashing a build and before a release. It reads the status, the web UI, the
configuration and the reader report, makes sure the API refuses what it should, blinks and beeps
the box once, and checks the live WebSocket. With --tap it asks you to tap and lift a card and
checks the scan; with --mqtt it also watches the broker for the same scan and for the Home
Assistant discovery messages. It saves no settings. Only the Python standard library is needed.
"""

from __future__ import annotations

import argparse
import base64
import gzip
import json
import os
import re
import socket
import struct
import sys
import threading
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field

UID = re.compile(r"^[0-9A-F]{2}(-[0-9A-F]{2}){3,9}$")
KINDS = {"ha_tag", "tapwave", "music", "url", "text", "uid"}
SECRETS = (("wifi", "password"), ("wifi", "ap_password"), ("mqtt", "password"),
           ("security", "admin_password"))


@dataclass
class Result:
    name: str
    ok: bool
    detail: str = ""


@dataclass
class Report:
    results: list[Result] = field(default_factory=list)

    def check(self, name: str, ok: bool, detail: str = "") -> bool:
        self.results.append(Result(name, ok, detail))
        print(f"{'PASS' if ok else 'FAIL'}  {name}{': ' + detail if detail else ''}", flush=True)
        return ok

    @property
    def failed(self) -> int:
        return sum(not r.ok for r in self.results)


# ---- HTTP ---------------------------------------------------------------------------------------


class Box:
    def __init__(self, host: str, password: str = "", timeout: float = 10):
        self.base = host if host.startswith("http") else f"http://{host}"
        self.password = password
        self.timeout = timeout

    def request(self, method: str, path: str, body: object = None, content_type: str | None = "json",
                auth: bool = True) -> tuple[int, dict, object]:
        """Returns the status, the headers and the decoded body (JSON when it is JSON)."""
        headers = {"Accept": "application/json"}
        data = None
        if method != "GET":
            data = json.dumps(body if body is not None else {}).encode()
            if content_type == "json":
                headers["Content-Type"] = "application/json"
            elif content_type:
                headers["Content-Type"] = content_type
        if auth and self.password:
            token = base64.b64encode(f"admin:{self.password}".encode()).decode()
            headers["Authorization"] = f"Basic {token}"
        req = urllib.request.Request(self.base + path, data=data, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as response:
                return response.status, dict(response.headers), decode(response.headers, response.read())
        except urllib.error.HTTPError as error:
            return error.code, dict(error.headers), decode(error.headers, error.read())
        except (urllib.error.URLError, OSError) as error:
            return 0, {}, str(getattr(error, "reason", error))


def decode(headers, raw: bytes) -> object:
    if (headers.get("Content-Encoding") or "") == "gzip":
        raw = gzip.decompress(raw)
    if (headers.get("Content-Type") or "").startswith("application/json"):
        try:
            return json.loads(raw)
        except json.JSONDecodeError:
            return None
    return raw.decode("utf-8", "replace")


# ---- WebSocket (client side, receive only) ------------------------------------------------------


class WebSocket:
    def __init__(self, host: str, port: int, path: str = "/ws", timeout: float = 10):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(
            f"GET {path} HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n".encode())
        self.buffer = b""
        while b"\r\n\r\n" not in self.buffer:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("the box closed the connection during the handshake")
            self.buffer += chunk
        head, _, self.buffer = self.buffer.partition(b"\r\n\r\n")
        if b" 101 " not in head.split(b"\r\n")[0]:
            raise ConnectionError(head.split(b"\r\n")[0].decode(errors="replace"))

    def receive(self, timeout: float) -> dict | None:
        """The next JSON message, or None when nothing arrives in time."""
        deadline = time.monotonic() + timeout
        while True:
            frame = read_frame(self.buffer)
            if frame:
                opcode, payload, used = frame
                self.buffer = self.buffer[used:]
                if opcode == 0x1:
                    return json.loads(payload)
                if opcode == 0x8:
                    raise ConnectionError("the box closed the WebSocket")
                continue
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            self.sock.settimeout(remaining)
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                return None
            if not chunk:
                raise ConnectionError("the box closed the WebSocket")
            self.buffer += chunk

    def close(self) -> None:
        self.sock.close()


def read_frame(data: bytes) -> tuple[int, bytes, int] | None:
    """Parses one unmasked server frame: (opcode, payload, bytes used), or None if incomplete."""
    if len(data) < 2:
        return None
    opcode = data[0] & 0x0F
    length = data[1] & 0x7F
    offset = 2
    if length == 126:
        if len(data) < 4:
            return None
        length = struct.unpack(">H", data[2:4])[0]
        offset = 4
    elif length == 127:
        if len(data) < 10:
            return None
        length = struct.unpack(">Q", data[2:10])[0]
        offset = 10
    if len(data) < offset + length:
        return None
    return opcode, data[offset:offset + length], offset + length


# ---- MQTT 3.1.1 (subscribe only) ----------------------------------------------------------------


def mqtt_length(n: int) -> bytes:
    out = bytearray()
    while True:
        byte, n = n % 128, n // 128
        out.append(byte | (0x80 if n else 0))
        if not n:
            return bytes(out)


def mqtt_string(text: str) -> bytes:
    raw = text.encode()
    return struct.pack(">H", len(raw)) + raw


def mqtt_connect(client_id: str, user: str = "", password: str = "", keepalive: int = 120) -> bytes:
    flags = 0x02 | (0x80 if user else 0) | (0x40 if password else 0)
    body = mqtt_string("MQTT") + bytes([4, flags]) + struct.pack(">H", keepalive) + mqtt_string(client_id)
    if user:
        body += mqtt_string(user)
    if password:
        body += mqtt_string(password)
    return bytes([0x10]) + mqtt_length(len(body)) + body


def mqtt_subscribe(packet_id: int, topics: list[str]) -> bytes:
    body = struct.pack(">H", packet_id) + b"".join(mqtt_string(t) + b"\x00" for t in topics)
    return bytes([0x82]) + mqtt_length(len(body)) + body


def mqtt_packet(data: bytes) -> tuple[int, bytes, int] | None:
    """Splits off one packet: (first byte, body, bytes used), or None if incomplete."""
    length, multiplier, i = 0, 1, 1
    while True:
        if i >= len(data):
            return None
        byte = data[i]
        length += (byte & 0x7F) * multiplier
        multiplier *= 128
        i += 1
        if not byte & 0x80:
            break
    if len(data) < i + length:
        return None
    return data[0], data[i:i + length], i + length


def mqtt_publish(first: int, body: bytes) -> tuple[str, bytes, bool]:
    """Topic, payload and the retain flag of a PUBLISH packet."""
    size = struct.unpack(">H", body[:2])[0]
    topic = body[2:2 + size].decode()
    offset = 2 + size + (2 if (first >> 1) & 0x03 else 0)
    return topic, body[offset:], bool(first & 0x01)


class MqttWatcher:
    """Collects every message on the given topics in a background thread."""

    CONNACK_CODES = {1: "protocol version refused", 2: "client ID refused", 3: "broker unavailable",
                     4: "bad user name or password", 5: "not authorized"}

    def __init__(self, host: str, port: int, user: str, password: str, topics: list[str]):
        self.messages: list[tuple[float, str, bytes, bool]] = []
        self.lock = threading.Lock()
        self.sock = socket.create_connection((host, port), timeout=10)
        self.sock.sendall(mqtt_connect(f"tapwave-smoke-{os.getpid()}", user, password))
        self.buffer = b""
        first, body = self._next()
        if first >> 4 != 2 or body[1] != 0:
            self.sock.close()
            raise ConnectionError(self.CONNACK_CODES.get(body[1], f"CONNACK {body[1]}"))
        self.sock.sendall(mqtt_subscribe(1, topics))
        threading.Thread(target=self._run, daemon=True).start()

    def _next(self) -> tuple[int, bytes]:
        while True:
            packet = mqtt_packet(self.buffer)
            if packet:
                first, body, used = packet
                self.buffer = self.buffer[used:]
                return first, body
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("the broker closed the connection")
            self.buffer += chunk

    def _run(self) -> None:
        self.sock.settimeout(None)
        try:
            while True:
                first, body = self._next()
                if first >> 4 == 3:
                    topic, payload, retained = mqtt_publish(first, body)
                    with self.lock:
                        self.messages.append((time.monotonic(), topic, payload, retained))
        except (OSError, ConnectionError):
            pass

    def close(self) -> None:
        try:
            self.sock.sendall(bytes([0xE0, 0x00]))  # DISCONNECT
        except OSError:
            pass
        self.sock.close()

    def find(self, topic: str, since: float = 0.0) -> list[bytes]:
        with self.lock:
            return [p for t, name, p, _ in self.messages if name == topic and t >= since]

    def wait_for(self, topic: str, since: float, timeout: float) -> list[bytes]:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            found = self.find(topic, since)
            if found:
                return found
            time.sleep(0.1)
        return []


# ---- Checks -------------------------------------------------------------------------------------


def check_scan(report: Report, scan: dict) -> None:
    report.check("the scan has a valid UID", bool(UID.match(scan.get("uid", ""))), scan.get("uid", "none"))
    report.check("the scan has an ID for Home Assistant", bool(scan.get("id")), scan.get("id", ""))
    report.check("the scan has a known kind", scan.get("kind") in KINDS, str(scan.get("kind")))


def run(args: argparse.Namespace) -> Report:
    report = Report()
    box = Box(args.host, args.password, args.timeout)

    code, _, status = box.request("GET", "/api/status")
    if not report.check("status answers", code == 200 and isinstance(status, dict),
                        f"HTTP {code}" if code else f"cannot reach {box.base}: {status}"):
        return report
    device = status.get("device", {})
    report.check("firmware version", bool(status.get("version")),
                 f"Tapwave {status.get('version')} ({device.get('environment', '?')}) on {device.get('chip', '?')}")
    report.check("web UI files are present", status.get("filesystem") is True,
                 "" if status.get("filesystem") else "upload littlefs.bin (see the Flashing wiki page)")
    reader = status.get("reader", {})
    report.check("a reader was found", bool(reader.get("found")),
                 reader.get("summary") or reader.get("problem") or "")
    wifi = status.get("wifi", {})
    report.check("Wi-Fi is connected", wifi.get("state") == "connected",
                 f"{wifi.get('ssid', '')} {wifi.get('rssi', '')} dBm".strip())
    if status.get("config_issues"):
        report.check("the stored settings load cleanly", False,
                     "; ".join(i.get("message", "") for i in status["config_issues"]))

    code, headers, page = box.request("GET", "/", auth=False)
    report.check("the web UI is served", code == 200 and isinstance(page, str) and "Tapwave" in page,
                 headers.get("Content-Type", f"HTTP {code}"))

    if status.get("auth_required"):
        code, _, _ = box.request("GET", "/api/config", auth=False)
        report.check("settings need the admin password", code == 401, f"HTTP {code}")
        if not args.password:
            report.check("an admin password was given", False, "pass --password to check the rest")
            return report

    code, _, config = box.request("GET", "/api/config")
    if report.check("settings can be read", code == 200, f"HTTP {code}"):
        values = config.get("config", {})
        leaked = [f"{s}.{k}" for s, k in SECRETS if values.get(s, {}).get(k) not in ("", "********", None)]
        report.check("passwords are never returned", not leaked, ", ".join(leaked))

    code, _, body = box.request("PATCH", "/api/config", {"mqtt": {"port": 70000}})
    paths = [d.get("path") for d in (body or {}).get("error", {}).get("details", [])] if isinstance(body, dict) else []
    report.check("invalid settings are refused and not saved", code == 422 and "mqtt.port" in paths, f"HTTP {code}")

    code, _, _ = box.request("POST", "/api/device/identify", content_type="text/plain")
    report.check("changes without a JSON content type are refused", code == 415, f"HTTP {code}")

    code, _, _ = box.request("GET", "/api/reader")
    report.check("the reader report answers", code == 200, f"HTTP {code}")

    code, _, _ = box.request("POST", "/api/device/identify")
    report.check("identify (the box should blink and beep now)", code in (200, 202), f"HTTP {code}")

    host = box.base.split("://", 1)[1].split("/", 1)[0]
    name, _, port = host.partition(":")
    try:
        ws = WebSocket(name, int(port or 80), timeout=args.timeout)
    except OSError as error:
        report.check("the WebSocket connects", False, str(error))
        return report
    hello = ws.receive(5)
    report.check("the WebSocket says hello", bool(hello) and hello.get("type") == "hello",
                 json.dumps(hello) if hello else "nothing within 5 s")

    watcher = None
    if args.mqtt:
        watcher = start_mqtt(report, args, status, box)

    if args.tap:
        since = time.monotonic()
        print(f"\n      Tap a card on the reader now (waiting {args.tap:.0f} s).", flush=True)
        scan = wait_for(ws, "scan", args.tap)
        if report.check("a tapped card arrives on the WebSocket", scan is not None, "" if scan else "no scan"):
            check_scan(report, scan["scan"])
            if watcher:
                prefix = status["mqtt"]["topic_prefix"]
                found = watcher.wait_for(f"{prefix}/tag/scanned", since, 5)
                uids = [json.loads(p).get("uid") for p in found]
                report.check("the same scan arrives on MQTT", scan["scan"].get("uid") in uids,
                             f"{prefix}/tag/scanned")
            print("      Now lift the card off the reader.", flush=True)
            removed = wait_for(ws, "removed", args.tap)
            report.check("lifting the card is reported", removed is not None
                         and removed.get("uid") == scan["scan"].get("uid"), "" if removed else "no removal")
    ws.close()
    if watcher:
        watcher.close()
    return report


def start_mqtt(report: Report, args: argparse.Namespace, status: dict, box: Box) -> MqttWatcher | None:
    mqtt = status.get("mqtt", {})
    if not report.check("the box is connected to the broker", bool(mqtt.get("connected")),
                        mqtt.get("error") or mqtt.get("broker", "")):
        return None
    _, _, config = box.request("GET", "/api/config")
    discovery = (config or {}).get("config", {}).get("mqtt", {}).get("discovery_prefix", "homeassistant")
    prefix = mqtt["topic_prefix"]
    node = f"tapwave_{status['device']['id']}"
    host, _, port = args.mqtt.partition(":")
    try:
        watcher = MqttWatcher(host, int(port or 1883), args.mqtt_user, args.mqtt_password,
                              [f"{prefix}/#", f"{discovery}/+/{node}/+/config"])
    except (OSError, ConnectionError) as error:
        report.check("this script connects to the broker", False, str(error))
        return None
    deadline = time.monotonic() + 5
    online = scanner = []
    while time.monotonic() < deadline and not (online and scanner):
        online = watcher.find(f"{prefix}/status")
        scanner = watcher.find(f"{discovery}/tag/{node}/scanner/config")
        time.sleep(0.1)
    report.check("the box is online on MQTT", b"online" in online, f"{prefix}/status")
    report.check("Home Assistant discovery is published", bool(scanner),
                 f"{discovery}/tag/{node}/scanner/config")
    return watcher


def wait_for(ws: WebSocket, kind: str, timeout: float) -> dict | None:
    deadline = time.monotonic() + timeout
    while (remaining := deadline - time.monotonic()) > 0:
        message = ws.receive(remaining)
        if message and message.get("type") == kind:
            return message
    return None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("host", help="the box's address, for example tapwave-a1b2c3.local")
    parser.add_argument("--password", default=os.environ.get("TAPWAVE_PASSWORD", ""),
                        help="the admin password, if one is set (or TAPWAVE_PASSWORD)")
    parser.add_argument("--tap", type=float, default=0, metavar="SECONDS",
                        help="ask for a card to be tapped and lifted, waiting this long for each")
    parser.add_argument("--mqtt", default="", metavar="HOST[:PORT]", help="also watch this broker")
    parser.add_argument("--mqtt-user", default="")
    parser.add_argument("--mqtt-password", default=os.environ.get("TAPWAVE_MQTT_PASSWORD", ""))
    parser.add_argument("--timeout", type=float, default=10)
    args = parser.parse_args(argv)
    report = run(args)
    print(f"\n{len(report.results) - report.failed} passed, {report.failed} failed")
    return 1 if report.failed else 0


if __name__ == "__main__":
    sys.exit(main())
