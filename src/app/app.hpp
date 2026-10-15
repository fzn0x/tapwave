// The application: owns every module, runs the event loop, and answers the API.
#pragma once

#include <ArduinoJson.h>

#include <array>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "app/config_store.hpp"
#include "app/device.hpp"
#include "app/events.hpp"
#include "app/scan_service.hpp"
#include "core/config.hpp"
#include "core/event_bus.hpp"
#include "drivers/io/buzzer.hpp"
#include "drivers/io/status_led.hpp"
#include "net/http_server.hpp"
#include "net/improv_ble.hpp"
#include "net/improv_serial.hpp"
#include "net/mqtt_service.hpp"
#include "net/updater.hpp"
#include "net/wifi_manager.hpp"

namespace tapwave::app {

struct PatchResult {
    std::vector<config::Issue> issues;  // errors refuse the patch; warnings come along
    bool saved = false;
    bool restart_required = false;
};

class App {
public:
    void setup();
    void loop();

    // ---- used by the API, safe from any task ----

    void post(Event event);
    std::string status_json() const;
    std::string scans_json() const;
    std::string config_json() const;
    std::string update_json() const;
    std::string reader_json() const;
    std::string networks_json();
    config::Config config() const;

    PatchResult patch_config(JsonObjectConst patch);
    void factory_reset();
    void try_wifi(std::string ssid, std::string password);

    // Where to send a browser that reached the setup access point under a foreign host name
    // (an operating system's connectivity check), or nullopt to serve the request normally.
    std::optional<std::string> captive_portal_redirect(std::string_view host) const;

    bool password_required() const;
    bool check_password(std::string_view password) const;
    std::string create_session();
    bool valid_session(std::string_view token) const;
    void end_session(std::string_view token);

    net::Updater& updater() { return updater_; }
    ScanService& scans() { return scans_; }
    const Device& device() const { return device_; }

private:
    void dispatch(const Event& event);
    void on_scan(const Scan& scan);
    void on_removed(const std::string& uid);
    void on_wifi();
    void on_mqtt_connection(bool connected);
    void on_mqtt_command(const MqttCommand& command);
    void on_config_changed(const config::Config& before, const config::Config& after);
    void on_update_changed();
    void feedback(FeedbackKind kind);

    void start_mqtt(const config::Config& config);
    void publish_state();
    void publish_scan(const Scan& scan, const std::string& json);
    void update_led_pattern();
    void periodic(std::uint32_t now);

    std::string hostname() const;
    std::string time_iso() const;
    void start_time_sync(const std::string& server);
    std::string scan_json(const Scan& scan) const;
    improv::DeviceHooks improv_hooks();
    void setup_http();

    Device device_;
    ConfigStore store_;

    mutable std::mutex mutex_;  // guards the members below up to the next blank line
    config::Config config_;
    std::vector<config::Issue> load_issues_;
    bool restart_required_ = false;
    std::optional<std::string> last_scan_json_;
    std::deque<std::string> history_;
    std::vector<std::array<char, 33>> sessions_;

    EventQueue<Event> events_{48};
    EventBus<Event> bus_;

    io::StatusLed led_;
    io::Buzzer buzzer_;
    ScanService scans_;
    net::WifiManager wifi_;
    net::ImprovSerial improv_serial_;
    net::ImprovBle improv_ble_;
    net::HttpServer http_;
    net::MqttService mqtt_;
    net::Updater updater_;

    bool filesystem_ok_ = false;
    bool network_started_ = false;
    std::uint32_t connected_at_ = 0;
    bool image_confirmed_ = false;
    std::uint32_t restart_at_ = 0;
    std::uint32_t next_status_push_ = 0;
    std::uint32_t next_diagnostics_ = 0;
    std::uint32_t next_update_check_ = 0;
    std::string ntp_server_;
};

// Registers the REST API and the WebSocket on the server (app/api.cpp).
void register_api(App& app, net::HttpServer& http, bool filesystem_ok);

}  // namespace tapwave::app
