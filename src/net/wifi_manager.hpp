// Wi-Fi: the station connection, the setup access point with its captive DNS, and network
// scans (ARCHITECTURE.md section 11). loop() runs on the loop task; connect(), start_scan() and
// the getters are safe from any task.
#pragma once

#include <DNSServer.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/improv_session.hpp"

namespace tapwave::net {

enum class WifiState : std::uint8_t {
    Setup,       // no credentials: setup access point open
    Connecting,  // trying credentials
    Connected,
    Offline,  // credentials stored, network not reachable
};

std::string_view to_string(WifiState state);

struct WifiStatus {
    WifiState state = WifiState::Setup;
    std::string ssid;
    std::string ip;
    int rssi = 0;
    bool ap_active = false;
    std::string ap_ssid;
    std::string ap_ip;
};

class WifiManager {
public:
    static constexpr std::uint32_t kAttemptTimeoutMs = 20000;
    static constexpr std::uint32_t kOfflineApDelayMs = 60000;
    static constexpr std::uint32_t kApLingerMs = 60000;
    static constexpr std::uint32_t kScanMaxAgeMs = 30000;

    struct Settings {
        std::string ssid;
        std::string password;
        std::string ap_ssid;
        std::string ap_password;
        std::string hostname;
    };

    void begin(const Settings& settings);
    void loop(std::uint32_t now_ms);

    // Tries new credentials. The outcome is attempt_result(); on success
    // on_credentials_confirmed runs on the loop task so the caller can store them.
    void connect(std::string ssid, std::string password);
    improv::ConnectResult attempt_result() const;

    void start_scan();
    // Networks from a recent scan, strongest first; nullopt while a scan runs or before one.
    std::optional<std::vector<improv::Network>> networks() const;
    bool scanning() const;
    // True when there are no scan results or they are older than kScanMaxAgeMs.
    bool networks_stale(std::uint32_t now_ms) const;

    WifiStatus status() const;

    std::function<void(const std::string& ssid, const std::string& password)>
        on_credentials_confirmed;
    std::function<void(WifiState state)> on_state;

private:
    void set_state(WifiState state, std::uint32_t now_ms);
    void begin_station(const std::string& ssid, const std::string& password, std::uint32_t now);
    void start_ap();
    void stop_ap();
    void collect_scan(std::uint32_t now_ms);

    mutable std::mutex mutex_;
    Settings settings_;
    WifiState state_ = WifiState::Setup;
    WifiStatus status_;

    struct Request {
        std::string ssid;
        std::string password;
    };
    std::optional<Request> pending_;
    std::optional<Request> trying_;  // credentials of a connect() in progress
    improv::ConnectResult attempt_ = improv::ConnectResult::Pending;
    bool scan_requested_ = false;
    bool scan_running_ = false;
    std::vector<improv::Network> networks_;
    std::uint32_t networks_at_ = 0;
    bool have_networks_ = false;

    std::uint32_t attempt_started_ = 0;
    std::uint32_t offline_since_ = 0;
    std::uint32_t last_retry_ = 0;
    std::uint32_t ap_stop_at_ = 0;
    bool ap_active_ = false;
    DNSServer dns_;
};

}  // namespace tapwave::net
