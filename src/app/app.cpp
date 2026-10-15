#include "app/app.hpp"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_sntp.h>

#include <ctime>

#include "core/bytes.hpp"
#include "core/mqtt_layout.hpp"
#include "core/rtttl.hpp"
#include "core/scan_json.hpp"
#include "core/time_format.hpp"

namespace tapwave::app {

namespace {

constexpr const char* kTag = "app";
constexpr std::size_t kHistorySize = 20;
constexpr std::size_t kMaxSessions = 8;
constexpr std::uint32_t kStatusPushMs = 5000;
constexpr std::uint32_t kDiagnosticsMs = 60000;
constexpr std::uint32_t kConfirmImageAfterMs = 30000;
constexpr std::uint32_t kFirstUpdateCheckMs = 120000;
constexpr std::uint32_t kUpdateCheckEveryMs = 12UL * 60 * 60 * 1000;
constexpr std::string_view kOkBeep = "ok:d=16,o=5,b=100:b";

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

bool due(std::uint32_t now, std::uint32_t at) {
    return at != 0 && static_cast<std::int32_t>(now - at) >= 0;
}

std::string chip_name(Chip chip) {
    return chip == Chip::Esp32S3 ? "ESP32-S3" : "ESP32";
}

void write_issues(JsonArray out, const std::vector<config::Issue>& issues) {
    for (const auto& issue : issues) {
        JsonObject item = out.add<JsonObject>();
        item["path"] = issue.path;
        item["message"] = issue.message;
        item["warning"] = issue.warning;
    }
}

}  // namespace

// ---------------------------------------------------------------------------- setup and loop

void App::setup() {
    Serial.begin(115200);
    device_ = Device::detect();
    ESP_LOGI(kTag, "Tapwave %s (%s) on %s, flash %u KB, PSRAM %u KB", device_.version.c_str(),
             device_.environment.c_str(), device_.chip_model.c_str(),
             static_cast<unsigned>(device_.flash_size / 1024),
             static_cast<unsigned>(device_.psram_size / 1024));

    auto loaded = store_.load(device_.chip);
    {
        std::scoped_lock lock(mutex_);
        config_ = loaded.config;
        load_issues_ = loaded.issues;
    }
    const config::Config cfg = config();

    filesystem_ok_ = LittleFS.begin(false, "/littlefs", 10, "spiffs");
    if (!filesystem_ok_) {
        ESP_LOGW(kTag, "no web interface filesystem; serving the recovery page");
    }

    led_.begin(cfg.pins.led, cfg.feedback.led_count,
               static_cast<std::uint8_t>(cfg.feedback.led_brightness));
    led_.set_enabled(cfg.feedback.led);
    led_.set_pattern(io::LedPattern::Booting);
    buzzer_.begin(cfg.pins.buzzer);
    buzzer_.set_enabled(cfg.feedback.buzzer);

    bus_.subscribe([this](const Event& event) { dispatch(event); });

    scans_.start(cfg, events_);

    updater_.begin(device_.version, device_.environment);
    updater_.on_change = [this] { post(UpdateChanged{}); };
    updater_.before_filesystem_write = [this] {
        filesystem_ok_ = false;
        LittleFS.end();
    };

    wifi_.on_state = [this](net::WifiState) { post(WifiChanged{}); };
    wifi_.on_credentials_confirmed = [this](const std::string& ssid, const std::string& password) {
        JsonDocument patch;
        patch["wifi"]["ssid"] = ssid;
        patch["wifi"]["password"] = password;
        patch_config(patch.as<JsonObjectConst>());
    };
    wifi_.begin(
        {cfg.wifi.ssid, cfg.wifi.password, device_.setup_name(), cfg.wifi.ap_password, hostname()});

    improv_serial_.begin(Serial, improv_hooks(), !cfg.wifi.ssid.empty());
    if (cfg.wifi.ssid.empty()) {
        improv_ble_.begin(device_.setup_name(), improv_hooks());
    }

    setup_http();
    update_led_pattern();
}

void App::loop() {
    const std::uint32_t now = millis();
    wifi_.loop(now);
    improv_serial_.loop();
    improv_ble_.loop();
    events_.drain([this](const Event& event) { bus_.publish(event); }, 16);
    led_.update(now);
    buzzer_.update(now);
    periodic(now);
    delay(2);
}

void App::post(Event event) {
    if (!events_.push(std::move(event))) {
        ESP_LOGW(kTag, "event queue full, an event was dropped");
    }
}

void App::dispatch(const Event& event) {
    std::visit(Overloaded{
                   [this](const TagScanned& e) { on_scan(e.scan); },
                   [this](const TagRemoved& e) { on_removed(e.uid); },
                   [this](const ReaderChanged&) {
                       update_led_pattern();
                       publish_state();
                   },
                   [this](const WifiChanged&) { on_wifi(); },
                   [this](const MqttConnection& e) { on_mqtt_connection(e.connected); },
                   [this](const MqttCommand& e) { on_mqtt_command(e); },
                   [this](const ConfigChanged& e) { on_config_changed(e.before, e.after); },
                   [this](const Feedback& e) { feedback(e.kind); },
                   [this](const PlayMelody& e) { buzzer_.play(e.melody); },
                   [this](const RestartRequested& e) { restart_at_ = millis() + e.delay_ms; },
                   [this](const UpdateChanged&) { on_update_changed(); },
               },
               event);
}

// ---------------------------------------------------------------------------- events

void App::on_scan(const Scan& scan) {
    const std::string json = scan_json(scan);
    {
        std::scoped_lock lock(mutex_);
        last_scan_json_ = json;
        history_.push_front(json);
        if (history_.size() > kHistorySize) {
            history_.pop_back();
        }
    }
    led_.flash(io::colors::kGreen, 600);
    buzzer_.play(rtttl::kSuccess);
    http_.broadcast(R"({"type":"scan","scan":)" + json + "}");
    if (config().modes.home_assistant) {
        publish_scan(scan, json);
    }
}

void App::on_removed(const std::string& uid) {
    JsonDocument doc;
    doc["type"] = "removed";
    doc["uid"] = uid;
    std::string message;
    serializeJson(doc, message);
    http_.broadcast(message);
    if (mqtt_.connected() && config().modes.home_assistant) {
        mqtt_.publish(mqtt_.topics().tag_present(), "OFF", true);
        mqtt_.publish(mqtt_.topics().tag_event(), mqtt::removed_event_json(uid), false);
    }
}

void App::on_wifi() {
    const auto status = wifi_.status();
    const config::Config cfg = config();
    if (status.state == net::WifiState::Connected) {
        if (!network_started_) {
            network_started_ = true;
            connected_at_ = millis();
            next_update_check_ = connected_at_ + kFirstUpdateCheckMs;
            const std::string name = hostname();
            if (MDNS.begin(name.c_str())) {
                MDNS.addService("http", "tcp", 80);
                MDNS.addService("tapwave", "tcp", 80);
                MDNS.addServiceTxt("tapwave", "tcp", "version", device_.version.c_str());
                MDNS.addServiceTxt("tapwave", "tcp", "id", device_.id.c_str());
            }
            start_time_sync(cfg.time.ntp_server);
            ESP_LOGI(kTag, "online at http://%s/ and http://%s.local/", status.ip.c_str(),
                     name.c_str());
        }
        if (improv_ble_.running()) {
            improv_ble_.stop();
        }
        if (cfg.mqtt.enabled && !mqtt_.started()) {
            start_mqtt(cfg);
        }
    }
    update_led_pattern();
    http_.broadcast(R"({"type":"status","status":)" + status_json() + "}");
}

void App::start_mqtt(const config::Config& cfg) {
    net::MqttService::Settings settings;
    settings.host = cfg.mqtt.host;
    settings.port = cfg.mqtt.port;
    settings.tls = cfg.mqtt.tls;
    settings.username = cfg.mqtt.username;
    settings.password = cfg.mqtt.password;
    settings.client_id = hostname();
    settings.topic_prefix = mqtt::expand_prefix(cfg.mqtt.topic_prefix, device_.id);
    settings.discovery_prefix = cfg.mqtt.discovery_prefix;
    mqtt_.on_connection = [this](bool connected) { post(MqttConnection{connected}); };
    mqtt_.on_command = [this](mqtt::Inbound what, std::string payload) {
        post(MqttCommand{what, std::move(payload)});
    };
    mqtt_.start(settings);
}

void App::on_mqtt_connection(bool connected) {
    if (connected) {
        const config::Config cfg = config();
        const auto status = wifi_.status();
        mqtt::Identity identity{device_.id,
                                device_.mac,
                                cfg.device.name,
                                device_.model(),
                                device_.chip_model,
                                device_.version,
                                "http://" + status.ip + "/"};
        const auto messages =
            cfg.mqtt.discovery
                ? mqtt::discovery(identity, mqtt_.topics(), cfg.mqtt.discovery_prefix)
                : mqtt::discovery_removal(identity, cfg.mqtt.discovery_prefix);
        for (const auto& message : messages) {
            mqtt_.publish(message.topic, message.payload, message.retain);
        }
        publish_state();
    }
    http_.broadcast(R"({"type":"status","status":)" + status_json() + "}");
}

void App::publish_state() {
    if (!mqtt_.connected()) {
        return;
    }
    const config::Config cfg = config();
    const auto& topics = mqtt_.topics();
    mqtt_.publish(topics.buzzer_state(), cfg.feedback.buzzer ? "ON" : "OFF", true);
    mqtt_.publish(topics.led_state(), cfg.feedback.led ? "ON" : "OFF", true);
    mqtt_.publish(topics.tag_present(), scans_.status().tag_present ? "ON" : "OFF", true);
    {
        std::scoped_lock lock(mutex_);
        if (last_scan_json_) {
            mqtt_.publish(topics.tag_last(), *last_scan_json_, true);
        }
    }
    on_update_changed();
    next_diagnostics_ = millis();  // send diagnostics on the next loop
}

void App::publish_scan(const Scan& scan, const std::string& json) {
    if (!mqtt_.connected()) {
        return;
    }
    const auto& topics = mqtt_.topics();
    mqtt_.publish(topics.tag_scanned(), json, false);
    mqtt_.publish(topics.tag_last(), json, true);
    mqtt_.publish(topics.tag_present(), "ON", true);
    mqtt_.publish(topics.tag_event(), mqtt::scanned_event_json(scan), false);
}

void App::on_mqtt_command(const MqttCommand& command) {
    auto set_switch = [this](const char* key, const std::string& payload) {
        JsonDocument patch;
        patch["feedback"][key] = payload == "ON";
        patch_config(patch.as<JsonObjectConst>());
    };
    switch (command.what) {
        case mqtt::Inbound::BuzzerSet:
            set_switch("buzzer", command.payload);
            break;
        case mqtt::Inbound::LedSet:
            set_switch("led", command.payload);
            break;
        case mqtt::Inbound::Restart:
            restart_at_ = millis() + 500;
            break;
        case mqtt::Inbound::Identify:
            feedback(FeedbackKind::Identify);
            break;
        case mqtt::Inbound::Feedback:
            feedback(command.payload == "error" || command.payload == "ko" ? FeedbackKind::Error
                                                                           : FeedbackKind::Ok);
            break;
        case mqtt::Inbound::Rtttl:
            if (!buzzer_.play(command.payload)) {
                ESP_LOGW(kTag, "ignored a melody that is not valid RTTTL");
            }
            break;
        case mqtt::Inbound::UpdateInstall:
            if (command.payload == "install") {
                updater_.start(config().updates.manifest_url, true);
            }
            break;
        case mqtt::Inbound::HomeAssistantStatus:
            if (command.payload == "online") {
                on_mqtt_connection(true);  // Home Assistant restarted: announce everything again
            }
            break;
        case mqtt::Inbound::Unknown:
            break;
    }
}

void App::on_config_changed(const config::Config& before, const config::Config& after) {
    led_.set_enabled(after.feedback.led);
    led_.set_brightness(static_cast<std::uint8_t>(after.feedback.led_brightness));
    buzzer_.set_enabled(after.feedback.buzzer);
    scans_.set_timing(after.nfc.poll_interval_ms, after.nfc.removal_polls);

    if (before.time != after.time && wifi_.status().state == net::WifiState::Connected) {
        start_time_sync(after.time.ntp_server);
    }
    if (before.wifi.ssid != after.wifi.ssid || before.wifi.password != after.wifi.password) {
        const auto status = wifi_.status();
        if (status.ssid != after.wifi.ssid || status.state != net::WifiState::Connected) {
            wifi_.connect(after.wifi.ssid, after.wifi.password);
        }
    }
    if (before.security != after.security) {
        std::scoped_lock lock(mutex_);
        sessions_.clear();
    }

    const bool mqtt_changed = before.mqtt != after.mqtt || before.device.name != after.device.name;
    if (mqtt_changed) {
        if (mqtt_.connected() && before.mqtt.discovery &&
            (!after.mqtt.enabled || !after.mqtt.discovery)) {
            mqtt::Identity identity{device_.id,
                                    device_.mac,
                                    before.device.name,
                                    device_.model(),
                                    device_.chip_model,
                                    device_.version,
                                    ""};
            for (const auto& m : mqtt::discovery_removal(identity, before.mqtt.discovery_prefix)) {
                mqtt_.publish(m.topic, m.payload, m.retain);
            }
        }
        mqtt_.stop();
        if (after.mqtt.enabled && wifi_.status().state == net::WifiState::Connected) {
            start_mqtt(after);
        }
    } else {
        publish_state();
    }
    http_.broadcast(R"({"type":"status","status":)" + status_json() + "}");
}

void App::on_update_changed() {
    const auto status = updater_.status();
    if (mqtt_.connected()) {
        mqtt::UpdateState state;
        state.installed_version = status.installed;
        state.latest_version = status.latest;
        state.release_url = status.release_url;
        state.in_progress = status.phase == net::UpdatePhase::Downloading ||
                            status.phase == net::UpdatePhase::Writing;
        state.percentage = state.in_progress ? status.percent : std::nullopt;
        mqtt_.publish(mqtt_.topics().update_state(), mqtt::update_state_json(state), true);
    }
    http_.broadcast(R"({"type":"update","update":)" + update_json() + "}");
    update_led_pattern();
    if (status.phase == net::UpdatePhase::Restarting && restart_at_ == 0) {
        restart_at_ = millis() + 1500;
    }
}

void App::feedback(FeedbackKind kind) {
    switch (kind) {
        case FeedbackKind::Ok:
            led_.flash(io::colors::kGreen, 800, 2);
            buzzer_.play(kOkBeep);
            break;
        case FeedbackKind::Error:
            led_.flash(io::colors::kRed, 1200, 3);
            buzzer_.play(rtttl::kError);
            break;
        case FeedbackKind::Identify:
            led_.flash(io::colors::kWhite, 5000, 10);
            buzzer_.play(rtttl::kIdentify);
            break;
    }
}

void App::update_led_pattern() {
    const auto wifi = wifi_.status();
    io::LedPattern pattern = io::LedPattern::Ready;
    if (updater_.busy()) {
        pattern = io::LedPattern::Updating;
    } else if (wifi.state == net::WifiState::Setup ||
               (wifi.state == net::WifiState::Offline && wifi.ap_active)) {
        pattern = io::LedPattern::Setup;
    } else if (wifi.state != net::WifiState::Connected) {
        pattern = io::LedPattern::Connecting;
    } else if (!scans_.status().found) {
        pattern = io::LedPattern::NoReader;
    }
    led_.set_pattern(pattern);
}

void App::periodic(std::uint32_t now) {
    if (now >= next_status_push_) {
        next_status_push_ = now + kStatusPushMs;
        http_.broadcast(R"({"type":"status","status":)" + status_json() + "}");
    }
    if (mqtt_.connected() && due(now, next_diagnostics_)) {
        next_diagnostics_ = now + kDiagnosticsMs;
        const auto wifi = wifi_.status();
        const auto reader = scans_.status();
        mqtt::Diagnostics d;
        d.rssi = wifi.rssi;
        d.ip = wifi.ip;
        d.uptime_s = now / 1000;
        d.free_heap = ESP.getFreeHeap();
        d.reader = reader.found ? reader.info.summary() : "none";
        d.version = device_.version;
        mqtt_.publish(mqtt_.topics().diagnostics(), mqtt::diagnostics_json(d), true);
    }
    if (!image_confirmed_ && connected_at_ != 0 && now - connected_at_ >= kConfirmImageAfterMs) {
        image_confirmed_ = true;
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            ESP_LOGI(kTag, "this firmware is confirmed; no rollback");
        }
    }
    if (due(now, next_update_check_) && wifi_.status().state == net::WifiState::Connected) {
        next_update_check_ = now + kUpdateCheckEveryMs;
        const config::Config cfg = config();
        if (cfg.updates.check && !updater_.busy()) {
            updater_.start(cfg.updates.manifest_url, false);
        }
    }
    if (due(now, restart_at_)) {
        ESP_LOGI(kTag, "restarting");
        mqtt_.stop();
        delay(100);
        ESP.restart();
    }
}

// ---------------------------------------------------------------------------- helpers

config::Config App::config() const {
    std::scoped_lock lock(mutex_);
    return config_;
}

std::string App::hostname() const {
    std::scoped_lock lock(mutex_);
    return config_.device.hostname.empty() ? device_.default_hostname() : config_.device.hostname;
}

std::string App::time_iso() const {
    const std::time_t now = std::time(nullptr);
    if (now < 1700000000) {
        return {};  // the clock has not been set yet
    }
    return iso8601_utc(now);
}

void App::start_time_sync(const std::string& server) {
    // ESP-IDF's SNTP client directly: Arduino's configTime() also sets a time zone, which pulls
    // tzset into IRAM (see core/time_format.hpp).
    ntp_server_ = server;  // SNTP keeps the pointer
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_server_.c_str());
    esp_sntp_init();
}

std::string App::scan_json(const Scan& scan) const {
    const std::string name = hostname();
    const std::string time = time_iso();
    return scan_to_json(scan, {name, time});
}

PatchResult App::patch_config(JsonObjectConst patch) {
    PatchResult result;
    std::unique_lock lock(mutex_);
    config::Config next = config_;
    result.issues = config::apply_json(next, patch, /*strict=*/true);
    auto validation = config::validate(next, device_.chip, device_.has_psram);
    result.issues.insert(result.issues.end(), validation.begin(), validation.end());
    if (config::has_errors(result.issues)) {
        return result;
    }
    if (next == config_) {
        result.saved = true;
        result.restart_required = restart_required_;
        return result;
    }
    if (!store_.save(next)) {
        result.issues.push_back({"", "the settings could not be saved to flash", false});
        return result;
    }
    config::Config before = config_;
    restart_required_ = restart_required_ || config::restart_required(before, next);
    config_ = next;
    result.saved = true;
    result.restart_required = restart_required_;
    lock.unlock();
    post(ConfigChanged{std::move(before), std::move(next)});
    return result;
}

void App::factory_reset() {
    store_.erase();
    post(RestartRequested{1000});
}

void App::try_wifi(std::string ssid, std::string password) {
    wifi_.connect(std::move(ssid), std::move(password));
}

std::optional<std::string> App::captive_portal_redirect(std::string_view host) const {
    const auto status = wifi_.status();
    if (!status.ap_active) {
        return std::nullopt;
    }
    if (const auto colon = host.find(':'); colon != std::string_view::npos) {
        host = host.substr(0, colon);
    }
    const std::string name = hostname();
    if (host.empty() || host == status.ap_ip || host == status.ip || host == name ||
        host == name + ".local") {
        return std::nullopt;
    }
    return "http://" + status.ap_ip + "/";
}

bool App::password_required() const {
    std::scoped_lock lock(mutex_);
    return !config_.security.admin_password.empty();
}

bool App::check_password(std::string_view password) const {
    std::scoped_lock lock(mutex_);
    const std::string& expected = config_.security.admin_password;
    if (expected.empty()) {
        return true;
    }
    // Compare every byte, so the time taken does not reveal how much matched.
    unsigned char difference = password.size() == expected.size() ? 0 : 1;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const char c = i < password.size() ? password[i] : 0;
        difference |= static_cast<unsigned char>(c ^ expected[i]);
    }
    return difference == 0;
}

std::string App::create_session() {
    std::array<std::uint8_t, 16> random{};
    esp_fill_random(random.data(), random.size());
    const std::string token = to_hex(random, {}, false);
    std::array<char, 33> stored{};
    std::copy(token.begin(), token.end(), stored.begin());
    std::scoped_lock lock(mutex_);
    if (sessions_.size() >= kMaxSessions) {
        sessions_.erase(sessions_.begin());
    }
    sessions_.push_back(stored);
    return token;
}

bool App::valid_session(std::string_view token) const {
    if (token.size() != 32) {
        return false;
    }
    std::scoped_lock lock(mutex_);
    for (const auto& s : sessions_) {
        if (std::string_view(s.data(), 32) == token) {
            return true;
        }
    }
    return false;
}

void App::end_session(std::string_view token) {
    std::scoped_lock lock(mutex_);
    std::erase_if(sessions_,
                  [&](const auto& s) { return std::string_view(s.data(), 32) == token; });
}

improv::DeviceHooks App::improv_hooks() {
    improv::DeviceHooks hooks;
    hooks.now_ms = [] { return static_cast<std::uint32_t>(millis()); };
    hooks.connect = [this](const std::string& ssid, const std::string& password) {
        wifi_.connect(ssid, password);
    };
    hooks.connect_result = [this] { return wifi_.attempt_result(); };
    hooks.online = [this] { return wifi_.status().state == net::WifiState::Connected; };
    hooks.device_url = [this] {
        const auto status = wifi_.status();
        return status.state == net::WifiState::Connected ? "http://" + status.ip + "/"
                                                         : std::string{};
    };
    hooks.identify = [this] { post(Feedback{FeedbackKind::Identify}); };
    hooks.device_info = [this] {
        return std::vector<std::string>{"Tapwave", device_.version, chip_name(device_.chip),
                                        config().device.name};
    };
    hooks.start_scan = [this] { wifi_.start_scan(); };
    hooks.scan_results = [this]() -> std::optional<std::vector<improv::Network>> {
        if (wifi_.scanning()) {
            return std::nullopt;
        }
        return wifi_.networks();
    };
    hooks.hostname = [this] { return hostname(); };
    hooks.set_hostname = [this](const std::string& name) {
        JsonDocument patch;
        patch["device"]["hostname"] = name;
        return patch_config(patch.as<JsonObjectConst>()).saved;
    };
    hooks.device_name = [this] { return config().device.name; };
    hooks.set_device_name = [this](const std::string& name) {
        JsonDocument patch;
        patch["device"]["name"] = name;
        return patch_config(patch.as<JsonObjectConst>()).saved;
    };
    return hooks;
}

void App::setup_http() {
    if (!http_.start(80, 40)) {
        return;
    }
    register_api(*this, http_, filesystem_ok_);
}

// ---------------------------------------------------------------------------- JSON views

std::string App::status_json() const {
    JsonDocument doc;
    const config::Config cfg = config();

    JsonObject dev = doc["device"].to<JsonObject>();
    dev["name"] = cfg.device.name;
    dev["hostname"] = hostname();
    dev["id"] = device_.id;
    dev["mac"] = device_.mac;
    dev["chip"] = device_.chip_model;
    dev["flash"] = device_.flash_size;
    dev["psram"] = device_.psram_size;
    dev["environment"] = device_.environment;

    doc["version"] = device_.version;
    doc["uptime"] = millis() / 1000;
    JsonObject heap = doc["heap"].to<JsonObject>();
    heap["free"] = ESP.getFreeHeap();
    heap["min_free"] = ESP.getMinFreeHeap();
    if (device_.has_psram) {
        heap["psram_free"] = ESP.getFreePsram();
    }
    const std::string now = time_iso();
    if (!now.empty()) {
        doc["time"] = now;
    }

    const auto wifi = wifi_.status();
    JsonObject w = doc["wifi"].to<JsonObject>();
    w["state"] = net::to_string(wifi.state);
    w["ssid"] = wifi.state == net::WifiState::Connected ? wifi.ssid : cfg.wifi.ssid;
    if (wifi.state == net::WifiState::Connected) {
        w["ip"] = wifi.ip;
        w["rssi"] = wifi.rssi;
    }
    if (!wifi.attempt.empty()) {
        w["attempt"] = wifi.attempt;  // the outcome of the last network tried from the UI or Improv
    }
    JsonObject ap = w["access_point"].to<JsonObject>();
    ap["active"] = wifi.ap_active;
    ap["ssid"] = wifi.ap_ssid;
    if (wifi.ap_active) {
        ap["ip"] = wifi.ap_ip;
    }

    JsonObject m = doc["mqtt"].to<JsonObject>();
    m["enabled"] = cfg.mqtt.enabled;
    m["connected"] = mqtt_.connected();
    m["broker"] = cfg.mqtt.host;
    if (cfg.mqtt.enabled) {
        m["topic_prefix"] = mqtt::expand_prefix(cfg.mqtt.topic_prefix, device_.id);
        const std::string error = mqtt_.last_error();
        if (!error.empty() && !mqtt_.connected()) {
            m["error"] = error;
            m["error_code"] = mqtt_.last_error_code();
        }
    }

    const auto reader = scans_.status();
    JsonObject r = doc["reader"].to<JsonObject>();
    r["found"] = reader.found;
    if (reader.found) {
        r["model"] = reader.info.model;
        r["summary"] = reader.info.summary();
        r["driver"] = reader.info.driver;
    } else {
        r["problem"] = reader.problem;
    }
    doc["tag_present"] = reader.tag_present;

    JsonObject modes = doc["modes"].to<JsonObject>();
    modes["home_assistant"] = cfg.modes.home_assistant;
    modes["player"] = cfg.modes.player;

    const auto update = updater_.status();
    JsonObject u = doc["update"].to<JsonObject>();
    u["phase"] = net::to_string(update.phase);
    u["available"] = update.available;
    if (!update.latest.empty()) {
        u["latest"] = update.latest;
    }

    {
        std::scoped_lock lock(mutex_);
        if (last_scan_json_) {
            doc["last_scan"] = serialized(*last_scan_json_);
        } else {
            doc["last_scan"] = nullptr;
        }
        doc["restart_required"] = restart_required_;
        write_issues(doc["config_issues"].to<JsonArray>(), load_issues_);
    }
    doc["filesystem"] = filesystem_ok_;
    doc["auth_required"] = !cfg.security.admin_password.empty();

    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string App::scans_json() const {
    std::string out = R"({"scans":[)";
    std::scoped_lock lock(mutex_);
    for (std::size_t i = 0; i < history_.size(); ++i) {
        if (i > 0) {
            out += ',';
        }
        out += history_[i];
    }
    out += "]}";
    return out;
}

std::string App::config_json() const {
    JsonDocument doc;
    {
        std::scoped_lock lock(mutex_);
        config::to_json(config_, doc["config"].to<JsonObject>(), /*redact_secrets=*/true);
        doc["restart_required"] = restart_required_;
    }
    doc["defaults"]["hostname"] = device_.default_hostname();
    doc["chip"] = chip_name(device_.chip);
    doc["psram"] = device_.has_psram;
    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string App::update_json() const {
    const auto s = updater_.status();
    JsonDocument doc;
    doc["phase"] = net::to_string(s.phase);
    doc["installed"] = s.installed;
    doc["latest"] = s.latest;
    doc["available"] = s.available;
    doc["release_url"] = s.release_url;
    doc["busy"] = updater_.busy();
    if (!s.target.empty()) {
        doc["target"] = s.target;
    }
    if (s.percent) {
        doc["percent"] = *s.percent;
    }
    if (!s.error.empty()) {
        doc["error"] = s.error;
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string App::reader_json() const {
    const auto reader = scans_.status();
    JsonDocument doc;
    doc["found"] = reader.found;
    if (reader.found) {
        doc["driver"] = reader.info.driver;
        doc["model"] = reader.info.model;
        doc["bus"] = reader.info.bus;
        doc["firmware"] = reader.info.firmware;
        doc["summary"] = reader.info.summary();
    } else {
        doc["problem"] = reader.problem;
    }
    doc["tag_present"] = reader.tag_present;
    JsonArray attempts = doc["attempts"].to<JsonArray>();
    for (const auto& a : reader.attempts) {
        JsonObject item = attempts.add<JsonObject>();
        item["driver"] = a.driver;
        item["result"] = a.result;
    }
    JsonArray drivers = doc["drivers"].to<JsonArray>();
    for (const auto& entry : nfc::Registry::instance().entries()) {
        JsonObject item = drivers.add<JsonObject>();
        item["id"] = entry.id;
        item["description"] = entry.description;
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string App::networks_json() {
    if (wifi_.networks_stale(millis()) && !wifi_.scanning()) {
        wifi_.start_scan();
    }
    JsonDocument doc;
    doc["scanning"] = wifi_.scanning();
    JsonArray list = doc["networks"].to<JsonArray>();
    if (const auto networks = wifi_.networks()) {
        for (const auto& n : *networks) {
            JsonObject item = list.add<JsonObject>();
            item["ssid"] = n.ssid;
            item["rssi"] = n.rssi;
            item["secure"] = n.auth != "NO";
        }
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace tapwave::app
