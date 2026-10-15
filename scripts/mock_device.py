"""A pretend Tapwave box for working on the web UI without hardware.

    python scripts/mock_device.py                  # http://localhost:8080
    python scripts/mock_device.py --scenario setup # the first-time Wi-Fi setup flow
    python scripts/mock_device.py --scenario problems

It serves ui/ as the box would and answers the REST API and WebSocket from ARCHITECTURE.md
section 13 with plausible data, tapping a demonstration card every few seconds. Only the
Python standard library is used.
"""

from __future__ import annotations

import argparse
import base64
import copy
import hashlib
import json
import mimetypes
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UI = ROOT / "ui"
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
SESSION = "tapwave_session=mock-session"

DEMO_CARDS = [
    {
        "kind": "ha_tag",
        "tag": "NTAG215",
        "uid": "04-A2-1B-3C-4D-5E-80",
        "id": "8f3b2c1e-6a5d-4d1e-9c0b-2f4e6a8b0c1d",
        "uri": "https://www.home-assistant.io/tag/8f3b2c1e-6a5d-4d1e-9c0b-2f4e6a8b0c1d",
        "text": "Bedtime lights",
        "records": [
            {"tnf": 1, "type": "U", "uri": "https://www.home-assistant.io/tag/8f3b2c1e-6a5d-4d1e-9c0b-2f4e6a8b0c1d"},
            {"tnf": 1, "type": "T", "lang": "en", "text": "Bedtime lights"},
        ],
    },
    {
        "kind": "tapwave",
        "tag": "NTAG213",
        "uid": "04-7C-92-0A-11-3B-80",
        "id": "04-7C-92-0A-11-3B-80",
        "uri": "tapwave://sd/Audiobooks/The%20Gruffalo?mode=audiobook",
        "text": "The Gruffalo",
        "action": {
            "uri": "tapwave://sd/Audiobooks/The%20Gruffalo?mode=audiobook",
            "name": "sd",
            "target": "Audiobooks/The Gruffalo",
            "supported": True,
            "options": {"mode": "audiobook"},
        },
        "records": [
            {"tnf": 1, "type": "U", "uri": "tapwave://sd/Audiobooks/The%20Gruffalo?mode=audiobook"},
            {"tnf": 1, "type": "T", "lang": "en", "text": "The Gruffalo"},
        ],
    },
    {
        "kind": "music",
        "tag": "NTAG216",
        "uid": "04-55-E1-9A-62-2C-81",
        "id": "04-55-E1-9A-62-2C-81",
        "uri": "spotify://playlist/37i9dQZF1DX4sWSpwq3LiO",
        "music_uri": "spotify://playlist/37i9dQZF1DX4sWSpwq3LiO",
        "text": "Sleepy songs",
        "records": [
            {"tnf": 1, "type": "U", "uri": "spotify://playlist/37i9dQZF1DX4sWSpwq3LiO"},
            {"tnf": 1, "type": "T", "lang": "en", "text": "Sleepy songs"},
        ],
    },
    {
        "kind": "uid",
        "tag": "MIFARE Classic 1K",
        "uid": "DE-AD-BE-EF",
        "id": "DE-AD-BE-EF",
        "records": [],
        "ndef_error": "the tag is not formatted for NDEF",
    },
]

SCENARIOS = ("ready", "setup", "problems")


def now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


class Box:
    """The pretend device state, shared by every request thread."""

    def __init__(self, scenario: str, tap_every: float, admin_password: str = ""):
        self.lock = threading.Lock()
        self.scenario = scenario
        self.tap_every = tap_every
        self.admin_password = admin_password
        self.started = time.time()
        self.clients: set[socket.socket] = set()
        self.scans: list[dict] = []
        self.present = False
        self.restart_required = scenario == "problems"
        self.attempt = ""
        self.update = {"phase": "idle", "installed": "0.1.0", "latest": "", "available": False,
                       "release_url": "", "busy": False}
        self.config = {
            "device": {"name": "Kids room box", "hostname": ""},
            "modes": {"home_assistant": True, "player": False},
            "wifi": {"ssid": "" if scenario == "setup" else "Home", "password": "********" if scenario != "setup" else "", "ap_password": ""},
            "mqtt": {"enabled": scenario != "setup", "host": "homeassistant.local", "port": 1883, "tls": False,
                     "username": "tapwave", "password": "********", "topic_prefix": "tapwave/{id}",
                     "discovery": True, "discovery_prefix": "homeassistant"},
            "nfc": {"driver": "auto", "poll_interval_ms": 150, "removal_polls": 3},
            "pins": {k: v for k, v in [("i2c_sda", 21), ("i2c_scl", 22), ("spi_sck", 18), ("spi_miso", 19),
                     ("spi_mosi", 23), ("nfc_cs", 5), ("nfc_reset", -1), ("nfc_irq", -1), ("led", 13),
                     ("buzzer", 4), ("button_previous", -1), ("button_play", -1), ("button_next", -1),
                     ("encoder_a", -1), ("encoder_b", -1), ("encoder_button", -1), ("battery_adc", -1),
                     ("i2s_bclk", -1), ("i2s_lrck", -1), ("i2s_dout", -1), ("sd_cs", -1)]},
            "feedback": {"led": True, "led_count": 1, "led_brightness": 64, "buzzer": True},
            "security": {"admin_password": "********" if admin_password else ""},
            "updates": {"check": True, "manifest_url": "https://fzn0x.github.io/tapwave/ota/manifest.json"},
            "time": {"ntp_server": "pool.ntp.org"},
        }

    # ---- views ------------------------------------------------------------------------------

    def status(self) -> dict:
        problems = self.scenario == "problems"
        setup = self.config["wifi"]["ssid"] == ""
        wifi = {"state": "setup" if setup else ("offline" if problems else "connected"),
                "ssid": self.config["wifi"]["ssid"],
                "access_point": {"active": setup or problems, "ssid": "Tapwave-B2C3"}}
        if wifi["state"] == "connected":
            wifi.update(ip="192.168.1.50", rssi=-58)
        if wifi["access_point"]["active"]:
            wifi["access_point"]["ip"] = "192.168.4.1"
        if self.attempt:
            wifi["attempt"] = self.attempt
        mqtt = {"enabled": self.config["mqtt"]["enabled"], "connected": not problems and not setup,
                "broker": self.config["mqtt"]["host"]}
        if problems:
            mqtt["error"] = "the broker refused the user name or password"
            mqtt["error_code"] = "auth"
        reader = ({"found": False, "problem": "No reader answered."} if problems else
                  {"found": True, "model": "PN532", "summary": "PN532 1.6 (i2c)", "driver": "pn532-i2c"})
        return {
            "device": {"name": self.config["device"]["name"], "hostname": self.config["device"]["hostname"] or "tapwave-a1b2c3",
                       "id": "a1b2c3", "mac": "aa:bb:cc:a1:b2:c3", "chip": "ESP32-D0WD-V3 rev 3.1",
                       "flash": 4194304, "psram": 4194304, "environment": "esp32-4mb"},
            "version": self.update["installed"],
            "uptime": int(time.time() - self.started) + 3600 * 5 + 720,
            "heap": {"free": 143360, "min_free": 120000, "psram_free": 4000000},
            "time": now_iso(),
            "wifi": wifi,
            "mqtt": mqtt,
            "reader": reader,
            "tag_present": self.present,
            "modes": self.config["modes"],
            "update": {"phase": self.update["phase"], "available": self.update["available"], "latest": self.update["latest"]},
            "last_scan": self.scans[0] if self.scans else None,
            "restart_required": self.restart_required,
            "config_issues": [],
            "filesystem": True,
            "auth_required": self.config["security"]["admin_password"] != "",
        }

    def reader(self) -> dict:
        if self.scenario == "problems":
            return {"found": False, "problem": "No reader answered.", "tag_present": False,
                    "attempts": [{"driver": "pn532-i2c", "result": "nothing answered at I2C address 0x24"},
                                 {"driver": "rc522-spi", "result": "no RC522 answered on SPI"},
                                 {"driver": "pn532-spi", "result": "no PN532 answered on SPI: the reader did not answer in time"}],
                    "drivers": self.drivers()}
        return {"found": True, "driver": "pn532-i2c", "model": "PN532", "bus": "i2c", "firmware": "1.6",
                "summary": "PN532 1.6 (i2c)", "tag_present": self.present,
                "attempts": [{"driver": "pn532-i2c", "result": "found PN532 1.6 (i2c)"}], "drivers": self.drivers()}

    @staticmethod
    def drivers() -> list[dict]:
        return [{"id": "pn532-i2c", "description": "PN532 on I2C"}, {"id": "rc522-spi", "description": "RC522 on SPI"},
                {"id": "pn532-spi", "description": "PN532 on SPI"}]

    def public_config(self) -> dict:
        return {"config": self.config, "restart_required": self.restart_required,
                "defaults": {"hostname": "tapwave-a1b2c3"}, "chip": "ESP32", "psram": True}

    # ---- actions ----------------------------------------------------------------------------

    def patch(self, body: dict) -> tuple[int, dict]:
        details = []
        updated = copy.deepcopy(self.config)
        for section, fields in body.items():
            if section not in updated or not isinstance(fields, dict):
                details.append({"path": section, "message": "unknown setting", "warning": False})
                continue
            for key, value in fields.items():
                if key not in updated[section]:
                    details.append({"path": f"{section}.{key}", "message": "unknown setting", "warning": False})
                    continue
                if isinstance(updated[section][key], bool) and not isinstance(value, bool):
                    details.append({"path": f"{section}.{key}", "message": "must be true or false", "warning": False})
                    continue
                if isinstance(updated[section][key], int) and not isinstance(updated[section][key], bool) and not isinstance(value, int):
                    details.append({"path": f"{section}.{key}", "message": "must be a whole number", "warning": False})
                    continue
                if value != "********":
                    updated[section][key] = value
        if not 1 <= updated["mqtt"]["port"] <= 65535:
            details.append({"path": "mqtt.port", "message": "use a port from 1 to 65535", "warning": False})
        used = {}
        for key, pin in updated["pins"].items():
            if pin < 0:
                continue
            if 6 <= pin <= 11:
                details.append({"path": f"pins.{key}", "message": "GPIO 6 to 11 connect the flash chip and cannot be used", "warning": False})
            elif pin in used:
                details.append({"path": f"pins.{key}", "message": f"GPIO {pin} is already used by pins.{used[pin]}", "warning": False})
            used[pin] = key
        if details:
            return 422, {"error": {"code": "invalid_config", "message": "Some settings are not valid. Nothing was saved.", "details": details}}
        restart = updated["pins"] != self.config["pins"] or updated["nfc"]["driver"] != self.config["nfc"]["driver"] \
            or updated["device"]["hostname"] != self.config["device"]["hostname"]
        self.config = updated
        self.restart_required = self.restart_required or restart
        return 200, {"restart_required": self.restart_required, "warnings": []}

    def tap(self, card: dict) -> dict:
        scan = dict(card, time=now_iso(), reader="tapwave-a1b2c3")
        self.scans = [scan, *self.scans][:20]
        self.present = True
        return scan

    def try_wifi(self, ssid: str, password: str) -> None:
        self.attempt = "pending"

        def finish():
            time.sleep(3)
            with self.lock:
                if password == "wrongpass":
                    self.attempt = "failed"
                else:
                    self.attempt = "succeeded"
                    self.config["wifi"]["ssid"] = ssid
                    self.scenario = "ready"
            self.broadcast({"type": "status", "status": self.status()})

        threading.Thread(target=finish, daemon=True).start()

    def run_update(self, install: bool) -> None:
        def steps():
            with self.lock:
                self.update.update(phase="checking", busy=True)
            self.broadcast({"type": "update", "update": self.update})
            time.sleep(1.5)
            with self.lock:
                self.update.update(phase="idle", latest="0.2.0", available=True, busy=install,
                                   release_url="https://github.com/fzn0x/tapwave/releases/tag/v0.2.0")
            self.broadcast({"type": "update", "update": self.update})
            if not install:
                return
            for target in ("web interface", "firmware"):
                for percent in range(0, 101, 10):
                    with self.lock:
                        self.update.update(phase="writing", target=target, percent=percent)
                    self.broadcast({"type": "update", "update": self.update})
                    time.sleep(0.25)
            with self.lock:
                self.update.update(phase="restarting", percent=100)
            self.broadcast({"type": "update", "update": self.update})
            time.sleep(3)
            with self.lock:
                self.update = {"phase": "idle", "installed": "0.2.0", "latest": "0.2.0", "available": False,
                               "release_url": self.update["release_url"], "busy": False}
                self.started = time.time()

        threading.Thread(target=steps, daemon=True).start()

    # ---- WebSocket ----------------------------------------------------------------------------

    def broadcast(self, message: dict) -> None:
        frame = ws_frame(json.dumps(message))
        with self.lock:
            clients = list(self.clients)
        for client in clients:
            try:
                client.sendall(frame)
            except OSError:
                with self.lock:
                    self.clients.discard(client)

    def demo_loop(self) -> None:
        index = 0
        last_status = 0.0
        while True:
            time.sleep(0.5)
            if time.time() - last_status >= 5:
                last_status = time.time()
                with self.lock:
                    status = self.status()
                self.broadcast({"type": "status", "status": status})
            if self.tap_every <= 0 or self.scenario != "ready":
                continue
            phase = (time.time() - self.started) % self.tap_every
            if phase < 0.5:
                with self.lock:
                    scan = self.tap(DEMO_CARDS[index % len(DEMO_CARDS)])
                index += 1
                self.broadcast({"type": "scan", "scan": scan})
            elif self.present and phase >= self.tap_every * 0.6:
                with self.lock:
                    self.present = False
                    uid = self.scans[0]["uid"]
                self.broadcast({"type": "removed", "uid": uid})


def ws_frame(text: str) -> bytes:
    payload = text.encode()
    header = bytearray([0x81])
    if len(payload) < 126:
        header.append(len(payload))
    elif len(payload) < 65536:
        header.append(126)
        header += len(payload).to_bytes(2, "big")
    else:
        header.append(127)
        header += len(payload).to_bytes(8, "big")
    return bytes(header) + payload


def ws_accept(key: str) -> str:
    return base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()


def make_handler(box: Box, version: str):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt, *args):  # quiet by default
            pass

        def send_json(self, status: int, body, cookie: str = "") -> None:
            data = json.dumps(body).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            if cookie:
                self.send_header("Set-Cookie", cookie)
            self.end_headers()
            self.wfile.write(data)

        # Like the firmware, everything but the status and the sign-in itself needs a session
        # once an admin password is set.
        def signed_in(self, path: str) -> bool:
            if not box.admin_password or path in ("/api/status", "/api/session"):
                return True
            basic = "Basic " + base64.b64encode(f"admin:{box.admin_password}".encode()).decode()
            return SESSION in (self.headers.get("Cookie") or "") or self.headers.get("Authorization") == basic

        def refuse(self) -> None:
            self.close_connection = True  # the request body, if any, is never read
            self.send_json(401, {"error": {"code": "unauthorized", "message": "Sign in with the admin password first."}})

        # Like the firmware, a change must say it is JSON (or an image), which other websites
        # cannot send without a CORS preflight.
        def right_type(self, path: str) -> bool:
            upload = path.startswith("/api/update/") and path.split("/")[-1] in ("firmware", "filesystem")
            expected = "application/octet-stream" if upload else "application/json"
            if (self.headers.get("Content-Type") or "").startswith(expected):
                return True
            self.close_connection = True
            self.send_json(415, {"error": {"code": "unsupported_media_type",
                                           "message": f"Send this request with Content-Type: {expected}."}})
            return False

        def session(self) -> None:
            if not box.admin_password:
                return self.send_json(200, {"ok": True, "required": False})
            if self.body().get("password") != box.admin_password:
                return self.send_json(401, {"error": {"code": "wrong_password", "message": "That password is not right."}})
            self.send_json(200, {"ok": True, "required": True},
                           cookie=f"{SESSION}; Path=/; HttpOnly; SameSite=Strict; Max-Age=2592000")

        def body(self) -> dict:
            length = int(self.headers.get("Content-Length") or 0)
            raw = self.rfile.read(length) if length else b""
            try:
                return json.loads(raw) if raw else {}
            except json.JSONDecodeError:
                return {}

        def do_GET(self):
            path = self.path.split("?", 1)[0]
            if path == "/ws":
                return self.websocket()
            if path.startswith("/api/") and not self.signed_in(path):
                return self.refuse()
            routes = {
                "/api/status": lambda: box.status(),
                "/api/scans": lambda: {"scans": box.scans},
                "/api/config": lambda: box.public_config(),
                "/api/reader": lambda: box.reader(),
                "/api/update": lambda: box.update,
                "/api/wifi/networks": lambda: {"scanning": False, "networks": [
                    {"ssid": "Home", "rssi": -48, "secure": True},
                    {"ssid": "Home-Guest", "rssi": -63, "secure": True},
                    {"ssid": "Garden cafe", "rssi": -81, "secure": False}]},
            }
            if path in routes:
                with box.lock:
                    return self.send_json(200, routes[path]())
            if path.startswith("/api/"):
                return self.send_json(404, {"error": {"code": "not_found", "message": "There is no such API endpoint."}})
            return self.static(path)

        def do_PATCH(self):
            if not self.signed_in(self.path):
                return self.refuse()
            if not self.right_type(self.path):
                return None
            if self.path == "/api/config":
                with box.lock:
                    status, body = box.patch(self.body())
                return self.send_json(status, body)
            self.send_json(404, {"error": {"code": "not_found", "message": "There is no such API endpoint."}})

        def do_POST(self):
            path = self.path.split("?", 1)[0]
            if not self.signed_in(path):
                return self.refuse()
            if not self.right_type(path):
                return None
            if path == "/api/session":
                return self.session()
            if path.startswith("/api/update/") and path.split("/")[-1] in ("firmware", "filesystem"):
                remaining = int(self.headers.get("Content-Length") or 0)
                while remaining > 0:
                    remaining -= len(self.rfile.read(min(65536, remaining)))
                return self.send_json(200, {"ok": True, "restarting": True})
            body = self.body()
            if path == "/api/wifi":
                box.try_wifi(body.get("ssid", ""), body.get("password", ""))
                return self.send_json(202, {"ok": True})
            if path in ("/api/update/check", "/api/update/install"):
                box.run_update(path.endswith("install"))
                return self.send_json(202, {"ok": True})
            if path in ("/api/device/restart", "/api/device/identify", "/api/reader/detect",
                        "/api/feedback", "/api/buzzer/rtttl", "/api/config/reset"):
                if path == "/api/device/restart":
                    box.restart_required = False
                return self.send_json(202, {"ok": True})
            self.send_json(404, {"error": {"code": "not_found", "message": "There is no such API endpoint."}})

        def do_DELETE(self):
            if self.path == "/api/session":
                return self.send_json(200, {"ok": True},
                                      cookie="tapwave_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0")
            self.send_json(404, {"error": {"code": "not_found", "message": "There is no such API endpoint."}})

        def static(self, path: str) -> None:
            if path in ("", "/"):
                path = "/index.html"
            file = (UI / path.lstrip("/")).resolve()
            if UI.resolve() not in file.parents or not file.is_file():
                file = UI / "index.html"
            data = file.read_bytes()
            if file.suffix in (".html", ".js", ".css"):
                data = data.replace(b"__TAPWAVE_VERSION__", version.encode())
            self.send_response(200)
            self.send_header("Content-Type", mimetypes.guess_type(file.name)[0] or "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def websocket(self) -> None:
            key = self.headers.get("Sec-WebSocket-Key", "")
            self.send_response(101)
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.send_header("Sec-WebSocket-Accept", ws_accept(key))
            self.end_headers()
            self.wfile.flush()
            conn = self.connection
            with box.lock:
                hello = {"type": "hello", "version": box.update["installed"], "id": "a1b2c3"}
                status = {"type": "status", "status": box.status()}
                box.clients.add(conn)
            conn.sendall(ws_frame(json.dumps(hello)) + ws_frame(json.dumps(status)))
            try:
                while conn.recv(1024):  # the UI never sends anything we act on
                    pass
            except OSError:
                pass
            finally:
                with box.lock:
                    box.clients.discard(conn)
                self.close_connection = True

    return Handler


def serve(port: int, scenario: str, tap_every: float, version: str = "dev",
          admin_password: str = "") -> ThreadingHTTPServer:
    box = Box(scenario, tap_every, admin_password)
    if scenario != "setup":
        box.tap(DEMO_CARDS[0])
        box.present = False
    server = ThreadingHTTPServer(("127.0.0.1", port), make_handler(box, version))
    server.daemon_threads = True
    server.box = box  # for tests
    threading.Thread(target=box.demo_loop, daemon=True).start()
    return server


def main() -> None:
    parser = argparse.ArgumentParser(description="Serve the Tapwave web UI against a pretend box.")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--scenario", choices=SCENARIOS, default="ready")
    parser.add_argument("--tap-every", type=float, default=8.0, help="seconds between demo taps, 0 for none")
    parser.add_argument("--admin-password", default="", help="ask for this password before settings")
    args = parser.parse_args()
    server = serve(args.port, args.scenario, args.tap_every, admin_password=args.admin_password)
    print(f"Tapwave mock box ({args.scenario}) on http://localhost:{args.port}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
