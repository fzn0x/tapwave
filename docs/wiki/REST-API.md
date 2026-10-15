# REST API

Everything the web UI does goes through this API, so anything you can click, a script can do. All
responses are JSON. The examples use `http://tapwave-a1b2c3.local`; replace it with your box's
address. [ARCHITECTURE.md section 13](https://github.com/fzn0x/tapwave/blob/main/ARCHITECTURE.md#13-http-api-and-websocket)
is the specification.

## Rules that apply to every request

- **Changes need a content type.** `POST`, `PATCH` and `DELETE` requests must send
  `Content-Type: application/json` (or `application/octet-stream` for image uploads), even with an
  empty `{}` body. Browsers do not let other websites send that without asking the box first, and
  the box never agrees, so a web page you visit cannot change your box's settings.
- **The admin password**, when one is set, protects every endpoint except `GET /api/status` and
  `POST /api/session`. Scripts use HTTP Basic authentication with the user `admin`:
  `curl -u admin:yourpassword ...`. The web UI signs in once and keeps a session cookie.
- **Errors** have one shape and a matching HTTP status:

  ```json
  {"error": {"code": "invalid_config", "message": "Some settings are not valid. Nothing was saved.",
             "details": [{"path": "mqtt.port", "message": "use a port from 1 to 65535", "warning": false}]}}
  ```

## Endpoints

| Method | Path | What it does |
| --- | --- | --- |
| `GET` | `/api/status` | Device, firmware, uptime, memory, Wi-Fi, MQTT, reader, modes and the last scan |
| `GET` | `/api/scans` | The last 20 scans, newest first |
| `GET` | `/api/config` | Every setting, with passwords shown as `********` |
| `PATCH` | `/api/config` | Change some settings |
| `POST` | `/api/config/reset` | Erase every setting, body `{"confirm": true}` |
| `GET` | `/api/wifi/networks` | Nearby Wi-Fi networks |
| `POST` | `/api/wifi` | Save `{"ssid", "password"}` and connect |
| `GET` | `/api/reader` | The reader that was found, and what each probe reported |
| `POST` | `/api/reader/detect` | Look for the reader again |
| `POST` | `/api/device/restart` | Restart |
| `POST` | `/api/device/identify` | Blink and beep |
| `POST` | `/api/feedback` | `{"result": "ok"}` or `{"result": "error"}`: confirmation for the last scan |
| `POST` | `/api/buzzer/rtttl` | `{"melody": "<RTTTL>"}` |
| `POST` | `/api/session` | Sign in with `{"password"}`; sets a session cookie |
| `DELETE` | `/api/session` | Sign out |
| `GET` | `/api/update` | Installed and latest version, progress, last error |
| `POST` | `/api/update/check` | Check the release list now |
| `POST` | `/api/update/install` | Install the latest release |
| `POST` | `/api/update/firmware` | Upload a firmware image |
| `POST` | `/api/update/filesystem` | Upload a web UI image |
| `GET` | `/ws` | WebSocket with live events |

Writing cards (`/api/tags`, `/api/writer`), the music library and the player arrive in milestones
2 and 3.

## Examples

Read the status and the last scans:

```sh
curl http://tapwave-a1b2c3.local/api/status
curl http://tapwave-a1b2c3.local/api/scans
```

Change settings. Send only what changes; the answer says whether a restart is needed:

```sh
curl -X PATCH -H 'Content-Type: application/json' \
  -d '{"device": {"name": "Kids room box"}, "feedback": {"led_brightness": 32}}' \
  http://tapwave-a1b2c3.local/api/config
```

```json
{"restart_required": false, "warnings": []}
```

Passwords read back as `********`. Sending `********` keeps the stored password, sending `""`
clears it.

Blink and beep, and confirm the last scan:

```sh
curl -X POST -H 'Content-Type: application/json' -d '{}' http://tapwave-a1b2c3.local/api/device/identify
curl -X POST -H 'Content-Type: application/json' -d '{"result": "ok"}' http://tapwave-a1b2c3.local/api/feedback
```

Upload an update (the box restarts when it is done):

```sh
curl -X POST -H 'Content-Type: application/octet-stream' --data-binary @littlefs.bin \
  http://tapwave-a1b2c3.local/api/update/filesystem
curl -X POST -H 'Content-Type: application/octet-stream' --data-binary @firmware.bin \
  http://tapwave-a1b2c3.local/api/update/firmware
```

With an admin password, add `-u admin:yourpassword` to every command except the status.

## WebSocket

`ws://tapwave-a1b2c3.local/ws` pushes JSON messages; the box ignores anything you send.

| `type` | Contents |
| --- | --- |
| `hello` | `version` and `id`, once on connect |
| `status` | `status`: the `/api/status` body, every 5 seconds and when something changes |
| `scan` | `scan`: the scan JSON described on the [MQTT](MQTT.md) page |
| `removed` | `uid` of the card that was lifted |
| `update` | `update`: the `/api/update` body while an update runs |

For example, with [websocat](https://github.com/vi/websocat):

```sh
websocat ws://tapwave-a1b2c3.local/ws
```
