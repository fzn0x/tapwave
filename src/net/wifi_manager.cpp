#include "net/wifi_manager.hpp"

#include <WiFi.h>
#include <esp_log.h>

#include <algorithm>

namespace tapwave::net {

namespace {

constexpr const char* kTag = "wifi";
constexpr std::uint32_t kOfflineRetryMs = 60000;

std::string auth_name(wifi_auth_mode_t mode) {
    switch (mode) {
        case WIFI_AUTH_OPEN:
            return "NO";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA";
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2 EAP";
        case WIFI_AUTH_WPA3_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA3";
        case WIFI_AUTH_WAPI_PSK:
            return "WAPI";
        default:
            return "WPA2";
    }
}

bool elapsed(std::uint32_t now, std::uint32_t since, std::uint32_t ms) {
    return now - since >= ms;
}

}  // namespace

std::string_view to_string(WifiState state) {
    switch (state) {
        case WifiState::Setup:
            return "setup";
        case WifiState::Connecting:
            return "connecting";
        case WifiState::Connected:
            return "connected";
        case WifiState::Offline:
            return "offline";
    }
    return "offline";
}

void WifiManager::begin(const Settings& settings) {
    settings_ = settings;
    WiFi.persistent(false);  // credentials live in Tapwave's own configuration
    WiFi.setHostname(settings_.hostname.c_str());
    WiFi.mode(WIFI_STA);
    {
        std::scoped_lock lock(mutex_);
        status_.ap_ssid = settings_.ap_ssid;
    }
    const std::uint32_t now = millis();
    if (settings_.ssid.empty()) {
        start_ap();
        set_state(WifiState::Setup, now);
    } else {
        begin_station(settings_.ssid, settings_.password, now);
    }
}

void WifiManager::begin_station(const std::string& ssid, const std::string& password,
                                std::uint32_t now) {
    WiFi.mode(ap_active_ ? WIFI_AP_STA : WIFI_STA);
    WiFi.setAutoReconnect(!ap_active_);
    WiFi.begin(ssid.c_str(), password.c_str());
    attempt_started_ = now;
    set_state(WifiState::Connecting, now);
}

void WifiManager::start_ap() {
    if (ap_active_) {
        return;
    }
    // Station retries make the access point hop channels, which drops phones on the setup page,
    // so they slow down while the access point is up.
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_AP_STA);
    const char* password = settings_.ap_password.empty() ? nullptr : settings_.ap_password.c_str();
    WiFi.softAP(settings_.ap_ssid.c_str(), password);
    dns_.setErrorReplyCode(DNSReplyCode::NoError);
    dns_.start(53, "*", WiFi.softAPIP());
    ap_active_ = true;
    ap_stop_at_ = 0;
    std::scoped_lock lock(mutex_);
    status_.ap_active = true;
    status_.ap_ip = WiFi.softAPIP().toString().c_str();
    ESP_LOGI(kTag, "setup access point %s at %s", settings_.ap_ssid.c_str(), status_.ap_ip.c_str());
}

void WifiManager::stop_ap() {
    if (!ap_active_) {
        return;
    }
    dns_.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    ap_active_ = false;
    std::scoped_lock lock(mutex_);
    status_.ap_active = false;
    status_.ap_ip.clear();
    ESP_LOGI(kTag, "setup access point closed");
}

void WifiManager::set_state(WifiState state, std::uint32_t now) {
    if (state == WifiState::Offline && state_ != WifiState::Offline) {
        offline_since_ = now;
        last_retry_ = now;
    }
    const bool changed = state != state_;
    state_ = state;
    {
        std::scoped_lock lock(mutex_);
        status_.state = state;
    }
    if (changed) {
        ESP_LOGI(kTag, "state: %s", std::string(to_string(state)).c_str());
        if (on_state) {
            on_state(state);
        }
    }
}

void WifiManager::connect(std::string ssid, std::string password) {
    std::scoped_lock lock(mutex_);
    pending_ = Request{std::move(ssid), std::move(password)};
    attempt_ = improv::ConnectResult::Pending;
}

improv::ConnectResult WifiManager::attempt_result() const {
    std::scoped_lock lock(mutex_);
    return attempt_;
}

void WifiManager::start_scan() {
    std::scoped_lock lock(mutex_);
    scan_requested_ = true;
}

std::optional<std::vector<improv::Network>> WifiManager::networks() const {
    std::scoped_lock lock(mutex_);
    if (!have_networks_) {
        return std::nullopt;
    }
    return networks_;
}

bool WifiManager::scanning() const {
    std::scoped_lock lock(mutex_);
    return scan_running_ || scan_requested_;
}

bool WifiManager::networks_stale(std::uint32_t now) const {
    std::scoped_lock lock(mutex_);
    return !have_networks_ || elapsed(now, networks_at_, kScanMaxAgeMs);
}

WifiStatus WifiManager::status() const {
    std::scoped_lock lock(mutex_);
    return status_;
}

void WifiManager::collect_scan(std::uint32_t now) {
    const int16_t found = WiFi.scanComplete();
    if (found == WIFI_SCAN_RUNNING) {
        return;
    }
    std::vector<improv::Network> networks;
    for (int16_t i = 0; i < found; ++i) {
        const std::string ssid = WiFi.SSID(i).c_str();
        if (ssid.empty()) {
            continue;  // hidden network
        }
        const int rssi = WiFi.RSSI(i);
        const auto same = std::ranges::find(networks, ssid, &improv::Network::ssid);
        if (same != networks.end()) {
            same->rssi = std::max(same->rssi, rssi);  // one entry per name, the strongest
            continue;
        }
        networks.push_back({ssid, rssi, auth_name(WiFi.encryptionType(i))});
    }
    WiFi.scanDelete();
    std::ranges::sort(networks, std::ranges::greater{}, &improv::Network::rssi);
    std::scoped_lock lock(mutex_);
    networks_ = std::move(networks);
    networks_at_ = now;
    have_networks_ = true;
    scan_running_ = false;
}

void WifiManager::loop(std::uint32_t now) {
    if (ap_active_) {
        dns_.processNextRequest();
    }

    std::optional<Request> request;
    bool scan = false;
    {
        std::scoped_lock lock(mutex_);
        request.swap(pending_);
        if (scan_requested_ && !scan_running_) {
            scan = true;
            scan_requested_ = false;
            scan_running_ = true;
        }
    }
    if (request) {
        ESP_LOGI(kTag, "trying network %s", request->ssid.c_str());
        WiFi.disconnect(false, false);
        trying_ = request;
        begin_station(request->ssid, request->password, now);
    }
    if (scan) {
        WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
    }
    if (scan_running_ && !scan) {
        collect_scan(now);
    }

    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected) {
        std::scoped_lock lock(mutex_);
        status_.ssid = WiFi.SSID().c_str();
        status_.ip = WiFi.localIP().toString().c_str();
        status_.rssi = WiFi.RSSI();
    }

    switch (state_) {
        case WifiState::Setup:
        case WifiState::Connecting:
        case WifiState::Offline:
            if (connected) {
                if (trying_) {
                    settings_.ssid = trying_->ssid;
                    settings_.password = trying_->password;
                    if (on_credentials_confirmed) {
                        on_credentials_confirmed(trying_->ssid, trying_->password);
                    }
                    trying_.reset();
                    std::scoped_lock lock(mutex_);
                    attempt_ = improv::ConnectResult::Succeeded;
                }
                set_state(WifiState::Connected, now);
                if (ap_active_) {
                    ap_stop_at_ = now + kApLingerMs;  // let the setup page show the new address
                }
                break;
            }
            if (state_ == WifiState::Connecting &&
                elapsed(now, attempt_started_, kAttemptTimeoutMs)) {
                if (trying_) {
                    ESP_LOGW(kTag, "could not join %s", trying_->ssid.c_str());
                    trying_.reset();
                    {
                        std::scoped_lock lock(mutex_);
                        attempt_ = improv::ConnectResult::Failed;
                    }
                    if (settings_.ssid.empty()) {
                        WiFi.disconnect(false, false);
                        set_state(WifiState::Setup, now);
                    } else {
                        begin_station(settings_.ssid, settings_.password,
                                      now);  // back to the old network
                    }
                } else {
                    set_state(WifiState::Offline, now);
                }
                break;
            }
            if (state_ == WifiState::Offline) {
                if (!ap_active_ && elapsed(now, offline_since_, kOfflineApDelayMs)) {
                    start_ap();
                }
                if (ap_active_ && elapsed(now, last_retry_, kOfflineRetryMs)) {
                    last_retry_ = now;
                    WiFi.begin(settings_.ssid.c_str(), settings_.password.c_str());
                }
            }
            break;

        case WifiState::Connected:
            if (!connected) {
                set_state(WifiState::Offline, now);
                break;
            }
            if (ap_active_ && ap_stop_at_ != 0 &&
                static_cast<std::int32_t>(now - ap_stop_at_) >= 0) {
                stop_ap();
            }
            break;
    }
}

}  // namespace tapwave::net
