// Facts about the hardware the firmware runs on.
#pragma once

#include <cstdint>
#include <string>

#include "core/pins.hpp"

namespace tapwave::app {

struct Device {
    Chip chip = Chip::Esp32;
    std::string chip_model;  // "ESP32-D0WD-V3 rev 3.1"
    bool has_psram = false;
    std::uint32_t psram_size = 0;
    std::uint32_t flash_size = 0;
    std::string mac;          // "aa:bb:cc:a1:b2:c3"
    std::string id;           // "a1b2c3", the end of the MAC
    std::string environment;  // PlatformIO environment, picks the release image
    std::string version;

    static Device detect();

    std::string default_hostname() const { return "tapwave-" + id; }
    std::string setup_name() const;  // "Tapwave-B2C3": access point and BLE name
    std::string model() const;       // "Tapwave (ESP32)"
};

}  // namespace tapwave::app
