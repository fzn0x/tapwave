// GPIO rules per chip, so a wrong pin is refused in the web UI instead of crashing the board.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace tapwave {

enum class Chip : std::uint8_t { Esp32, Esp32S3 };

// How a function uses its pin.
enum class PinUse : std::uint8_t {
    Output,         // driven by the ESP32: chip selects, clocks, LED, buzzer, I2S
    Input,          // read only: MISO, interrupt lines
    InputPullup,    // buttons and encoders, which rely on the internal pull-up
    Bidirectional,  // I2C data and clock
    Analog,         // battery voltage
};

struct PinProblem {
    std::string message;
    bool warning = false;  // allowed, but worth telling the user about
};

// Checks one pin against the chip. `has_psram` reserves the PSRAM pins on ESP32 WROVER modules.
// Returns nullopt when the pin is fine. -1 ("not connected") is always fine.
std::optional<PinProblem> check_pin(Chip chip, bool has_psram, int gpio, PinUse use);

}  // namespace tapwave
