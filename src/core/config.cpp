#include "core/config.hpp"

#include <algorithm>
#include <array>
#include <type_traits>
#include <utility>

namespace tapwave::config {

namespace {

JsonObject section_of(JsonObject root, const char* name) {
    // Use the member proxy directly: copying a missing member into a JsonVariant detaches it.
    if (root[name].is<JsonObject>()) {
        return root[name].as<JsonObject>();
    }
    return root[name].to<JsonObject>();
}

bool is_known_field(std::string_view section, std::string_view key) {
    bool found = false;
    const Config probe;
    for_each_field(probe, [&](const char* s, const char* k, const auto&, unsigned) {
        found = found || (section == s && key == k);
    });
    return found;
}

bool is_known_section(std::string_view section) {
    bool found = false;
    const Config probe;
    for_each_field(probe, [&](const char* s, const char*, const auto&, unsigned) {
        found = found || section == s;
    });
    return found;
}

std::string path_of(std::string_view section, std::string_view key) {
    std::string out{section};
    out.push_back('.');
    out.append(key);
    return out;
}

// ---- format checks ----

bool valid_hostname(std::string_view name) {
    if (name.empty() || name.size() > 63 || name.front() == '-' || name.back() == '-') {
        return false;
    }
    return std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '-';
    });
}

bool valid_topic_prefix(std::string_view prefix) {
    if (prefix.empty() || prefix.size() > 64 || prefix.front() == '/' || prefix.back() == '/') {
        return false;
    }
    return std::ranges::none_of(prefix, [](char c) {
        return c == '#' || c == '+' || static_cast<unsigned char>(c) < 0x20;
    });
}

bool has_control_characters(std::string_view text) {
    return std::ranges::any_of(text, [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

bool all_hex(std::string_view text) {
    return std::ranges::all_of(text, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}

struct PinFunction {
    std::string_view key;
    PinUse use;
};

constexpr auto kPinFunctions = std::to_array<PinFunction>({
    {"i2c_sda", PinUse::Bidirectional},
    {"i2c_scl", PinUse::Bidirectional},
    {"spi_sck", PinUse::Output},
    {"spi_miso", PinUse::Input},
    {"spi_mosi", PinUse::Output},
    {"nfc_cs", PinUse::Output},
    {"nfc_reset", PinUse::Output},
    {"nfc_irq", PinUse::Input},
    {"led", PinUse::Output},
    {"buzzer", PinUse::Output},
    {"button_previous", PinUse::InputPullup},
    {"button_play", PinUse::InputPullup},
    {"button_next", PinUse::InputPullup},
    {"encoder_a", PinUse::InputPullup},
    {"encoder_b", PinUse::InputPullup},
    {"encoder_button", PinUse::InputPullup},
    {"battery_adc", PinUse::Analog},
    {"i2s_bclk", PinUse::Output},
    {"i2s_lrck", PinUse::Output},
    {"i2s_dout", PinUse::Output},
    {"sd_cs", PinUse::Output},
});

PinUse pin_use(std::string_view key) {
    for (const PinFunction& f : kPinFunctions) {
        if (f.key == key) {
            return f.use;
        }
    }
    return PinUse::Output;
}

template <class T>
std::string stringify(const T& value) {
    if constexpr (std::is_same_v<T, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_same_v<T, int>) {
        return std::to_string(value);
    } else {
        return value;
    }
}

std::vector<std::pair<unsigned, std::string>> flatten(const Config& config) {
    std::vector<std::pair<unsigned, std::string>> out;
    for_each_field(config, [&](const char*, const char*, const auto& value, unsigned flags) {
        out.emplace_back(flags, stringify(value));
    });
    return out;
}

}  // namespace

Config defaults(Chip chip) {
    Config c;
    switch (chip) {
        case Chip::Esp32:
            c.pins.i2c_sda = 21;
            c.pins.i2c_scl = 22;
            c.pins.spi_sck = 18;
            c.pins.spi_miso = 19;
            c.pins.spi_mosi = 23;
            c.pins.nfc_cs = 5;
            break;
        case Chip::Esp32S3:
            c.pins.i2c_sda = 8;
            c.pins.i2c_scl = 9;
            c.pins.spi_sck = 12;
            c.pins.spi_miso = 13;
            c.pins.spi_mosi = 11;
            c.pins.nfc_cs = 10;
            break;
    }
    return c;
}

void to_json(const Config& config, JsonObject out, bool redact_secrets) {
    for_each_field(config,
                   [&](const char* section, const char* key, const auto& value, unsigned flags) {
                       JsonObject s = section_of(out, section);
                       using T = std::remove_cvref_t<decltype(value)>;
                       if constexpr (std::is_same_v<T, std::string>) {
                           if (redact_secrets && (flags & kSecret)) {
                               s[key] = value.empty() ? std::string{} : std::string{kRedacted};
                               return;
                           }
                       }
                       s[key] = value;
                   });
}

std::vector<Issue> apply_json(Config& config, JsonObjectConst patch, bool strict) {
    std::vector<Issue> issues;
    if (strict) {
        for (JsonPairConst section : patch) {
            const std::string_view name = section.key().c_str();
            if (!is_known_section(name)) {
                issues.push_back({std::string(name), "unknown setting"});
                continue;
            }
            if (!section.value().is<JsonObjectConst>()) {
                issues.push_back({std::string(name), "must be an object"});
                continue;
            }
            for (JsonPairConst field : section.value().as<JsonObjectConst>()) {
                const std::string_view key = field.key().c_str();
                if (!is_known_field(name, key)) {
                    issues.push_back({path_of(name, key), "unknown setting"});
                }
            }
        }
    }

    for_each_field(config, [&](const char* section, const char* key, auto& value, unsigned flags) {
        JsonVariantConst v = patch[section][key];
        if (v.isNull()) {
            return;
        }
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<T, bool>) {
            if (!v.is<bool>()) {
                issues.push_back({path_of(section, key), "must be true or false"});
                return;
            }
            value = v.as<bool>();
        } else if constexpr (std::is_same_v<T, int>) {
            if (!v.is<int>()) {
                issues.push_back({path_of(section, key), "must be a whole number"});
                return;
            }
            value = v.as<int>();
        } else {
            if (!v.is<const char*>()) {
                issues.push_back({path_of(section, key), "must be text"});
                return;
            }
            const std::string_view text = v.as<const char*>();
            if ((flags & kSecret) && text == kRedacted) {
                return;
            }
            value = std::string(text);
        }
    });
    return issues;
}

std::vector<Issue> validate(const Config& c, Chip chip, bool has_psram) {
    std::vector<Issue> issues;
    auto error = [&](std::string path, std::string message) {
        issues.push_back({std::move(path), std::move(message), false});
    };

    if (c.device.name.empty() || c.device.name.size() > 32 ||
        has_control_characters(c.device.name)) {
        error("device.name", "use 1 to 32 characters");
    }
    if (!c.device.hostname.empty() && !valid_hostname(c.device.hostname)) {
        error("device.hostname",
              "use up to 63 letters, digits and hyphens, not starting or "
              "ending with a hyphen");
    }

    if (c.wifi.ssid.size() > 32) {
        error("wifi.ssid", "Wi-Fi network names are at most 32 bytes");
    }
    const auto& pw = c.wifi.password;
    if (!pw.empty() && !(pw.size() >= 8 && pw.size() <= 63) && !(pw.size() == 64 && all_hex(pw))) {
        error("wifi.password", "Wi-Fi passwords are 8 to 63 characters");
    }
    if (!c.wifi.ap_password.empty() &&
        (c.wifi.ap_password.size() < 8 || c.wifi.ap_password.size() > 63)) {
        error("wifi.ap_password",
              "use 8 to 63 characters, or leave empty for an open setup "
              "network");
    }

    if (c.mqtt.enabled && c.mqtt.host.empty()) {
        error("mqtt.host", "enter the address of your MQTT broker");
    }
    if (c.mqtt.host.size() > 128 || has_control_characters(c.mqtt.host)) {
        error("mqtt.host", "the broker address is not valid");
    }
    if (c.mqtt.port < 1 || c.mqtt.port > 65535) {
        error("mqtt.port", "use a port from 1 to 65535");
    }
    if (!valid_topic_prefix(c.mqtt.topic_prefix)) {
        error("mqtt.topic_prefix",
              "use up to 64 characters without '#', '+' or a leading or "
              "trailing '/'");
    }
    if (!valid_topic_prefix(c.mqtt.discovery_prefix)) {
        error("mqtt.discovery_prefix",
              "use up to 64 characters without '#', '+' or a leading "
              "or trailing '/'");
    }

    const bool driver_ok =
        !c.nfc.driver.empty() && c.nfc.driver.size() <= 32 &&
        std::ranges::all_of(c.nfc.driver, [](char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-';
        });
    if (!driver_ok) {
        error("nfc.driver", "use \"auto\" or a reader ID such as \"pn532-i2c\"");
    }
    if (c.nfc.poll_interval_ms < 50 || c.nfc.poll_interval_ms > 2000) {
        error("nfc.poll_interval_ms", "use 50 to 2000 milliseconds");
    }
    if (c.nfc.removal_polls < 1 || c.nfc.removal_polls > 20) {
        error("nfc.removal_polls", "use 1 to 20 polls");
    }

    std::vector<std::pair<int, std::string>> used;
    for_each_field(c, [&](const char* section, const char* key, const auto& value, unsigned) {
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<T, int>) {
            if (std::string_view(section) != "pins") {
                return;
            }
            const std::string path = path_of(section, key);
            if (auto problem = check_pin(chip, has_psram, value, pin_use(key))) {
                issues.push_back({path, problem->message, problem->warning});
            }
            if (value < 0) {
                return;
            }
            const auto clash = std::ranges::find(used, value, &std::pair<int, std::string>::first);
            if (clash != used.end()) {
                error(path,
                      "GPIO " + std::to_string(value) + " is already used by " + clash->second);
            } else {
                used.emplace_back(value, path);
            }
        }
    });

    if (c.feedback.led_count < 1 || c.feedback.led_count > 64) {
        error("feedback.led_count", "use 1 to 64 LEDs");
    }
    if (c.feedback.led_brightness < 0 || c.feedback.led_brightness > 255) {
        error("feedback.led_brightness", "use 0 to 255");
    }
    if (c.security.admin_password.size() > 64) {
        error("security.admin_password", "use at most 64 characters");
    }
    if (c.updates.check && !c.updates.manifest_url.starts_with("https://") &&
        !c.updates.manifest_url.starts_with("http://")) {
        error("updates.manifest_url", "use an http:// or https:// address");
    }
    if (c.time.ntp_server.empty() || c.time.ntp_server.size() > 64) {
        error("time.ntp_server", "enter a time server, for example pool.ntp.org");
    }
    if (c.time.timezone.empty() || c.time.timezone.size() > 64) {
        error("time.timezone", "enter a POSIX time zone such as CET-1CEST,M3.5.0,M10.5.0/3");
    }
    return issues;
}

bool has_errors(std::span<const Issue> issues) {
    return std::ranges::any_of(issues, [](const Issue& i) { return !i.warning; });
}

bool restart_required(const Config& before, const Config& after) {
    const auto a = flatten(before);
    const auto b = flatten(after);
    for (std::size_t i = 0; i < a.size(); ++i) {
        if ((a[i].first & kRestart) && a[i].second != b[i].second) {
            return true;
        }
    }
    return false;
}

std::span<const Migration> migrations() {
    // Add {from, run} entries here when the schema changes, and bump kSchemaVersion.
    return {};
}

std::expected<int, MigrateError> migrate(JsonObject root, std::span<const Migration> steps,
                                         int target) {
    JsonVariant v = root["v"];
    if (!v.is<int>()) {
        return std::unexpected(MigrateError::NoVersion);
    }
    const int start = v.as<int>();
    if (start > target) {
        return std::unexpected(MigrateError::TooNew);
    }
    for (int version = start; version < target; ++version) {
        const auto step =
            std::ranges::find_if(steps, [&](const Migration& m) { return m.from == version; });
        if (step == steps.end()) {
            return std::unexpected(MigrateError::MissingStep);
        }
        step->run(root);
        root["v"] = version + 1;
    }
    return start;
}

LoadResult load(std::string_view stored, Chip chip) {
    LoadResult result;
    result.config = defaults(chip);
    if (stored.empty()) {
        result.fresh = true;
        return result;
    }
    JsonDocument doc;
    if (deserializeJson(doc, stored) || !doc.is<JsonObject>()) {
        result.reset = true;
        result.issues.push_back({"",
                                 "the stored settings could not be read, so the defaults are "
                                 "in use"});
        return result;
    }
    JsonObject root = doc.as<JsonObject>();
    const auto from = migrate(root, migrations(), kSchemaVersion);
    if (!from) {
        result.reset = true;
        result.issues.push_back(
            {"", from.error() == MigrateError::TooNew
                     ? "the stored settings come from a newer firmware, so the defaults are in use"
                     : "the stored settings have an unknown format, so the defaults are in use"});
        return result;
    }
    result.migrated = *from != kSchemaVersion;
    root.remove("v");
    result.issues = apply_json(result.config, root, /*strict=*/false);
    return result;
}

std::string save(const Config& config) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    root["v"] = kSchemaVersion;
    to_json(config, root, /*redact_secrets=*/false);
    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace tapwave::config
