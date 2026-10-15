// Improv Wi-Fi over Bluetooth LE, only while the device has no Wi-Fi. Home Assistant's
// "Improv via BLE" integration finds it and offers to set it up.
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include "core/improv.hpp"
#include "core/improv_session.hpp"

class BLECharacteristic;
class BLEServer;

namespace tapwave::net {

class ImprovBle {
public:
    bool begin(const std::string& device_name, improv::DeviceHooks hooks);
    void loop();
    // Shuts BLE down and gives its memory back. Safe to call when not running.
    void stop();
    bool running() const { return running_; }

    // Called from the BLE stack's task.
    void on_rpc_write(const std::uint8_t* data, std::size_t size);

private:
    void advertise();
    void notify_state(improv::State state);

    bool running_ = false;
    std::string name_;
    std::unique_ptr<improv::Session> session_;
    improv::BleRpcAssembler assembler_;
    improv::State advertised_ = improv::State::Stopped;
    BLEServer* server_ = nullptr;
    BLECharacteristic* state_ = nullptr;
    BLECharacteristic* error_ = nullptr;
    BLECharacteristic* rpc_ = nullptr;
    BLECharacteristic* result_ = nullptr;
    BLECharacteristic* capabilities_ = nullptr;

    std::mutex mutex_;
    std::deque<Bytes> writes_;  // RPC writes waiting for the loop task
};

}  // namespace tapwave::net
