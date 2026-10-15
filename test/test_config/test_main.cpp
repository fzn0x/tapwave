#include "doctest_main.hpp"

#include <algorithm>

#include "core/config.hpp"

using namespace tapwave;
using namespace tapwave::config;

namespace {

JsonDocument parse(std::string_view json) {
    JsonDocument doc;
    REQUIRE_FALSE(deserializeJson(doc, json));
    return doc;
}

bool has_issue(const std::vector<Issue>& issues, std::string_view path, bool warning = false) {
    return std::ranges::any_of(
        issues, [&](const Issue& i) { return i.path == path && i.warning == warning; });
}

Config valid_esp32() {
    return defaults(Chip::Esp32);
}

}  // namespace

TEST_CASE("defaults follow the usual dev board wiring per chip") {
    const Config esp32 = defaults(Chip::Esp32);
    CHECK(esp32.pins.i2c_sda == 21);
    CHECK(esp32.pins.i2c_scl == 22);
    CHECK(esp32.pins.nfc_cs == 5);
    CHECK(esp32.pins.led == -1);
    CHECK(esp32.modes.home_assistant);
    CHECK_FALSE(esp32.modes.player);
    CHECK(esp32.mqtt.topic_prefix == "tapwave/{id}");

    const Config s3 = defaults(Chip::Esp32S3);
    CHECK(s3.pins.i2c_sda == 8);
    CHECK(s3.pins.spi_mosi == 11);
}

TEST_CASE("the defaults pass validation on both chips") {
    CHECK(validate(defaults(Chip::Esp32), Chip::Esp32, true).empty());
    CHECK(validate(defaults(Chip::Esp32S3), Chip::Esp32S3, true).empty());
}

TEST_CASE("to_json redacts secrets that are set") {
    Config c = valid_esp32();
    c.wifi.password = "hunter22";
    c.mqtt.password = "";
    JsonDocument doc;
    to_json(c, doc.to<JsonObject>(), true);
    CHECK(doc["wifi"]["password"] == "********");
    CHECK(doc["mqtt"]["password"] == "");
    CHECK(doc["security"]["admin_password"] == "");
    CHECK(doc["device"]["name"] == "Tapwave");
    CHECK(doc["pins"]["i2c_sda"] == 21);
    CHECK(doc["modes"]["home_assistant"] == true);

    JsonDocument clear;
    to_json(c, clear.to<JsonObject>(), false);
    CHECK(clear["wifi"]["password"] == "hunter22");
}

TEST_CASE("apply_json merges a partial update") {
    Config c = valid_esp32();
    const auto patch = parse(R"({"mqtt": {"enabled": true, "host": "broker.lan", "port": 8883},
                                  "pins": {"led": 13}})");
    const auto issues = apply_json(c, patch.as<JsonObjectConst>());
    CHECK(issues.empty());
    CHECK(c.mqtt.enabled);
    CHECK(c.mqtt.host == "broker.lan");
    CHECK(c.mqtt.port == 8883);
    CHECK(c.pins.led == 13);
    CHECK(c.pins.i2c_sda == 21);  // untouched
}

TEST_CASE("sending the redacted placeholder keeps a secret") {
    Config c = valid_esp32();
    c.wifi.password = "hunter22";
    apply_json(c, parse(R"({"wifi": {"password": "********"}})").as<JsonObjectConst>());
    CHECK(c.wifi.password == "hunter22");
    apply_json(c, parse(R"({"wifi": {"password": ""}})").as<JsonObjectConst>());
    CHECK(c.wifi.password.empty());
}

TEST_CASE("apply_json reports wrong types and unknown keys") {
    Config c = valid_esp32();
    const auto issues =
        apply_json(c, parse(R"({"mqtt": {"port": "1883", "tls": 1, "host": 5, "colour": "red"},
                     "pins": {"led": 1.5}, "lights": {}, "modes": true})")
                          .as<JsonObjectConst>());
    CHECK(has_issue(issues, "mqtt.port"));
    CHECK(has_issue(issues, "mqtt.tls"));
    CHECK(has_issue(issues, "mqtt.host"));
    CHECK(has_issue(issues, "mqtt.colour"));
    CHECK(has_issue(issues, "pins.led"));
    CHECK(has_issue(issues, "lights"));
    CHECK(has_issue(issues, "modes"));
    CHECK(c.mqtt.port == 1883);
    CHECK(c.pins.led == -1);
}

TEST_CASE("non-strict apply ignores unknown keys") {
    Config c = valid_esp32();
    const auto issues = apply_json(
        c, parse(R"({"future": {"x": 1}, "device": {"name": "Box"}})").as<JsonObjectConst>(),
        false);
    CHECK(issues.empty());
    CHECK(c.device.name == "Box");
}

TEST_CASE("validate catches out-of-range and malformed values") {
    Config c = valid_esp32();
    c.device.name = "";
    c.device.hostname = "-kids-box";
    c.wifi.password = "short";
    c.wifi.ap_password = "1234";
    c.mqtt.enabled = true;
    c.mqtt.host = "";
    c.mqtt.port = 70000;
    c.mqtt.topic_prefix = "tapwave/#";
    c.mqtt.discovery_prefix = "/homeassistant";
    c.nfc.driver = "PN532";
    c.nfc.poll_interval_ms = 10;
    c.nfc.removal_polls = 0;
    c.feedback.led_count = 0;
    c.feedback.led_brightness = 300;
    c.updates.manifest_url = "ftp://example.com";
    c.time.ntp_server = "";
    const auto issues = validate(c, Chip::Esp32, true);
    for (std::string_view path :
         {"device.name", "device.hostname", "wifi.password", "wifi.ap_password", "mqtt.host",
          "mqtt.port", "mqtt.topic_prefix", "mqtt.discovery_prefix", "nfc.driver",
          "nfc.poll_interval_ms", "nfc.removal_polls", "feedback.led_count",
          "feedback.led_brightness", "updates.manifest_url", "time.ntp_server"}) {
        CHECK_MESSAGE(has_issue(issues, path), path);
    }
    CHECK(has_errors(issues));
}

TEST_CASE("Wi-Fi passwords may be a 64-digit hex key") {
    Config c = valid_esp32();
    c.wifi.password = std::string(64, 'a');
    CHECK(validate(c, Chip::Esp32, true).empty());
    c.wifi.password = std::string(64, 'z');
    CHECK(has_issue(validate(c, Chip::Esp32, true), "wifi.password"));
}

TEST_CASE("a pin used twice is refused") {
    Config c = valid_esp32();
    c.pins.led = 21;  // already I2C SDA
    const auto issues = validate(c, Chip::Esp32, true);
    REQUIRE(has_issue(issues, "pins.led"));
    const auto clash = std::ranges::find(issues, std::string("pins.led"), &Issue::path);
    CHECK(clash->message.find("pins.i2c_sda") != std::string::npos);
}

TEST_CASE("pin rules are applied per function") {
    Config c = valid_esp32();
    c.pins.led = 34;          // input only
    c.pins.button_play = 35;  // input only, fine for a button but needs a pull-up
    c.pins.buzzer = 7;        // flash
    c.pins.battery_adc = 4;   // ADC2
    const auto issues = validate(c, Chip::Esp32, true);
    CHECK(has_issue(issues, "pins.led"));
    CHECK(has_issue(issues, "pins.button_play", true));
    CHECK(has_issue(issues, "pins.buzzer"));
    CHECK(has_issue(issues, "pins.battery_adc"));
}

TEST_CASE("warnings alone do not block saving") {
    const std::vector<Issue> warnings{{"pins.led", "strapping pin", true}};
    CHECK_FALSE(has_errors(warnings));
}

TEST_CASE("restart is needed only for restart fields") {
    const Config before = valid_esp32();
    Config after = before;
    after.mqtt.host = "broker.lan";
    after.feedback.led_brightness = 10;
    CHECK_FALSE(restart_required(before, after));
    after.pins.led = 13;
    CHECK(restart_required(before, after));
    Config renamed = before;
    renamed.device.hostname = "kids-box";
    CHECK(restart_required(before, renamed));
}

TEST_CASE("save and load round-trip every field") {
    Config c = defaults(Chip::Esp32S3);
    c.device.name = "Kids box";
    c.wifi.ssid = "Home";
    c.wifi.password = "correct horse";
    c.mqtt.enabled = true;
    c.mqtt.host = "10.0.0.2";
    c.pins.led = 48;
    c.time.ntp_server = "time.cloudflare.com";
    const std::string stored = save(c);
    CHECK(stored.starts_with(R"({"v":1,)"));
    const LoadResult loaded = load(stored, Chip::Esp32S3);
    CHECK_FALSE(loaded.fresh);
    CHECK_FALSE(loaded.reset);
    CHECK_FALSE(loaded.migrated);
    CHECK(loaded.issues.empty());
    CHECK(loaded.config == c);
}

TEST_CASE("nothing stored means a fresh device with defaults") {
    const LoadResult loaded = load("", Chip::Esp32);
    CHECK(loaded.fresh);
    CHECK(loaded.config == defaults(Chip::Esp32));
}

TEST_CASE("unreadable or foreign documents fall back to defaults and say why") {
    for (std::string_view stored : {"{not json", "[1,2]", R"({"device": {}})", R"({"v": 99})"}) {
        const LoadResult loaded = load(stored, Chip::Esp32);
        CHECK_MESSAGE(loaded.reset, stored);
        CHECK(loaded.config == defaults(Chip::Esp32));
        REQUIRE(loaded.issues.size() == 1);
        CHECK_FALSE(loaded.issues.front().message.empty());
    }
    CHECK(load(R"({"v": 99})", Chip::Esp32).issues.front().message.find("newer") !=
          std::string::npos);
}

TEST_CASE("a stored field with the wrong type keeps its default") {
    const LoadResult loaded = load(R"({"v":1,"mqtt":{"port":"x","host":"b"}})", Chip::Esp32);
    CHECK(loaded.config.mqtt.port == 1883);
    CHECK(loaded.config.mqtt.host == "b");
    CHECK(has_issue(loaded.issues, "mqtt.port"));
}

TEST_CASE("migrations run in order and bump the version") {
    static constexpr Migration steps[] = {
        {1, [](JsonObject root) { root["device"]["name"] = "from v1"; }},
        {2,
         [](JsonObject root) {
             // v3 renamed device.name to device.label
             root["device"]["label"] = root["device"]["name"];
             root["device"].remove("name");
         }},
    };
    JsonDocument doc = parse(R"({"v": 1, "device": {}})");
    const auto from = migrate(doc.as<JsonObject>(), steps, 3);
    REQUIRE(from);
    CHECK(*from == 1);
    CHECK(doc["v"] == 3);
    CHECK(doc["device"]["label"] == "from v1");
    CHECK_FALSE(doc["device"]["name"].is<const char*>());
}

TEST_CASE("migration errors") {
    static constexpr Migration only_one[] = {{1, [](JsonObject) {}}};
    JsonDocument no_version = parse(R"({"device": {}})");
    CHECK(migrate(no_version.as<JsonObject>(), only_one, 3).error() == MigrateError::NoVersion);
    JsonDocument gap = parse(R"({"v": 1})");
    CHECK(migrate(gap.as<JsonObject>(), only_one, 3).error() == MigrateError::MissingStep);
    JsonDocument newer = parse(R"({"v": 4})");
    CHECK(migrate(newer.as<JsonObject>(), only_one, 3).error() == MigrateError::TooNew);
    JsonDocument current = parse(R"({"v": 3})");
    CHECK(migrate(current.as<JsonObject>(), only_one, 3) == 3);
}

TEST_CASE("this firmware's migration table reaches the current schema") {
    JsonDocument doc = parse(R"({"v": 1})");
    CHECK(migrate(doc.as<JsonObject>(), migrations(), kSchemaVersion));
    CHECK(doc["v"] == kSchemaVersion);
}
