# Tapwave architecture

Tapwave is ESP32 firmware that turns an NFC reader into three products at once:

1. **A music box.** Tap a card and audio plays from the SD card, a web radio stream or Bluetooth.
2. **A Home Assistant tag reader.** Tap a card and Home Assistant (or Music Assistant) decides what
   happens, on any speaker in the house.
3. **A tag writer.** Create and assign cards from a phone browser, no extra tools.

There is one firmware binary per board. The user switches modes on and off in the web UI, and the
modes run together: a box can play a card locally and report the same scan to Home Assistant.

This document is the reference for how the firmware is built. It describes what exists today and
what is planned. Every planned item names the milestone that delivers it, and the
[feature matrix](#17-feature-matrix) has an honest "not yet" column.

## Contents

1. [Goals and constraints](#1-goals-and-constraints)
2. [Milestones](#2-milestones)
3. [Hardware targets and flash layout](#3-hardware-targets-and-flash-layout)
4. [Source layout](#4-source-layout)
5. [Runtime model](#5-runtime-model)
6. [NFC drivers](#6-nfc-drivers)
7. [Audio drivers](#7-audio-drivers)
8. [Tags and the scan pipeline](#8-tags-and-the-scan-pipeline)
9. [The tapwave:// URI scheme](#9-the-tapwave-uri-scheme)
10. [Configuration](#10-configuration)
11. [Networking and provisioning](#11-networking-and-provisioning)
12. [MQTT and Home Assistant](#12-mqtt-and-home-assistant)
13. [HTTP API and WebSocket](#13-http-api-and-websocket)
14. [Firmware updates](#14-firmware-updates)
15. [Web UI](#15-web-ui)
16. [Testing and CI](#16-testing-and-ci)
17. [Feature matrix](#17-feature-matrix)
18. [Decision log](#18-decision-log)

## 1. Goals and constraints

- **One binary, runtime modes.** Nothing about the product's behaviour is chosen at compile time.
  Board environments differ only in chip and flash size.
- **Audio is optional.** With no DAC and no SD card the firmware boots and works as a pure tag
  reader. Audio code paths start only when the player mode is on and the hardware answers.
- **Pins are configuration.** Every GPIO is set in the web UI and stored in NVS. Changing wiring
  never needs a reflash.
- **The API is the product.** The web UI is a client of the public REST API and WebSocket. Anything
  the UI can do, a script can do.
- **No credentials in source.** Wi-Fi, MQTT and admin passwords live only in NVS. Build files and
  defaults hold no secrets and no personal endpoints.
- **Host-testable logic.** Parsing, classification, configuration, protocol codecs and
  payload builders are plain C++23 with no Arduino or ESP-IDF headers. They run in the PlatformIO
  `native` environment and in CI on every pull request.

## 2. Milestones

| Milestone | Scope | Status |
| --- | --- | --- |
| 1 | Boot, Wi-Fi provisioning (captive portal, Improv serial and BLE), PN532 and RC522 auto-detection, NDEF and UID reads, MQTT with Home Assistant discovery, web UI with live last scan, OTA from the web UI and Home Assistant | In progress |
| 2 | Write mode, tag table (UID mapping), URI scheme actions, Home Assistant and Music Assistant playback | Planned |
| 3 | Local audio engine for SD and streams, `media_player` entity, kids mode (volume limit, sleep timer) | Planned |
| 4 | Bluetooth A2DP sink and source, PN5180, ESP32-S3 as a tested target, resume position per tag, polish | Planned |

## 3. Hardware targets and flash layout

The primary target is an **ESP32-WROVER** module (ESP32 with PSRAM). The **ESP32-S3** is the
secondary target: it builds in CI from the start and becomes a tested target in milestone 4. The
S3 has no Bluetooth Classic, so A2DP (milestone 4) is available on the original ESP32 only.

The firmware is built with PlatformIO on the [pioarduino](https://github.com/pioarduino/platform-espressif32)
platform (Arduino-ESP32 3.3 on ESP-IDF 5.5). Each board environment fixes the chip and flash size:

| Environment | Chip | Flash | App slot (x2, OTA) | LittleFS | Core dump |
| --- | --- | --- | --- | --- | --- |
| `esp32-4mb` | ESP32 (WROVER, WROOM) | 4 MB | 1920 KB | 128 KB | 64 KB |
| `esp32-8mb` | ESP32 | 8 MB | 3264 KB | 1536 KB | 64 KB |
| `esp32-16mb` | ESP32 | 16 MB | 4096 KB | 8064 KB | 64 KB |
| `esp32s3` | ESP32-S3 (N16R8) | 16 MB | 4096 KB | 8064 KB | 64 KB |

The partition tables are in [`partitions/`](partitions). A partition table can only change through
a USB reflash, so these sizes are a promise to every installed device. The milestone 1 firmware is
1.77 MB, 90 % of the 4 MB slot. 4 MB boards keep OTA; if a later build with the audio engine and
Bluetooth audio does not fit 1920 KB, the 4 MB build will ship without Bluetooth audio rather than
without OTA. That decision is measured, not guessed, when it comes up.

PSRAM is used when present (stream buffers, larger JSON documents). Boards without PSRAM run the
tag reader; the audio player will require PSRAM.

## 4. Source layout

```
tapwave/
├── platformio.ini           board environments, native test environment
├── partitions/              partition tables per flash size
├── src/
│   ├── main.cpp             Arduino entry point: builds the App and runs it
│   ├── app/                 wiring: services that connect drivers, network and core
│   ├── core/                pure C++23, no Arduino or ESP-IDF headers (host-tested)
│   ├── drivers/
│   │   ├── nfc/             NFC reader drivers and auto-detection
│   │   │   └── common/      pure protocol code shared by drivers (host-tested)
│   │   ├── audio/           audio output and decoder interfaces (implementations: milestone 3)
│   │   └── io/              status LED, buzzer, buttons, rotary encoder, battery
│   └── net/                 Wi-Fi, provisioning, MQTT, HTTP server, REST API, OTA
├── ui/                      web UI source: HTML, CSS, JavaScript modules, no framework
├── test/                    native unit tests, one folder per module
├── scripts/                 build helpers, UI dev server, device smoke test
└── docs/wiki/               user and contributor guides, mirrored to the GitHub wiki
```

**Dependency rule.** `core` depends on nothing but the C++ standard library and ArduinoJson.
`drivers/nfc/common` depends only on `core`. Drivers depend on `core`. `net` depends on `core`.
`app` is the only place that knows about every other module. The native test environment compiles
`core` and `drivers/nfc/common` on a PC, so an accidental Arduino include in those folders fails CI.

## 5. Runtime model

### Tasks

| Task | Core | Owns |
| --- | --- | --- |
| Arduino `loopTask` | 1 | Event dispatch, Wi-Fi state machine, MQTT publishing, LED animation, buttons |
| `nfc` | 1 | Polls the reader, reads tags, posts scan and removal events |
| esp-mqtt | any | MQTT connection; incoming messages are posted as events |
| esp_http_server | any | HTTP and WebSocket; handlers read state snapshots and post commands as events |
| BLE host | any | Improv BLE, only while the device is unprovisioned |
| `audio` (milestone 3) | 0 | Decoding and I2S output, decided with the audio engine |

### Event bus

Tasks never call into each other. A producer posts an event (for example `TagScanned`,
`TagRemoved`, `MqttCommand`) into a thread-safe queue, and the loop task dispatches it to every
subscriber. Subscribers therefore run on one thread and need no locks. The bus itself
(`core/event_bus.hpp`) is a plain synchronous dispatcher; `app/` wraps it with the queue.

### Boot sequence

1. Start logging on the USB serial port.
2. Load configuration from NVS, migrating it if its schema version is old (section 10).
3. Mount LittleFS (the web UI). A missing or empty filesystem is not fatal: the HTTP server falls
   back to a recovery page built into the firmware that can upload the filesystem image.
4. Start the status LED and buzzer if their pins are set.
5. Detect the NFC reader (section 6) and start the `nfc` task.
6. Start Wi-Fi. With stored credentials, connect as a station. Without them, open the setup access
   point with the captive portal, Improv BLE and Improv serial (section 11).
7. Start the HTTP server, then MQTT if it is configured.
8. After a successful OTA, mark the new image valid once Wi-Fi has been up for 30 seconds.

## 6. NFC drivers

### Two interfaces

The app talks to one interface, `nfc::TagReader`. It hides how a reader gets to the NDEF bytes:

```cpp
namespace tapwave::nfc {

class TagReader {
public:
    virtual ~TagReader() = default;

    // Model, bus and firmware version, for status pages and diagnostics.
    virtual const ReaderInfo& info() const = 0;

    // Cheap presence check. Returns the UID of the tag in the field, or nullopt.
    virtual std::optional<Uid> detect() = 0;

    // Full read of the tag found by the last detect(): family, UID, ATQA, SAK, NDEF bytes.
    virtual std::expected<TagRead, Error> read() = 0;

    // Writes an NDEF message to the tag in the field (milestone 2).
    virtual std::expected<void, Error> write_ndef(std::span<const std::uint8_t> message) = 0;
};

}  // namespace tapwave::nfc
```

Most readers are ISO 14443-A frontends that move raw frames and leave tag memory layouts to the
host. They implement the smaller `nfc::Iso14443aReader` interface:

```cpp
class Iso14443aReader {
public:
    virtual ~Iso14443aReader() = default;
    virtual const ReaderInfo& info() const = 0;
    virtual std::expected<Target, Error> select() = 0;  // activate one Type A tag
    virtual std::expected<Block, Error> mifare_read(std::uint8_t block) = 0;  // 16 bytes
    virtual std::expected<void, Error> ultralight_write(std::uint8_t page, Page data) = 0;
    virtual std::expected<void, Error> classic_auth(std::uint8_t block, KeyType type,
                                                    const ClassicKey& key) = 0;
    virtual std::expected<Bytes, Error> transceive(std::span<const std::uint8_t> frame) = 0;
    virtual void halt() = 0;
};
```

`nfc::Type2TagReader` (in `drivers/nfc/common/`) adapts any `Iso14443aReader` into a `TagReader`.
It identifies the tag (SAK, ATQA, `GET_VERSION`), walks the Type 2 capability container and TLV
blocks, reads NDEF from MIFARE Classic with the public NFC Forum keys, and (milestone 2) writes
NDEF to NTAG21x and Ultralight. Because it is pure logic over an interface, it is tested on the host
against simulated tags.

A reader that handles NDEF itself (an NCI controller such as the PN7150, or a serial reader that
reports NDEF) implements `TagReader` directly and skips the adapter. That is what "any
NDEF-capable reader via a small driver interface" means in practice.

### Supported readers

| Reader | Bus | Interface | Milestone |
| --- | --- | --- | --- |
| PN532 | I2C | `Iso14443aReader` | 1 |
| PN532 | SPI | `Iso14443aReader` | 1 |
| RC522 (MFRC522 and FM17522 clones) | SPI | `Iso14443aReader` | 1 |
| PN5180 | SPI | `Iso14443aReader`, plus ISO 15693 | 4 |

### Supported tags

| Tag | Read UID | Read NDEF | Write NDEF |
| --- | --- | --- | --- |
| NTAG213, NTAG215, NTAG216 | 1 | 1 | 2 |
| MIFARE Ultralight, Ultralight EV1, Ultralight C | 1 | 1 | 2 |
| MIFARE Classic 1K and 4K | 1 | 1 (NFC Forum keys) | Never (read-only by design) |
| ISO 14443-4 (DESFire, phones, bank cards) | 1 | No | No |
| ISO 15693 (ICODE, Tonies-style) | 4 (PN5180) | 4 | Not planned |

Numbers are milestones.

### Auto-detection

At boot, `drivers/nfc/registry` probes drivers in priority order using the pins from the
configuration: PN532 over I2C (address `0x24`, `GetFirmwareVersion`), then RC522 over SPI
(`VersionReg` reads `0x88`, `0x90`, `0x91`, `0x92` or `0xB2`), then PN532 over SPI. The first
probe that answers wins and is reported in the status page and the `reader` diagnostic entity. The
configuration can also pin one driver (`nfc.driver`), which skips probing. If no reader answers,
the device still boots, shows "No reader found" with the probed buses, and retries every 30
seconds so a loose wire can be fixed without a restart.

### Adding a reader

A new reader is one new file in `src/drivers/nfc/`. It implements `Iso14443aReader` or
`TagReader` and registers itself:

```cpp
TAPWAVE_REGISTER_NFC_DRIVER("mychip-spi", /*priority=*/40, probe_mychip);
```

The registry and auto-detection pick it up with no other edits. The
[driver guide](docs/wiki/Adding-a-Reader.md) walks through an example.

## 7. Audio drivers

Audio arrives in milestone 3. The interfaces are fixed now so the player and the reader are built
against the same seams:

```cpp
namespace tapwave::audio {

// Where decoded PCM goes: an I2S DAC (MAX98357A, PCM5102) or, in milestone 4, an A2DP sink.
class AudioOutput {
public:
    virtual ~AudioOutput() = default;
    virtual bool begin(const PcmFormat& format) = 0;
    virtual std::size_t write(std::span<const std::int16_t> frames) = 0;
    virtual void set_volume(float gain) = 0;  // 0.0 to 1.0, after the kids-mode limit
    virtual void stop() = 0;
};

// Where encoded bytes come from: an SD file, an HTTP(S) stream, a playlist entry.
class ByteSource { /* read, seek, size, content type */ };

// MP3, AAC, FLAC, WAV, OGG Vorbis decoders behind one interface.
class Decoder { /* probe, decode into PCM frames */ };

}  // namespace tapwave::audio
```

**License constraint.** Tapwave is MIT licensed. The popular Arduino audio libraries
(ESP32-audioI2S, arduino-audio-tools) are GPL-3.0, and linking them would make the whole binary
GPL. Milestone 3 therefore uses permissively licensed decoders (for example minimp3, dr_flac and
dr_wav, stb_vorbis, and an AAC decoder whose license allows MIT distribution). The final choice is
a decision point at the start of milestone 3.

## 8. Tags and the scan pipeline

A tag is either **data-carrying** (the NDEF URI on the tag is the instruction, so it works with no
server and no configuration) or **UID-mapped** (the device or the server looks the UID up). Every
scan goes through the same pipeline:

```
read  ->  parse NDEF  ->  classify  ->  resolve (tag table, milestone 2)  ->  dispatch to modes
```

1. **Read.** `TagReader::read()` returns the UID, tag family, ATQA, SAK and the raw NDEF message.
2. **Parse.** `core/ndef` decodes every record: well-known URI (with the NFC Forum prefix table)
   and Text, MIME media records, absolute URIs and external types. Unknown records are kept as raw
   bytes so nothing is lost on the way to Home Assistant.
3. **Classify.** `core/scan` looks at all records and fills in the fields below. One tag can match
   several, for example a card with both a Home Assistant tag URL and a `tapwave://` action.
4. **Resolve** (milestone 2). An entry in the tag table, keyed by UID or Home Assistant tag ID,
   overrides what the tag carries. A parent can re-map a pre-written card without rewriting it.
5. **Dispatch.** Each enabled mode receives the scan: the player acts on `action`, Home Assistant
   receives the scan on MQTT, the writer (when armed) writes instead of dispatching.

| Field | Set when | Example |
| --- | --- | --- |
| `uid` | Always | `04-A2-1B-3C-4D-5E-80` |
| `ha_tag_id` | A URI record is `https://www.home-assistant.io/tag/<id>` | `8f3b2c1e-...` |
| `action` | A URI record uses the `tapwave:` scheme (section 9) | `sd`, `/Gruffalo` |
| `music_uri` | A URI record is a music service or Music Assistant URI | `spotify://album/4aaw...` |
| `uri` | The first URI record of any kind | `https://radio.example/live.mp3` |
| `text` | The first Text record | `Gruffalo` |
| `kind` | Always: the highest of `ha_tag`, `tapwave`, `music`, `url`, `text`, `uid` | `ha_tag` |
| `id` | Always: the tag ID Home Assistant sees, `ha_tag_id` if set, otherwise `uid` | |

**UID format.** UIDs are uppercase hex bytes joined by dashes, the format used by ESPHome and
adonno/tagreader. People who move from tagreader keep their existing Home Assistant tags and
automations.

**Music URIs.** A URI is classified as music when it starts with a known service prefix
(`https://open.spotify.com/`, `spotify:`, `https://music.apple.com/`, `sonos-2://`) or has the
Music Assistant shape `<provider>://<media type>/<item>`, where the media type is one of `track`,
`album`, `artist`, `playlist`, `radio`, `audiobook`, `podcast` or `folder`. Those are the URIs
tagreader routes to Music Assistant, plus the generic Music Assistant form.

**Removal.** A tag counts as removed after `nfc.removal_polls` consecutive polls without it
(default 3, about half a second). Removal is reported so a box can pause when a card is lifted
(milestone 3) and Home Assistant can react to it.

## 9. The tapwave:// URI scheme

The scheme lets a card carry its own instruction. It is open: other projects may read and write it,
and this section is the specification.

### Syntax

```
tapwave-uri = "tapwave://" action "/" target [ "?" options ]
action      = 1*( ALPHA / DIGIT / "-" )          ; case-insensitive
target      = *( pchar / "/" )                   ; RFC 3986 path characters
options     = option *( "&" option )
option      = key "=" value                       ; both percent-encoded
```

- Writers percent-encode `%`, `?`, `#`, spaces and every byte outside printable ASCII in the
  target. `:` and `/` stay as they are, so a stream URL needs no encoding unless it has a query
  string.
- Readers split off the options at the first unencoded `?`, then percent-decode the target once.
- Readers ignore options they do not know. A reader that does not know an action reports the scan
  with `action.supported = false` and does nothing else.
- The meaning of an existing action or option never changes. New behaviour gets a new action or a
  new option. The scheme has no version number for that reason.

### Actions

| URI | Meaning | Options |
| --- | --- | --- |
| `tapwave://sd/<path>` | Play a file or folder from the SD card. A folder plays its audio files in natural name order. | `mode`, `track`, `volume` |
| `tapwave://stream/<url>` | Play an HTTP or HTTPS stream. `.m3u` and `.pls` playlists are followed. | `volume` |
| `tapwave://ha/<media_id>` | Ask Home Assistant to play `media_id`, usually through Music Assistant. | `player`, `enqueue`, `radio` |
| `tapwave://cmd/<command>` | Control playback. | none |

`mode` is one of `album` (default for folders: play in order once), `single`, `audiobook`
(remember the position per tag, milestone 4), `shuffle` or `loop`. `track` is a 1-based start
index. `volume` is 0 to 100 and is always capped by the kids-mode limit. `player` is a Home
Assistant entity ID such as `media_player.kitchen`; `enqueue` is `play`, `next`, `add` or
`replace`; `radio` is `true` or `false`.

Commands for `cmd/`:

| Command | Effect |
| --- | --- |
| `play`, `pause`, `toggle`, `stop` | Transport control |
| `next`, `previous` | Skip |
| `volume/<n>`, `volume/+<n>`, `volume/-<n>` | Set or change the volume (0 to 100) |
| `sleep/<minutes>`, `sleep/off` | Sleep timer |
| `shuffle/on`, `shuffle/off`, `shuffle/toggle` | Shuffle |
| `repeat/off`, `repeat/one`, `repeat/all` | Repeat |

When the player mode is off, `cmd/` and `ha/` scans are forwarded to Home Assistant as events, so a
Home Assistant blueprint can apply them to any speaker.

### Examples

```
tapwave://sd/Audiobooks/The%20Gruffalo?mode=audiobook
tapwave://sd/Music/Lullabies?mode=shuffle&volume=30
tapwave://stream/https://icecast.example.org/jazz.mp3
tapwave://ha/spotify://playlist/37i9dQZF1DX4sWSpwq3LiO?player=media_player.kids_room
tapwave://cmd/sleep/20
tapwave://cmd/volume/-10
```

### Plain URIs

- `https://www.home-assistant.io/tag/<id>` is a Home Assistant tag. Its ID is reported, the URL
  is not played.
- Music service and Music Assistant URIs are forwarded to Home Assistant (section 8).
- Any other `http://` or `https://` URI plays locally when the player mode is on and the URL looks
  like audio (`.mp3`, `.aac`, `.m4a`, `.ogg`, `.opus`, `.flac`, `.wav`, `.m3u`, `.pls`, or an
  `audio/*` content type). Otherwise it is forwarded to Home Assistant.

### Space on the tag

Custom schemes have no NFC Forum prefix code, so the whole URI is stored. A URI record of up to
249 characters costs 8 bytes of overhead (TLV, record header, prefix byte and terminator); longer
URIs need the long TLV and record length fields and cost 13 bytes:

| Tag | User memory | Longest `tapwave://` URI |
| --- | --- | --- |
| MIFARE Ultralight | 48 bytes | 40 characters |
| NTAG213 | 144 bytes | 136 characters |
| NTAG215 | 496 bytes | 483 characters |
| NTAG216 | 872 bytes | 859 characters |

The writer (milestone 2) refuses a URI that does not fit and says how many bytes over it is.

## 10. Configuration

The configuration is one JSON document stored as a blob in NVS (namespace `tapwave`, key
`config`). It carries a schema version in `"v"`. The current version is **1**.

On boot `core/config` parses the blob, runs every migration from the stored version to the current
one (`migrate_v1_to_v2`, `migrate_v2_to_v3`, ...), fills missing keys with defaults and validates
the result. A blob that is unreadable or from a newer firmware is kept untouched under
`config.bak` and the device starts with defaults, so a downgrade never destroys settings silently.
Migrations are pure functions over a JSON document and each one has a host test.

| Section | Keys |
| --- | --- |
| `device` | `name`, `hostname` |
| `modes` | `home_assistant` (report scans), `player` (play locally, milestone 3) |
| `wifi` | `ssid`, `password`, `ap_password` |
| `mqtt` | `enabled`, `host`, `port`, `tls`, `username`, `password`, `topic_prefix`, `discovery`, `discovery_prefix` |
| `nfc` | `driver` (`auto` or a driver ID), `poll_interval_ms`, `removal_polls` |
| `pins` | `i2c_sda`, `i2c_scl`, `spi_sck`, `spi_miso`, `spi_mosi`, `nfc_cs`, `nfc_reset`, `nfc_irq`, `led`, `buzzer`, `button_previous`, `button_play`, `button_next`, `encoder_a`, `encoder_b`, `encoder_button`, `battery_adc`, `i2s_bclk`, `i2s_lrck`, `i2s_dout`, `sd_cs` |
| `feedback` | `led`, `led_count`, `led_brightness`, `buzzer` |
| `security` | `admin_password` |
| `updates` | `check`, `manifest_url` |
| `time` | `ntp_server` (timestamps are UTC; the web UI shows them in the browser's time zone) |

A pin of `-1` means "not connected". Pins are validated against the chip before they are saved:
flash pins (GPIO 6 to 11 on the ESP32), PSRAM pins (GPIO 16 and 17 on WROVER modules) and
input-only pins used as outputs (GPIO 34 to 39) are rejected, and a pin used twice is rejected
unless both uses are on the same bus. Strapping pins (0, 2, 5, 12, 15) are allowed with a warning.

Writing to tags is an action, not a mode: the writer arms for one tag and disarms after it.

**Secrets.** `wifi.password`, `wifi.ap_password`, `mqtt.password` and `security.admin_password`
are never returned by the API. A set secret reads back as `"********"`; sending `"********"` back
keeps the stored value, sending `""` clears it.

## 11. Networking and provisioning

### Wi-Fi

| State | Behaviour |
| --- | --- |
| Unprovisioned | Setup access point `Tapwave-XXXX` with captive portal, Improv BLE and Improv serial |
| Connecting | Station mode with stored credentials, 20 second timeout per attempt |
| Connected | Station only; mDNS, SNTP, HTTP and MQTT running |
| Offline | Retries with backoff. After 60 seconds offline the setup access point opens alongside the station so credentials can be fixed, and closes again once the station connects |

The access point is open unless `wifi.ap_password` is set; it only exists while the device has no
working Wi-Fi. `XXXX` is the end of the MAC address, which is also printed on the serial console.

### Captive portal

A DNS server answers every name with the access point address, and the HTTP server redirects the
operating systems' connectivity checks (`/generate_204`, `/hotspot-detect.html`,
`/connecttest.txt`, `/ncsi.txt`) to the setup page. The setup page lists nearby networks, takes the
password, and once the device joins shows its new address (`http://<hostname>.local`).

### Improv Wi-Fi

[Improv](https://www.improv-wifi.com) is the open standard that ESPHome, ESP Web Tools and Home
Assistant use to onboard devices:

- **Improv serial** is always on, on the USB serial port. It lets the browser-based installer and
  the Improv website send Wi-Fi credentials right after flashing.
- **Improv BLE** runs only while the device is unprovisioned. Home Assistant's Improv via BLE
  integration discovers it and offers to set it up. The device starts in the authorized state (no
  button press needed), the same as an ESPHome device without an authorizer. BLE is shut down after
  provisioning to give its memory back.

Both share one codec (`core/improv`) that implements commands 0x01 to 0x07: send Wi-Fi settings,
current state or identify, device info, Wi-Fi scan, hostname, device name and network state.

### Discovery and time

mDNS announces `<hostname>.local` with `_http._tcp` and `_tapwave._tcp` (TXT: `version`, `id`).
SNTP sets the clock so scan history and logs carry real timestamps; without it they show uptime.

## 12. MQTT and Home Assistant

**Implemented: MQTT with Home Assistant discovery.** The ESPHome native API is a stretch goal and
is not implemented. With MQTT, Tapwave works with any broker and any home server, and Home
Assistant creates the device and its entities on its own.

The MQTT client is ESP-IDF's esp-mqtt (MQTT 3.1.1, optional TLS with the built-in certificate
bundle). Every topic sits under a prefix, `mqtt.topic_prefix`, which defaults to `tapwave/<id>`.
`<id>` is the last six hex digits of the Wi-Fi MAC address, so it survives renames.

### Topics

| Topic | Direction | Retained | Payload |
| --- | --- | --- | --- |
| `<prefix>/status` | out | yes | `online` or `offline` (last will) |
| `<prefix>/tag/scanned` | out | no | Scan JSON, one message per scan |
| `<prefix>/tag/last` | out | yes | Scan JSON of the most recent tag |
| `<prefix>/tag/present` | out | yes | `ON` while a tag is on the reader, `OFF` after removal |
| `<prefix>/tag/event` | out | no | `{"event_type": "scanned" or "removed" or "music", ...}` |
| `<prefix>/diagnostics` | out | yes | `{"rssi", "ip", "uptime", "heap", "reader", "version"}` every 60 s |
| `<prefix>/buzzer/state`, `<prefix>/led/state` | out | yes | `ON` or `OFF` |
| `<prefix>/buzzer/set`, `<prefix>/led/set` | in | | `ON` or `OFF` |
| `<prefix>/cmd/restart` | in | | any payload |
| `<prefix>/cmd/identify` | in | | any payload: blink and beep |
| `<prefix>/cmd/feedback` | in | | `ok` or `error`: confirmation beep and LED for the last scan |
| `<prefix>/cmd/rtttl` | in | | An RTTTL melody for the buzzer |
| `<prefix>/update/state` | out | yes | Update entity JSON (section 14) |
| `<prefix>/update/install` | in | | `install` |
| `<prefix>/cmd/write` | in | | Milestone 2: arm the writer with a URI |
| `<prefix>/player/...` | both | | Milestone 3: media player state and commands |

`cmd/feedback` and `cmd/rtttl` cover the `rfidreader_tag_ok`, `rfidreader_tag_ko` and
`play_rtttl` services that tagreader users call from automations.

Scan JSON:

```json
{
  "id": "8f3b2c1e-6a5d-4d1e-9c0b-2f4e6a8b0c1d",
  "uid": "04-A2-1B-3C-4D-5E-80",
  "kind": "ha_tag",
  "tag": "NTAG215",
  "uri": "https://www.home-assistant.io/tag/8f3b2c1e-6a5d-4d1e-9c0b-2f4e6a8b0c1d",
  "text": "Kitchen lights",
  "records": [
    {"tnf": 1, "type": "U", "uri": "https://www.home-assistant.io/tag/8f3b2c1e-..."},
    {"tnf": 1, "type": "T", "lang": "en", "text": "Kitchen lights"}
  ],
  "reader": "tapwave-a1b2c3",
  "time": "2026-10-15T08:30:12Z"
}
```

`time` is present once SNTP has synced. Records that are neither URI nor Text carry `payload` as
hex.

### Discovery

Discovery messages are retained and published per component to
`<discovery_prefix>/<component>/tapwave_<id>/<key>/config` (`discovery_prefix` defaults to
`homeassistant`). They are published on connect and again whenever Home Assistant announces
`online` on `<discovery_prefix>/status`. Turning discovery off publishes empty retained payloads,
which removes the entities. Every message carries the same `device` block (identifiers, MAC
connection, name, model, firmware version, configuration URL) and an `origin` block.

| Entity | Component | Key | Notes |
| --- | --- | --- | --- |
| Tag scanner | `tag` | `scanner` | `topic: <prefix>/tag/scanned`, `value_template: {{ value_json.id }}`. Home Assistant fires `tag_scanned` and the tag appears under Settings, Tags. |
| Last tag | `sensor` | `last_tag` | State is the tag ID, attributes are the full scan JSON |
| Tag present | `binary_sensor` | `tag_present` | |
| Tag events | `event` | `tag_event` | Event types `scanned`, `removed`, `music` |
| Buzzer | `switch` | `buzzer` | Configuration category |
| Status LED | `switch` | `led` | Configuration category |
| Restart | `button` | `restart` | Device class `restart` |
| Identify | `button` | `identify` | Device class `identify` |
| Firmware | `update` | `firmware` | Installs from the release manifest (section 14) |
| Wi-Fi signal | `sensor` | `wifi_signal` | Diagnostic, dBm |
| Reader | `sensor` | `reader` | Diagnostic: model and firmware, or `none` |
| IP address | `sensor` | `ip` | Diagnostic |
| Uptime | `sensor` | `uptime` | Diagnostic, seconds |
| Media player | `media_player` | `player` | Milestone 3, when the player mode is on |

Availability for every entity comes from `<prefix>/status`.

### Moving from adonno/tagreader

- UIDs use the same format, so existing tags keep working.
- Home Assistant tag URLs report the same tag ID.
- Music tags arrive as the `music` event type on the `tag_event` entity instead of the
  `esphome.music_tag` event. They also fire `tag_scanned` with the UID, which tagreader did not do.
- Tag removal arrives as the `removed` event type instead of `esphome.tag_removed`.

## 13. HTTP API and WebSocket

The HTTP server is ESP-IDF's `esp_http_server` with WebSocket support. All API responses are JSON.
The web UI uses nothing else, so this table is also the list of things a script can do.

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/api/status` | Device, firmware, uptime, memory, Wi-Fi, MQTT, reader, modes and the last scan |
| `GET` | `/api/scans` | The last 20 scans, newest first |
| `GET` | `/api/config` | Configuration, secrets redacted |
| `PATCH` | `/api/config` | Merge a partial configuration. Returns the result and `restart_required`, or `422` with one error per invalid field |
| `POST` | `/api/config/reset` | Factory reset, body `{"confirm": true}` |
| `GET` | `/api/wifi/networks` | Nearby networks; starts a scan when the cached list is old |
| `POST` | `/api/wifi` | Save `{"ssid", "password"}` and connect |
| `GET` | `/api/reader` | Detected reader and the probe report |
| `POST` | `/api/reader/detect` | Probe the buses again |
| `POST` | `/api/device/restart` | Restart |
| `POST` | `/api/device/identify` | Blink and beep |
| `POST` | `/api/feedback` | `{"result": "ok" or "error"}` |
| `POST` | `/api/buzzer/rtttl` | `{"melody": "<RTTTL>"}` |
| `POST` | `/api/session` | `{"password"}`: sign in, sets a session cookie |
| `DELETE` | `/api/session` | Sign out |
| `GET` | `/api/update` | Installed and latest version, progress, last error |
| `POST` | `/api/update/check` | Fetch the release manifest now |
| `POST` | `/api/update/install` | Install the latest release from the manifest |
| `POST` | `/api/update/firmware` | Upload a firmware image, `Content-Type: application/octet-stream` |
| `POST` | `/api/update/filesystem` | Upload a web UI (LittleFS) image |
| `GET` | `/ws` | WebSocket for live events |

Milestones 2 and 3 add `/api/tags` (tag table, import and export), `/api/writer`, `/api/library`
and `/api/player`.

Errors use one shape: `{"error": {"code": "invalid_config", "message": "...", "details": [...]}}`
with a matching HTTP status.

**Access control.** With no admin password the API is open to the local network, like most home
devices. With `security.admin_password` set, every endpoint except `GET /api/status` and
`POST /api/session` needs either the session cookie (`HttpOnly`, `SameSite=Strict`) or HTTP Basic
authentication with user `admin`, so scripts and `curl` keep working. Requests that change state
must send `Content-Type: application/json` or `application/octet-stream`, which browsers do not
allow other websites to send without a CORS preflight that the device never approves. Traffic is
plain HTTP on the local network.

**WebSocket.** The server pushes JSON messages; the client sends nothing.

| `type` | Payload |
| --- | --- |
| `hello` | Firmware version and device ID, on connect |
| `scan` | `scan`: the scan JSON from section 12 |
| `removed` | `uid` |
| `status` | `status`: the `/api/status` body, every 5 seconds and on change |
| `update` | `update`: the `/api/update` body while an update runs |

## 14. Firmware updates

There are three ways to update, and all three end in the same code path:

1. **Upload** a `.bin` in the web UI or with `POST /api/update/firmware` and
   `POST /api/update/filesystem`.
2. **Home Assistant** shows the `update` entity. Pressing Install publishes `install` on
   `<prefix>/update/install`.
3. **API**: `POST /api/update/install`.

Routes 2 and 3 download from the release manifest at `updates.manifest_url` (default
`https://fzn0x.github.io/tapwave/ota/manifest.json`, published by the release workflow):

```json
{
  "version": "0.2.0",
  "release_url": "https://github.com/fzn0x/tapwave/releases/tag/v0.2.0",
  "builds": {
    "esp32-4mb": {
      "firmware": {"url": "https://...", "size": 1203456, "sha256": "..."},
      "filesystem": {"url": "https://...", "size": 131072, "sha256": "..."}
    }
  }
}
```

The device picks the entry for its own environment, streams the filesystem image and then the
firmware over HTTPS (verified against the built-in certificate bundle), checks each SHA-256 while
writing, and restarts. The new image boots in "pending verify" state and is marked valid once Wi-Fi
has been up for 30 seconds. If it crashes or never gets online, the bootloader rolls back to the
previous image on the next reset.

The `update` entity state:

```json
{"installed_version": "0.1.0", "latest_version": "0.2.0", "title": "Tapwave",
 "release_url": "https://github.com/fzn0x/tapwave/releases/tag/v0.2.0",
 "in_progress": false, "update_percentage": null}
```

## 15. Web UI

The web UI lives in [`ui/`](ui): plain HTML, CSS and JavaScript modules with no framework and no
network access beyond the device. Its tabs call tags "cards", the word a parent uses. `scripts/build_ui.py` gzips it into the LittleFS image. The
firmware also contains a small recovery page, so a board flashed without the filesystem image can
still upload one.

| Page | Milestone 1 | Later |
| --- | --- | --- |
| Home | The last card tapped (live), then one row each for the reader, Wi-Fi, Home Assistant and updates | Player controls (3) |
| Cards | Recent scans with copyable IDs | Tag table, assign, write, import and export JSON (2) |
| Library | Explains what it will do | SD card browser and upload (3) |
| Settings | Modes, Wi-Fi, Home Assistant and MQTT, reader and pins, LED and buzzer, admin password, time | Audio, kids mode (3) |
| Update | Version, check, install, upload | |

The UI is written for a parent setting up a box, not for a developer: plain words, one task per
screen, pin numbers behind an "Advanced" toggle, and every error says what happened and what to do
next. It is mobile first, keyboard accessible and readable in light and dark mode.

## 16. Testing and CI

| Layer | Tool | Runs |
| --- | --- | --- |
| `core` and `drivers/nfc/common` | doctest in the PlatformIO `native` environment | Every pull request, also with AddressSanitizer and UBSan |
| NFC drivers over simulated tags | doctest with fake `Iso14443aReader` implementations | Every pull request |
| Web UI modules | `node --test` | Every pull request |
| Build scripts and dev server | Python `unittest` | Every pull request |
| Firmware | `pio run` for every board environment, plus the filesystem image | Every pull request; size report in the job summary |
| Formatting | clang-format | Every pull request |
| Real hardware | `scripts/smoke_test.py` against a device on the network | By hand before a release |

Tags `v*` trigger the release workflow: it builds every environment, attaches app, filesystem and
merged factory images to the GitHub release, and publishes the OTA manifest and the browser
installer to GitHub Pages.

## 17. Feature matrix

Compared with the projects Tapwave draws from, as of October 2026. Facts come from each project's
README, documentation and source; `?` marks something we could not confirm. Corrections are
welcome as pull requests.

The Tapwave column is honest about the present: **Yes** works in the current firmware,
**Not yet (Mn)** is planned for milestone n.

| Feature | Tapwave | ESPuino | Phoniebox v3 | TonUINO-TNG | adonno/tagreader |
| --- | --- | --- | --- | --- | --- |
| Hardware | ESP32 (WROVER); ESP32-S3 not yet (M4) | ESP32 with PSRAM | Raspberry Pi | Arduino Nano, Nano Every, AiO boards, Nano ESP32 | ESP8266 D1 mini (ESPHome) |
| License | MIT | GPL-3.0 | MIT | GPL-3.0 | GPL-3.0 |
| NFC readers | PN532 (I2C, SPI), RC522; PN5180 not yet (M4) | RC522, PN5180 | RC522, PN532 (I2C), RDM6300, USB, nfcpy | RC522 | PN532 (I2C) |
| Reader picked at runtime | Yes, auto-detected | ? | Setup script | No, build time | No, build time |
| Reads NDEF (URI, Text, MIME) | Yes | No, UID | No, card ID | No, own card format | Yes |
| Cards carry their own instruction | Read and reported: yes. Acted on: not yet (M2) | No | No | Yes, own format | No, Home Assistant decides |
| Writes cards | Not yet (M2) | No | No | Yes, from the admin menu | Yes, from Home Assistant |
| Home Assistant | MQTT discovery: tag scanner, sensors, switches, update entity | MQTT, entities set up by hand | MQTT, entities set up by hand | No | ESPHome API, `tag_scanned` |
| Music Assistant | Not yet (M2) | No | No | No | Yes, music tag events |
| Plays from SD card | Not yet (M3) | Yes | Yes, local folders | Yes, through DFPlayer | No |
| Web radio | Not yet (M3) | Yes | Yes | No | No |
| Bluetooth audio | Not yet (M4) | A2DP sink and source | Bluetooth output | No | No |
| Resume position per card | Not yet (M4) | Yes | Not yet in v3 | Remembers the track | No |
| Volume limit, sleep timer | Not yet (M3) | Yes | Max volume, shutdown timer | Volume limits | No |
| Buttons, rotary encoder, battery | Pins configurable: yes. Behaviour: not yet (M3) | Yes | GPIO buttons | Yes | No |
| Web UI | Yes | Yes | Yes | ESP32 variant | No |
| REST API | Yes | Yes | RPC | No | No, ESPHome API |
| Firmware update | Web upload, Home Assistant, release manifest | Web upload | System updates | ? | ESPHome OTA |
| Wi-Fi setup | Captive portal, Improv serial, Improv BLE | Access point | Raspberry Pi OS | Access point (ESP32 variant) | Captive portal |
| Change pins without reflashing | Yes, in the web UI | No, `settings-*.h` | Config files | No | No, YAML |

## 18. Decision log

| Decision | Choice | Why |
| --- | --- | --- |
| Name and scheme | Tapwave, `tapwave://` | Chosen by the maintainer. `otp://` read as "one-time password" and tied the scheme to a working name. |
| Framework | Arduino-ESP32 3.3 on pioarduino | Chosen by the maintainer. Large ecosystem, same base as ESPuino, and ESP-IDF APIs (esp-mqtt, NVS, OTA, esp_http_server) stay one include away. |
| Minimum flash | 4 MB with OTA | Chosen by the maintainer. Cheap boards stay supported; bigger boards get bigger slots. |
| UID format | Uppercase hex with dashes | Chosen by the maintainer. Matches ESPHome and tagreader, so migrating users keep their tags. |
| C++ standard | C++23 (`gnu++23`) | The newest published ISO standard. C++26 is not yet published and GCC 14, the ESP32 toolchain, implements only parts of it. |
| HTTP server | `esp_http_server` | Built into the framework (no dependency), Apache-2.0, WebSocket support. ESPAsyncWebServer would add two LGPL-3.0 libraries. |
| MQTT client | esp-mqtt | Built in, runs in its own task, TLS and reconnects included. |
| BLE stack | NimBLE-Arduino 2.5.1 | Measured on this platform, Bluedroid's BLE host adds about 760 KB and NimBLE's about 240 KB. With Bluedroid the milestone 1 firmware was 2.29 MB, 320 KB over the 1.92 MB OTA slot of 4 MB boards; with NimBLE it is 1.77 MB. Milestone 4's A2DP needs Bluedroid, so the BLE host is revisited there. |
| Timestamps | UTC, formatted by `core/time_format` | With the PSRAM cache workaround the C library runs from IRAM; strftime, gmtime_r and tzset alone overflowed it by 4 KB. |
| Home Assistant | MQTT discovery | Works with any broker and home server; the ESPHome native API stays a stretch goal. |
| NFC drivers | Own protocol code over `Wire` and `SPI` | One interface for all readers, bounded timeouts instead of blocking waits, frame codecs tested on the host, no license mixing. |
| Config storage | JSON blob in NVS | Atomic writes and wear levelling, and it survives reflashing the web UI filesystem. |
| Unit tests | doctest | Header only, fast to compile, supported by PlatformIO. |
| Web UI | No framework | Small enough to gzip into 128 KB of LittleFS, and contributors need only a browser and Python. |
