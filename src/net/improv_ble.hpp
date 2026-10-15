// Improv Wi-Fi over Bluetooth LE, only while the device has no Wi-Fi. Home Assistant's
// "Improv via BLE" integration finds it and offers to set it up.
//
// Built on NimBLE-Arduino: its BLE host costs about 240 KB of flash where Bluedroid's costs about
// 760 KB, which is what lets the full firmware fit the 1.9 MB OTA slots of 4 MB boards
// (ARCHITECTURE.md section 18).
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include "core/improv.hpp"
#include "core/improv_session.hpp"

class NimBLECharacteristic;

namespace tapwave::net {

class ImprovBle {
public:
    bool begin(const std::string& device_name, improv::DeviceHooks hooks);
    void loop();
    // Shuts BLE down and gives its memory back. Safe to call when not running.
    void stop();
    bool running() const { return running_; }

    // Called from the BLE host task.
    void on_rpc_write(const std::uint8_t* data, std::size_t size);

private:
    void advertise();

    bool running_ = false;
    std::string name_;
    std::unique_ptr<improv::Session> session_;
    improv::BleRpcAssembler assembler_;
    improv::State advertised_ = improv::State::Stopped;
    NimBLECharacteristic* state_ = nullptr;
    NimBLECharacteristic* error_ = nullptr;
    NimBLECharacteristic* result_ = nullptr;

    std::mutex mutex_;
    std::deque<Bytes> writes_;  // RPC writes waiting for the loop task
};

}  // namespace tapwave::net
