// MQTT topics and Home Assistant discovery payloads (ARCHITECTURE.md section 12).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/scan.hpp"

namespace tapwave::mqtt {

// Who this device is, for the discovery "device" block and unique IDs.
struct Identity {
    std::string id;                 // last six hex digits of the MAC: "a1b2c3"
    std::string mac;                // "aa:bb:cc:a1:b2:c3"
    std::string name;               // the device name from the configuration
    std::string model;              // "Tapwave (ESP32)"
    std::string hw_version;         // "ESP32 rev 3.1"
    std::string sw_version;         // firmware version
    std::string configuration_url;  // "http://192.168.1.50/"
};

// Replaces "{id}" in the configured topic prefix.
std::string expand_prefix(std::string_view pattern, std::string_view id);

class Topics {
public:
    explicit Topics(std::string prefix) : prefix_(std::move(prefix)) {}

    const std::string& prefix() const { return prefix_; }
    std::string status() const { return at("status"); }
    std::string tag_scanned() const { return at("tag/scanned"); }
    std::string tag_last() const { return at("tag/last"); }
    std::string tag_present() const { return at("tag/present"); }
    std::string tag_event() const { return at("tag/event"); }
    std::string diagnostics() const { return at("diagnostics"); }
    std::string buzzer_state() const { return at("buzzer/state"); }
    std::string buzzer_set() const { return at("buzzer/set"); }
    std::string led_state() const { return at("led/state"); }
    std::string led_set() const { return at("led/set"); }
    std::string cmd_restart() const { return at("cmd/restart"); }
    std::string cmd_identify() const { return at("cmd/identify"); }
    std::string cmd_feedback() const { return at("cmd/feedback"); }
    std::string cmd_rtttl() const { return at("cmd/rtttl"); }
    std::string update_state() const { return at("update/state"); }
    std::string update_install() const { return at("update/install"); }

    // Topic filters to subscribe to for commands.
    std::vector<std::string> subscriptions() const;

private:
    std::string at(std::string_view suffix) const;
    std::string prefix_;
};

enum class Inbound : std::uint8_t {
    BuzzerSet,
    LedSet,
    Restart,
    Identify,
    Feedback,
    Rtttl,
    UpdateInstall,
    HomeAssistantStatus,  // "<discovery prefix>/status": republish discovery on "online"
    Unknown,
};

Inbound route(const Topics& topics, std::string_view discovery_prefix, std::string_view topic);

struct Message {
    std::string topic;
    std::string payload;
    bool retain = true;
};

// Every discovery message, retained. The tag scanner makes Home Assistant fire tag_scanned.
std::vector<Message> discovery(const Identity& identity, const Topics& topics,
                               std::string_view discovery_prefix);

// Empty retained payloads on the same topics, which remove the entities.
std::vector<Message> discovery_removal(const Identity& identity, std::string_view discovery_prefix);

struct Diagnostics {
    int rssi = 0;
    std::string ip;
    std::uint32_t uptime_s = 0;
    std::uint32_t free_heap = 0;
    std::string reader;  // "PN532 1.6 (i2c)" or "none"
    std::string version;
};

std::string diagnostics_json(const Diagnostics& d);

struct UpdateState {
    std::string installed_version;
    std::string latest_version;  // empty until a manifest was read
    std::string release_url;
    bool in_progress = false;
    std::optional<int> percentage;
};

// The JSON state of the Home Assistant update entity.
std::string update_state_json(const UpdateState& u);

// Payloads for the tag event entity.
std::string scanned_event_json(const Scan& scan);
std::string removed_event_json(std::string_view uid);

}  // namespace tapwave::mqtt
