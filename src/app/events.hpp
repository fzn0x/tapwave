// Everything that happens on the device, as events for the loop task (ARCHITECTURE.md
// section 5).
#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "core/config.hpp"
#include "core/mqtt_layout.hpp"
#include "core/scan.hpp"

namespace tapwave::app {

struct TagScanned {
    Scan scan;
};

struct TagRemoved {
    std::string uid;
};

struct ReaderChanged {};  // ScanService::status() has news

struct WifiChanged {};  // WifiManager::status() has news

struct MqttConnection {
    bool connected = false;
};

struct MqttCommand {
    mqtt::Inbound what = mqtt::Inbound::Unknown;
    std::string payload;
};

struct ConfigChanged {
    config::Config before;
    config::Config after;
};

enum class FeedbackKind : std::uint8_t { Ok, Error, Identify };

struct Feedback {
    FeedbackKind kind = FeedbackKind::Ok;
};

struct PlayMelody {
    std::string melody;
};

struct RestartRequested {
    std::uint32_t delay_ms = 1000;
};

struct UpdateChanged {};  // Updater::status() has news

using Event =
    std::variant<TagScanned, TagRemoved, ReaderChanged, WifiChanged, MqttConnection, MqttCommand,
                 ConfigChanged, Feedback, PlayMelody, RestartRequested, UpdateChanged>;

}  // namespace tapwave::app
