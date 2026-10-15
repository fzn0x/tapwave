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
a USB reflash, so these sizes are a promise to every installed device. 4 MB boards keep OTA; if the
milestone 4 build with Bluetooth audio does not fit 1920 KB, the 4 MB build will ship without
Bluetooth audio rather than without OTA. That decision is measured, not guessed, in milestone 4.

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
