// The device configuration (ARCHITECTURE.md section 10).
//
// One JSON document with a schema version, stored in NVS by the device and edited through the
// API. Every field is listed once in for_each_field(), which drives JSON output, patching,
// redaction and restart detection, so a new setting is one line there plus its default.
#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/pins.hpp"

namespace tapwave::config {

inline constexpr int kSchemaVersion = 1;

// Shown instead of a stored secret. Sending it back keeps the stored value.
inline constexpr std::string_view kRedacted = "********";

struct Pins {
    int i2c_sda = -1;
    int i2c_scl = -1;
    int spi_sck = -1;
    int spi_miso = -1;
    int spi_mosi = -1;
    int nfc_cs = -1;
    int nfc_reset = -1;
    int nfc_irq = -1;
    int led = -1;
    int buzzer = -1;
    int button_previous = -1;
    int button_play = -1;
    int button_next = -1;
    int encoder_a = -1;
    int encoder_b = -1;
    int encoder_button = -1;
    int battery_adc = -1;
    int i2s_bclk = -1;
    int i2s_lrck = -1;
    int i2s_dout = -1;
    int sd_cs = -1;

    friend bool operator==(const Pins&, const Pins&) = default;
};

struct Config {
    struct Device {
        std::string name = "Tapwave";
        std::string hostname;  // empty: "tapwave-<id>"
        friend bool operator==(const Device&, const Device&) = default;
    } device;

    struct Modes {
        bool home_assistant = true;  // report scans over MQTT
        bool player = false;         // play cards locally (milestone 3)
        friend bool operator==(const Modes&, const Modes&) = default;
    } modes;

    struct Wifi {
        std::string ssid;
        std::string password;
        std::string ap_password;  // empty: open setup access point
        friend bool operator==(const Wifi&, const Wifi&) = default;
    } wifi;

    struct Mqtt {
        bool enabled = false;
        std::string host;
        int port = 1883;
        bool tls = false;
        std::string username;
        std::string password;
        std::string topic_prefix = "tapwave/{id}";
        bool discovery = true;
        std::string discovery_prefix = "homeassistant";
        friend bool operator==(const Mqtt&, const Mqtt&) = default;
    } mqtt;

    struct Nfc {
        std::string driver = "auto";
        int poll_interval_ms = 150;
        int removal_polls = 3;
        friend bool operator==(const Nfc&, const Nfc&) = default;
    } nfc;

    Pins pins;

    struct Feedback {
        bool led = true;
        int led_count = 1;
        int led_brightness = 64;
        bool buzzer = true;
        friend bool operator==(const Feedback&, const Feedback&) = default;
    } feedback;

    struct Security {
        std::string admin_password;
        friend bool operator==(const Security&, const Security&) = default;
    } security;

    struct Updates {
        bool check = true;
        std::string manifest_url = "https://fzn0x.github.io/tapwave/ota/manifest.json";
        friend bool operator==(const Updates&, const Updates&) = default;
    } updates;

    struct Time {
        std::string ntp_server = "pool.ntp.org";
        std::string timezone = "UTC0";  // POSIX TZ string
        friend bool operator==(const Time&, const Time&) = default;
    } time;

    friend bool operator==(const Config&, const Config&) = default;
};

enum FieldFlags : unsigned {
    kNoFlags = 0,
    kSecret = 1u << 0,   // never returned by the API
    kRestart = 1u << 1,  // takes effect after a restart
};

// Calls f(section, key, value, flags) for every field, in document order. Works on const and
// non-const configs; value is std::string&, bool& or int&.
template <class C, class F>
void for_each_field(C& c, F&& f) {
    f("device", "name", c.device.name, kNoFlags);
    f("device", "hostname", c.device.hostname, kRestart);
    f("modes", "home_assistant", c.modes.home_assistant, kNoFlags);
    f("modes", "player", c.modes.player, kNoFlags);
    f("wifi", "ssid", c.wifi.ssid, kNoFlags);
    f("wifi", "password", c.wifi.password, kSecret);
    f("wifi", "ap_password", c.wifi.ap_password, kSecret);
    f("mqtt", "enabled", c.mqtt.enabled, kNoFlags);
    f("mqtt", "host", c.mqtt.host, kNoFlags);
    f("mqtt", "port", c.mqtt.port, kNoFlags);
    f("mqtt", "tls", c.mqtt.tls, kNoFlags);
    f("mqtt", "username", c.mqtt.username, kNoFlags);
    f("mqtt", "password", c.mqtt.password, kSecret);
    f("mqtt", "topic_prefix", c.mqtt.topic_prefix, kNoFlags);
    f("mqtt", "discovery", c.mqtt.discovery, kNoFlags);
    f("mqtt", "discovery_prefix", c.mqtt.discovery_prefix, kNoFlags);
    f("nfc", "driver", c.nfc.driver, kRestart);
    f("nfc", "poll_interval_ms", c.nfc.poll_interval_ms, kNoFlags);
    f("nfc", "removal_polls", c.nfc.removal_polls, kNoFlags);
    f("pins", "i2c_sda", c.pins.i2c_sda, kRestart);
    f("pins", "i2c_scl", c.pins.i2c_scl, kRestart);
    f("pins", "spi_sck", c.pins.spi_sck, kRestart);
    f("pins", "spi_miso", c.pins.spi_miso, kRestart);
    f("pins", "spi_mosi", c.pins.spi_mosi, kRestart);
    f("pins", "nfc_cs", c.pins.nfc_cs, kRestart);
    f("pins", "nfc_reset", c.pins.nfc_reset, kRestart);
    f("pins", "nfc_irq", c.pins.nfc_irq, kRestart);
    f("pins", "led", c.pins.led, kRestart);
    f("pins", "buzzer", c.pins.buzzer, kRestart);
    f("pins", "button_previous", c.pins.button_previous, kRestart);
    f("pins", "button_play", c.pins.button_play, kRestart);
    f("pins", "button_next", c.pins.button_next, kRestart);
    f("pins", "encoder_a", c.pins.encoder_a, kRestart);
    f("pins", "encoder_b", c.pins.encoder_b, kRestart);
    f("pins", "encoder_button", c.pins.encoder_button, kRestart);
    f("pins", "battery_adc", c.pins.battery_adc, kRestart);
    f("pins", "i2s_bclk", c.pins.i2s_bclk, kRestart);
    f("pins", "i2s_lrck", c.pins.i2s_lrck, kRestart);
    f("pins", "i2s_dout", c.pins.i2s_dout, kRestart);
    f("pins", "sd_cs", c.pins.sd_cs, kRestart);
    f("feedback", "led", c.feedback.led, kNoFlags);
    f("feedback", "led_count", c.feedback.led_count, kRestart);
    f("feedback", "led_brightness", c.feedback.led_brightness, kNoFlags);
    f("feedback", "buzzer", c.feedback.buzzer, kNoFlags);
    f("security", "admin_password", c.security.admin_password, kSecret);
    f("updates", "check", c.updates.check, kNoFlags);
    f("updates", "manifest_url", c.updates.manifest_url, kNoFlags);
    f("time", "ntp_server", c.time.ntp_server, kNoFlags);
    f("time", "timezone", c.time.timezone, kNoFlags);
}

// The defaults for a chip: bus pins follow the usual wiring for that chip's dev boards.
Config defaults(Chip chip);

struct Issue {
    std::string path;  // "mqtt.port"
    std::string message;
    bool warning = false;

    friend bool operator==(const Issue&, const Issue&) = default;
};

// Writes the configuration. With redact_secrets, set secrets read "********" and unset ones "".
void to_json(const Config& config, JsonObject out, bool redact_secrets);

// Merges a partial configuration into `config`. Fields not in `patch` keep their value; a
// secret sent back as "********" keeps its value. Type errors and unknown keys are reported
// and leave the field unchanged; with strict = false unknown keys are ignored silently.
std::vector<Issue> apply_json(Config& config, JsonObjectConst patch, bool strict = true);

// Range checks, formats and pin rules. Warnings do not block saving; errors do.
std::vector<Issue> validate(const Config& config, Chip chip, bool has_psram);

bool has_errors(std::span<const Issue> issues);

// True if a field marked kRestart differs.
bool restart_required(const Config& before, const Config& after);

// ---- Storage and migrations ----------------------------------------------------------------

// A migration upgrades the document from `from` to `from + 1`.
struct Migration {
    int from;
    void (*run)(JsonObject root);
};

// Every migration this firmware knows. Empty while the schema is at version 1.
std::span<const Migration> migrations();

enum class MigrateError : std::uint8_t {
    NoVersion,   // the document has no "v"
    TooNew,      // written by newer firmware
    MissingStep  // no migration from some version on the way
};

// Runs the migrations from the document's "v" up to `target` and updates "v".
// Returns the version the document started at.
std::expected<int, MigrateError> migrate(JsonObject root, std::span<const Migration> steps,
                                         int target);

struct LoadResult {
    Config config;
    bool fresh = false;     // nothing was stored: first boot or factory reset
    bool reset = false;     // the stored document was unusable and defaults were used
    bool migrated = false;  // the stored document was upgraded and should be saved again
    std::vector<Issue> issues;
};

// Reads a stored document. Never fails: anything unusable falls back to defaults and says why,
// and the caller keeps the original bytes as a backup.
LoadResult load(std::string_view stored, Chip chip);

// The document to store, with "v" and every secret in clear text.
std::string save(const Config& config);

}  // namespace tapwave::config
