#include "doctest_main.hpp"

#include <ArduinoJson.h>

#include <algorithm>

#include "core/mqtt_layout.hpp"

using namespace tapwave;
using namespace tapwave::mqtt;

namespace {

Identity identity() {
    return {"a1b2c3", "aa:bb:cc:a1:b2:c3",   "Kids box", "Tapwave (ESP32)", "ESP32 rev 3.1",
            "0.1.0",  "http://192.168.1.50/"};
}

const Topics kTopics{expand_prefix("tapwave/{id}", "a1b2c3")};

JsonDocument parse(std::string_view json) {
    JsonDocument doc;
    REQUIRE_FALSE(deserializeJson(doc, json));
    return doc;
}

const Message& find(const std::vector<Message>& messages, std::string_view topic) {
    const auto it = std::ranges::find(messages, topic, &Message::topic);
    REQUIRE_MESSAGE(it != messages.end(), topic);
    return *it;
}

}  // namespace

TEST_CASE("the topic prefix expands the device ID") {
    CHECK(expand_prefix("tapwave/{id}", "a1b2c3") == "tapwave/a1b2c3");
    CHECK(expand_prefix("home/kids-box", "a1b2c3") == "home/kids-box");
    CHECK(expand_prefix("{id}/{id}", "x") == "x/x");
}

TEST_CASE("topics follow the documented tree") {
    CHECK(kTopics.status() == "tapwave/a1b2c3/status");
    CHECK(kTopics.tag_scanned() == "tapwave/a1b2c3/tag/scanned");
    CHECK(kTopics.tag_last() == "tapwave/a1b2c3/tag/last");
    CHECK(kTopics.tag_present() == "tapwave/a1b2c3/tag/present");
    CHECK(kTopics.tag_event() == "tapwave/a1b2c3/tag/event");
    CHECK(kTopics.diagnostics() == "tapwave/a1b2c3/diagnostics");
    CHECK(kTopics.buzzer_set() == "tapwave/a1b2c3/buzzer/set");
    CHECK(kTopics.cmd_rtttl() == "tapwave/a1b2c3/cmd/rtttl");
    CHECK(kTopics.update_install() == "tapwave/a1b2c3/update/install");
    const auto subscriptions = kTopics.subscriptions();
    CHECK(std::ranges::find(subscriptions, "tapwave/a1b2c3/cmd/#") != subscriptions.end());
}

TEST_CASE("inbound topics are routed") {
    CHECK(route(kTopics, "homeassistant", "tapwave/a1b2c3/buzzer/set") == Inbound::BuzzerSet);
    CHECK(route(kTopics, "homeassistant", "tapwave/a1b2c3/led/set") == Inbound::LedSet);
    CHECK(route(kTopics, "homeassistant", "tapwave/a1b2c3/cmd/restart") == Inbound::Restart);
    CHECK(route(kTopics, "homeassistant", "tapwave/a1b2c3/cmd/identify") == Inbound::Identify);
    CHECK(route(kTopics, "homeassistant", "tapwave/a1b2c3/cmd/feedback") == Inbound::Feedback);
    CHECK(route(kTopics, "homeassistant", "tapwave/a1b2c3/cmd/rtttl") == Inbound::Rtttl);
    CHECK(route(kTopics, "homeassistant", "tapwave/a1b2c3/update/install") ==
          Inbound::UpdateInstall);
    CHECK(route(kTopics, "homeassistant", "homeassistant/status") == Inbound::HomeAssistantStatus);
    CHECK(route(kTopics, "ha", "ha/status") == Inbound::HomeAssistantStatus);
    CHECK(route(kTopics, "homeassistant", "homeassistant/statuses") == Inbound::Unknown);
    CHECK(route(kTopics, "homeassistant", "tapwave/a1b2c3/cmd/dance") == Inbound::Unknown);
}

TEST_CASE("the tag scanner makes Home Assistant read the tag ID from scans") {
    const auto messages = discovery(identity(), kTopics, "homeassistant");
    const Message& tag = find(messages, "homeassistant/tag/tapwave_a1b2c3/scanner/config");
    CHECK(tag.retain);
    const JsonDocument doc = parse(tag.payload);
    CHECK(doc["topic"] == "tapwave/a1b2c3/tag/scanned");
    CHECK(doc["value_template"] == "{{ value_json.id }}");
    CHECK(doc["device"]["identifiers"][0] == "tapwave_aabbcca1b2c3");
    CHECK_FALSE(doc["unique_id"].is<const char*>());
}

TEST_CASE("every entity carries the device, origin and availability") {
    const auto messages = discovery(identity(), kTopics, "homeassistant");
    CHECK(messages.size() == 13);
    for (const Message& m : messages) {
        CHECK(m.retain);
        const JsonDocument doc = parse(m.payload);
        CHECK(doc["device"]["name"] == "Kids box");
        CHECK(doc["device"]["connections"][0][0] == "mac");
        CHECK(doc["device"]["connections"][0][1] == "aa:bb:cc:a1:b2:c3");
        CHECK(doc["device"]["sw_version"] == "0.1.0");
        CHECK(doc["device"]["configuration_url"] == "http://192.168.1.50/");
        if (m.topic.find("/tag/") != std::string::npos) {
            continue;
        }
        CHECK(doc["availability_topic"] == "tapwave/a1b2c3/status");
        CHECK(doc["origin"]["name"] == "Tapwave");
        CHECK(std::string(doc["unique_id"].as<const char*>()).starts_with("tapwave_aabbcca1b2c3_"));
    }
}

TEST_CASE("entity payloads") {
    const auto messages = discovery(identity(), kTopics, "homeassistant");

    const JsonDocument last =
        parse(find(messages, "homeassistant/sensor/tapwave_a1b2c3/last_tag/config").payload);
    CHECK(last["state_topic"] == "tapwave/a1b2c3/tag/last");
    CHECK(last["json_attributes_topic"] == "tapwave/a1b2c3/tag/last");
    CHECK(last["value_template"] == "{{ value_json.id }}");

    const JsonDocument event =
        parse(find(messages, "homeassistant/event/tapwave_a1b2c3/tag_event/config").payload);
    CHECK(event["event_types"].size() == 3);
    CHECK(event["event_types"][2] == "music");

    const JsonDocument buzzer =
        parse(find(messages, "homeassistant/switch/tapwave_a1b2c3/buzzer/config").payload);
    CHECK(buzzer["command_topic"] == "tapwave/a1b2c3/buzzer/set");
    CHECK(buzzer["entity_category"] == "config");

    const JsonDocument restart =
        parse(find(messages, "homeassistant/button/tapwave_a1b2c3/restart/config").payload);
    CHECK(restart["device_class"] == "restart");

    const JsonDocument update =
        parse(find(messages, "homeassistant/update/tapwave_a1b2c3/firmware/config").payload);
    CHECK(update["payload_install"] == "install");
    CHECK(update["state_topic"] == "tapwave/a1b2c3/update/state");

    const JsonDocument rssi =
        parse(find(messages, "homeassistant/sensor/tapwave_a1b2c3/wifi_signal/config").payload);
    CHECK(rssi["value_template"] == "{{ value_json.rssi }}");
    CHECK(rssi["entity_category"] == "diagnostic");
    CHECK(rssi["unit_of_measurement"] == "dBm");
}

TEST_CASE("discovery uses the configured prefix") {
    const auto messages = discovery(identity(), kTopics, "ha-test");
    CHECK(std::ranges::all_of(messages,
                              [](const Message& m) { return m.topic.starts_with("ha-test/"); }));
}

TEST_CASE("removal empties every discovery topic") {
    const auto added = discovery(identity(), kTopics, "homeassistant");
    const auto removed = discovery_removal(identity(), "homeassistant");
    REQUIRE(removed.size() == added.size());
    for (std::size_t i = 0; i < added.size(); ++i) {
        CHECK(removed[i].topic == added[i].topic);
        CHECK(removed[i].payload.empty());
        CHECK(removed[i].retain);
    }
}

TEST_CASE("diagnostics JSON") {
    const JsonDocument doc =
        parse(diagnostics_json({-61, "192.168.1.50", 3600, 123456, "PN532 1.6 (i2c)", "0.1.0"}));
    CHECK(doc["rssi"] == -61);
    CHECK(doc["ip"] == "192.168.1.50");
    CHECK(doc["uptime"] == 3600);
    CHECK(doc["heap"] == 123456);
    CHECK(doc["reader"] == "PN532 1.6 (i2c)");
    CHECK(doc["version"] == "0.1.0");
}

TEST_CASE("update entity state") {
    const JsonDocument idle = parse(update_state_json({"0.1.0", "", "", false, std::nullopt}));
    CHECK(idle["installed_version"] == "0.1.0");
    CHECK(idle["latest_version"] == "0.1.0");  // nothing known yet: up to date
    CHECK(idle["in_progress"] == false);
    CHECK(idle["update_percentage"].isNull());
    CHECK_FALSE(idle["release_url"].is<const char*>());

    const JsonDocument running = parse(update_state_json(
        {"0.1.0", "0.2.0", "https://github.com/fzn0x/tapwave/releases/tag/v0.2.0", true, 42}));
    CHECK(running["latest_version"] == "0.2.0");
    CHECK(running["update_percentage"] == 42);
    CHECK(running["title"] == "Tapwave");
}

TEST_CASE("tag events") {
    const Scan ha =
        classify("04-A2", "NTAG215", {ndef::uri_record("https://www.home-assistant.io/tag/abc")});
    const JsonDocument scanned = parse(scanned_event_json(ha));
    CHECK(scanned["event_type"] == "scanned");
    CHECK(scanned["id"] == "abc");
    CHECK(scanned["kind"] == "ha_tag");

    const Scan music = classify(
        "04-A2", "NTAG215", {ndef::uri_record("spotify://album/x"), ndef::text_record("Album")});
    const JsonDocument m = parse(scanned_event_json(music));
    CHECK(m["event_type"] == "music");
    CHECK(m["music_uri"] == "spotify://album/x");
    CHECK(m["text"] == "Album");
    CHECK(m["id"] == "04-A2");

    const JsonDocument removed = parse(removed_event_json("04-A2"));
    CHECK(removed["event_type"] == "removed");
    CHECK(removed["uid"] == "04-A2");
}
