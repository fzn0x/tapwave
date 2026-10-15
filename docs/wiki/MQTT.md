# MQTT

Tapwave works with any MQTT 3.1.1 broker, with or without Home Assistant. Settings, Home Assistant
in the web UI holds the broker address, user, TLS and the two prefixes below.
[ARCHITECTURE.md section 12](https://github.com/fzn0x/tapwave/blob/main/ARCHITECTURE.md#12-mqtt-and-home-assistant)
is the full specification.

## Topics

Every topic starts with the topic prefix, `tapwave/<id>` by default. `<id>` is the last six hex
digits of the box's MAC address, so it stays the same when you rename the box. The examples use
`tapwave/a1b2c3`.

| Topic | Direction | Retained | Payload |
| --- | --- | --- | --- |
| `status` | from the box | yes | `online`, or `offline` (last will) |
| `tag/scanned` | from the box | no | Scan JSON, one message per scan |
| `tag/last` | from the box | yes | Scan JSON of the most recent card |
| `tag/present` | from the box | yes | `ON` while a card is on the reader, `OFF` after it is lifted |
| `tag/event` | from the box | no | `{"event_type": "scanned", "removed" or "music", ...}` |
| `diagnostics` | from the box | yes | `rssi`, `ip`, `uptime`, `heap`, `reader`, `version`, every 60 seconds |
| `buzzer/state`, `led/state` | from the box | yes | `ON` or `OFF` |
| `buzzer/set`, `led/set` | to the box | | `ON` or `OFF` |
| `cmd/restart` | to the box | | any payload |
| `cmd/identify` | to the box | | any payload: blink and beep |
| `cmd/feedback` | to the box | | `ok` or `error`: confirmation beep and light for the last scan |
| `cmd/rtttl` | to the box | | an RTTTL melody for the buzzer |
| `update/state` | from the box | yes | the firmware update entity's state |
| `update/install` | to the box | | `install` |

## Scan JSON

```json
{
  "id": "8f3b2c1e-6a5d-4d1e-9c0b-2f4e6a8b0c1d",
  "uid": "04-A2-1B-3C-4D-5E-80",
  "kind": "ha_tag",
  "tag": "NTAG215",
  "uri": "https://www.home-assistant.io/tag/8f3b2c1e-6a5d-4d1e-9c0b-2f4e6a8b0c1d",
  "text": "Kitchen lights",
  "records": [
    {"tnf": 1, "type": "U", "uri": "https://www.home-assistant.io/tag/8f3b2c1e-6a5d-4d1e-9c0b-2f4e6a8b0c1d"},
    {"tnf": 1, "type": "T", "lang": "en", "text": "Kitchen lights"}
  ],
  "reader": "tapwave-a1b2c3",
  "time": "2026-10-15T08:30:12Z"
}
```

- `id` is the ID Home Assistant uses: the Home Assistant tag ID when the card has one, otherwise
  the UID.
- `uid` is uppercase hex with dashes, the same format as ESPHome and adonno/tagreader.
- `kind` is the most specific of `ha_tag`, `tapwave`, `music`, `url`, `text` and `uid`.
- `action` (for `tapwave://` cards) and `music_uri` (for music links) are added when they apply.
- Records other than URI and Text carry their payload as hex in `payload`.
- `time` is UTC and only present once the clock has synced with the time server.

## Discovery

With discovery on, the box publishes retained configuration messages to
`<discovery prefix>/<component>/tapwave_<id>/<key>/config` (discovery prefix `homeassistant` by
default) when it connects, and again whenever Home Assistant announces `online` on
`homeassistant/status`. Turning discovery off publishes empty retained messages, which removes the
entities. The entities are listed on the [Home Assistant](Home-Assistant.md) page.

## Try it from a terminal

```sh
mosquitto_sub -h homeassistant.local -u user -P pass -t 'tapwave/a1b2c3/#' -v
mosquitto_pub -h homeassistant.local -u user -P pass -t tapwave/a1b2c3/cmd/identify -m 1
mosquitto_pub -h homeassistant.local -u user -P pass -t tapwave/a1b2c3/cmd/rtttl \
  -m 'beep:d=8,o=6,b=200:c,e,g'
```

## TLS

Turn on "Use an encrypted connection" and use the broker's TLS port (usually 8883). The box checks
the broker's certificate against the public certificate authorities built into ESP-IDF, so a broker
with a self-signed certificate does not work yet.
