// The Improv Wi-Fi conversation, shared by the serial and BLE transports. The transport turns
// bytes into RPCs and RPC answers back into bytes; the session decides what to answer and talks
// to the device through hooks, so it can be tested without hardware.
//
// Tapwave starts in the authorized state on both transports (no button press needed), the same
// as an ESPHome device without an authorizer.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/improv.hpp"

namespace tapwave::improv {

enum class Transport : std::uint8_t { Serial, Ble };

struct Network {
    std::string ssid;
    int rssi = 0;
    std::string auth;  // "WPA2", "WPA3", "NO", ...
};

enum class ConnectResult : std::uint8_t { Pending, Succeeded, Failed };

struct DeviceHooks {
    std::function<std::uint32_t()> now_ms;
    std::function<void(const std::string& ssid, const std::string& password)> connect;
    std::function<ConnectResult()> connect_result;  // of the last connect()
    std::function<bool()> online;
    std::function<std::string()> device_url;  // "http://192.168.1.50/"
    std::function<void()> identify;
    std::function<std::vector<std::string>()> device_info;
    std::function<void()> start_scan;
    std::function<std::optional<std::vector<Network>>()> scan_results;  // nullopt while scanning
    std::function<std::string()> hostname;
    std::function<bool(const std::string&)> set_hostname;
    std::function<std::string()> device_name;
    std::function<bool(const std::string&)> set_device_name;
};

struct Output {
    std::function<void(State)> state;
    std::function<void(Error)> error;
    std::function<void(std::uint8_t command, const std::vector<std::string>& strings)> result;
};

class Session {
public:
    static constexpr std::uint32_t kConnectTimeoutMs = 30000;

    Session(Transport transport, DeviceHooks hooks, Output output);

    // Provisioned devices start in that state, others ready for credentials.
    void begin(bool provisioned);

    void handle(const Rpc& rpc);
    void report(Error error);  // a packet the transport could not parse
    void loop();

    State state() const { return state_; }

private:
    void set_state(State state);
    void set_error(Error error);
    void send_provisioned_result();

    Transport transport_;
    DeviceHooks hooks_;
    Output out_;
    State state_ = State::Authorized;
    Error error_ = Error::None;
    bool connecting_ = false;
    std::uint32_t connect_deadline_ = 0;
    bool scanning_ = false;
};

}  // namespace tapwave::improv
