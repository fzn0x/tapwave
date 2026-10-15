#include "core/mqtt_layout.hpp"

#include <ArduinoJson.h>

namespace tapwave::mqtt {

namespace {

constexpr std::string_view kProjectUrl = "https://github.com/fzn0x/tapwave";

std::string compact_mac(std::string_view mac) {
    std::string out;
    for (char c : mac) {
        if (c != ':') {
            out.push_back(c);
        }
    }
    return out;
}

std::string node_id(const Identity& identity) {
    return "tapwave_" + identity.id;
}

std::string unique_id(const Identity& identity, std::string_view key) {
    return "tapwave_" + compact_mac(identity.mac) + "_" + std::string(key);
}

std::string config_topic(std::string_view discovery_prefix, std::string_view component,
                         const Identity& identity, std::string_view key) {
    std::string out{discovery_prefix};
    out += '/';
    out += component;
    out += '/';
    out += node_id(identity);
    out += '/';
    out += key;
    out += "/config";
    return out;
}

void write_device(JsonObject device, const Identity& identity) {
    device["identifiers"].to<JsonArray>().add("tapwave_" + compact_mac(identity.mac));
    JsonArray connection = device["connections"].to<JsonArray>().add<JsonArray>();
    connection.add("mac");
    connection.add(identity.mac);
    device["name"] = identity.name;
    device["manufacturer"] = "Tapwave";
    device["model"] = identity.model;
    device["hw_version"] = identity.hw_version;
    device["sw_version"] = identity.sw_version;
    if (!identity.configuration_url.empty()) {
        device["configuration_url"] = identity.configuration_url;
    }
}

// An entity: shared fields plus whatever the component needs.
struct Entity {
    std::string_view component;
    std::string_view key;
    std::string_view name;
    void (*fill)(JsonObject config, const Topics& topics);
};

void common_fields(JsonObject config, const Entity& entity, const Identity& identity,
                   const Topics& topics) {
    config["name"] = entity.name;
    config["unique_id"] = unique_id(identity, entity.key);
    config["availability_topic"] = topics.status();
    config["payload_available"] = "online";
    config["payload_not_available"] = "offline";
    write_device(config["device"].to<JsonObject>(), identity);
    JsonObject origin = config["origin"].to<JsonObject>();
    origin["name"] = "Tapwave";
    origin["sw_version"] = identity.sw_version;
    origin["support_url"] = kProjectUrl;
}

void diagnostic_sensor(JsonObject config, const Topics& topics, std::string_view field) {
    config["state_topic"] = topics.diagnostics();
    config["value_template"] = "{{ value_json." + std::string(field) + " }}";
    config["entity_category"] = "diagnostic";
}

const Entity kEntities[] = {
    {"sensor", "last_tag", "Last tag",
     [](JsonObject c, const Topics& t) {
         c["state_topic"] = t.tag_last();
         c["value_template"] = "{{ value_json.id }}";
         c["json_attributes_topic"] = t.tag_last();
         c["icon"] = "mdi:nfc-variant";
     }},
    {"binary_sensor", "tag_present", "Tag present",
     [](JsonObject c, const Topics& t) {
         c["state_topic"] = t.tag_present();
         c["payload_on"] = "ON";
         c["payload_off"] = "OFF";
         c["icon"] = "mdi:nfc";
     }},
    {"event", "tag_event", "Tag",
     [](JsonObject c, const Topics& t) {
         c["state_topic"] = t.tag_event();
         JsonArray types = c["event_types"].to<JsonArray>();
         types.add("scanned");
         types.add("removed");
         types.add("music");
         c["icon"] = "mdi:gesture-tap";
     }},
    {"switch", "buzzer", "Buzzer",
     [](JsonObject c, const Topics& t) {
         c["state_topic"] = t.buzzer_state();
         c["command_topic"] = t.buzzer_set();
         c["payload_on"] = "ON";
         c["payload_off"] = "OFF";
         c["entity_category"] = "config";
         c["icon"] = "mdi:volume-high";
     }},
    {"switch", "led", "Status LED",
     [](JsonObject c, const Topics& t) {
         c["state_topic"] = t.led_state();
         c["command_topic"] = t.led_set();
         c["payload_on"] = "ON";
         c["payload_off"] = "OFF";
         c["entity_category"] = "config";
         c["icon"] = "mdi:led-on";
     }},
    {"button", "restart", "Restart",
     [](JsonObject c, const Topics& t) {
         c["command_topic"] = t.cmd_restart();
         c["payload_press"] = "PRESS";
         c["device_class"] = "restart";
         c["entity_category"] = "config";
     }},
    {"button", "identify", "Identify",
     [](JsonObject c, const Topics& t) {
         c["command_topic"] = t.cmd_identify();
         c["payload_press"] = "PRESS";
         c["device_class"] = "identify";
         c["entity_category"] = "config";
     }},
    {"update", "firmware", "Firmware",
     [](JsonObject c, const Topics& t) {
         c["state_topic"] = t.update_state();
         c["command_topic"] = t.update_install();
         c["payload_install"] = "install";
         c["device_class"] = "firmware";
         c["entity_category"] = "config";
     }},
    {"sensor", "wifi_signal", "Wi-Fi signal",
     [](JsonObject c, const Topics& t) {
         diagnostic_sensor(c, t, "rssi");
         c["unit_of_measurement"] = "dBm";
         c["device_class"] = "signal_strength";
         c["state_class"] = "measurement";
     }},
    {"sensor", "reader", "Reader",
     [](JsonObject c, const Topics& t) {
         diagnostic_sensor(c, t, "reader");
         c["icon"] = "mdi:nfc-search-variant";
     }},
    {"sensor", "ip", "IP address",
     [](JsonObject c, const Topics& t) {
         diagnostic_sensor(c, t, "ip");
         c["icon"] = "mdi:ip-network";
     }},
    {"sensor", "uptime", "Uptime",
     [](JsonObject c, const Topics& t) {
         diagnostic_sensor(c, t, "uptime");
         c["unit_of_measurement"] = "s";
         c["device_class"] = "duration";
         c["state_class"] = "total_increasing";
     }},
};

constexpr std::string_view kTagComponent = "tag";
constexpr std::string_view kTagKey = "scanner";

std::string serialize(const JsonDocument& doc) {
    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace

std::string expand_prefix(std::string_view pattern, std::string_view id) {
    std::string out;
    std::size_t pos = 0;
    while (pos < pattern.size()) {
        const std::size_t found = pattern.find("{id}", pos);
        if (found == std::string_view::npos) {
            out.append(pattern.substr(pos));
            break;
        }
        out.append(pattern.substr(pos, found - pos));
        out.append(id);
        pos = found + 4;
    }
    return out;
}

std::string Topics::at(std::string_view suffix) const {
    std::string out = prefix_;
    out += '/';
    out += suffix;
    return out;
}

std::vector<std::string> Topics::subscriptions() const {
    return {buzzer_set(), led_set(), at("cmd/#"), update_install()};
}

Inbound route(const Topics& topics, std::string_view discovery_prefix, std::string_view topic) {
    if (topic == topics.buzzer_set()) {
        return Inbound::BuzzerSet;
    }
    if (topic == topics.led_set()) {
        return Inbound::LedSet;
    }
    if (topic == topics.cmd_restart()) {
        return Inbound::Restart;
    }
    if (topic == topics.cmd_identify()) {
        return Inbound::Identify;
    }
    if (topic == topics.cmd_feedback()) {
        return Inbound::Feedback;
    }
    if (topic == topics.cmd_rtttl()) {
        return Inbound::Rtttl;
    }
    if (topic == topics.update_install()) {
        return Inbound::UpdateInstall;
    }
    if (topic.size() == discovery_prefix.size() + 7 && topic.starts_with(discovery_prefix) &&
        topic.substr(discovery_prefix.size()) == "/status") {
        return Inbound::HomeAssistantStatus;
    }
    return Inbound::Unknown;
}

std::vector<Message> discovery(const Identity& identity, const Topics& topics,
                               std::string_view discovery_prefix) {
    std::vector<Message> out;

    // The tag scanner has its own schema: no name, unique ID or availability.
    {
        JsonDocument doc;
        doc["topic"] = topics.tag_scanned();
        doc["value_template"] = "{{ value_json.id }}";
        write_device(doc["device"].to<JsonObject>(), identity);
        out.push_back({config_topic(discovery_prefix, kTagComponent, identity, kTagKey),
                       serialize(doc), true});
    }

    for (const Entity& entity : kEntities) {
        JsonDocument doc;
        JsonObject config = doc.to<JsonObject>();
        common_fields(config, entity, identity, topics);
        entity.fill(config, topics);
        out.push_back({config_topic(discovery_prefix, entity.component, identity, entity.key),
                       serialize(doc), true});
    }
    return out;
}

std::vector<Message> discovery_removal(const Identity& identity,
                                       std::string_view discovery_prefix) {
    std::vector<Message> out;
    out.push_back({config_topic(discovery_prefix, kTagComponent, identity, kTagKey), "", true});
    for (const Entity& entity : kEntities) {
        out.push_back(
            {config_topic(discovery_prefix, entity.component, identity, entity.key), "", true});
    }
    return out;
}

std::string diagnostics_json(const Diagnostics& d) {
    JsonDocument doc;
    doc["rssi"] = d.rssi;
    doc["ip"] = d.ip;
    doc["uptime"] = d.uptime_s;
    doc["heap"] = d.free_heap;
    doc["reader"] = d.reader;
    doc["version"] = d.version;
    return serialize(doc);
}

std::string update_state_json(const UpdateState& u) {
    JsonDocument doc;
    doc["installed_version"] = u.installed_version;
    if (u.latest_version.empty()) {
        doc["latest_version"] = u.installed_version;
    } else {
        doc["latest_version"] = u.latest_version;
    }
    doc["title"] = "Tapwave";
    if (!u.release_url.empty()) {
        doc["release_url"] = u.release_url;
    }
    doc["in_progress"] = u.in_progress;
    if (u.percentage) {
        doc["update_percentage"] = *u.percentage;
    } else {
        doc["update_percentage"] = nullptr;
    }
    return serialize(doc);
}

std::string scanned_event_json(const Scan& scan) {
    JsonDocument doc;
    doc["event_type"] = scan.kind == ScanKind::Music ? "music" : "scanned";
    doc["id"] = scan.id();
    doc["uid"] = scan.uid;
    doc["kind"] = to_string(scan.kind);
    if (scan.music_uri) {
        doc["music_uri"] = *scan.music_uri;
    }
    if (scan.uri) {
        doc["uri"] = *scan.uri;
    }
    if (scan.text) {
        doc["text"] = *scan.text;
    }
    return serialize(doc);
}

std::string removed_event_json(std::string_view uid) {
    JsonDocument doc;
    doc["event_type"] = "removed";
    doc["uid"] = uid;
    return serialize(doc);
}

}  // namespace tapwave::mqtt
