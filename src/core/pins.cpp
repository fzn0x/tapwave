#include "core/pins.hpp"

namespace tapwave {

namespace {

bool drives(PinUse use) {
    return use == PinUse::Output || use == PinUse::Bidirectional;
}

std::optional<PinProblem> check_esp32(bool has_psram, int gpio, PinUse use) {
    const bool exists = (gpio >= 0 && gpio <= 19) || (gpio >= 21 && gpio <= 23) ||
                        (gpio >= 25 && gpio <= 27) || (gpio >= 32 && gpio <= 39);
    if (!exists) {
        return PinProblem{"GPIO " + std::to_string(gpio) + " does not exist on the ESP32"};
    }
    if (gpio >= 6 && gpio <= 11) {
        return PinProblem{"GPIO 6 to 11 connect the flash chip and cannot be used"};
    }
    if (gpio == 1 || gpio == 3) {
        return PinProblem{"GPIO 1 and 3 are the USB serial port, used for logs and setup"};
    }
    if (has_psram && (gpio == 16 || gpio == 17)) {
        return PinProblem{"GPIO 16 and 17 connect the PSRAM on WROVER modules"};
    }
    if (gpio >= 34 && drives(use)) {
        return PinProblem{"GPIO 34 to 39 are input only"};
    }
    if (use == PinUse::Analog && gpio < 32) {
        return PinProblem{
            "battery sensing needs an ADC1 pin (GPIO 32 to 39); ADC2 stops "
            "working while Wi-Fi is on"};
    }
    if (use == PinUse::InputPullup && gpio >= 34) {
        return PinProblem{"GPIO 34 to 39 have no internal pull-up; add a 10 kOhm resistor to 3.3 V",
                          true};
    }
    if (gpio == 0) {
        return PinProblem{
            "GPIO 0 selects the boot mode: the board will not start normally if it "
            "is held low at power-on",
            true};
    }
    if (gpio == 2) {
        return PinProblem{"GPIO 2 must not be pulled high at power-on, or flashing over USB fails",
                          true};
    }
    if (gpio == 12) {
        return PinProblem{
            "GPIO 12 sets the flash voltage at power-on: if it is pulled high the "
            "board will not start",
            true};
    }
    return std::nullopt;
}

std::optional<PinProblem> check_esp32s3(int gpio, PinUse use) {
    const bool exists = (gpio >= 0 && gpio <= 21) || (gpio >= 26 && gpio <= 48);
    if (!exists) {
        return PinProblem{"GPIO " + std::to_string(gpio) + " does not exist on the ESP32-S3"};
    }
    if (gpio >= 26 && gpio <= 37) {
        return PinProblem{"GPIO 26 to 37 connect the flash and PSRAM chips and cannot be used"};
    }
    if (gpio == 19 || gpio == 20) {
        return PinProblem{"GPIO 19 and 20 are the USB port"};
    }
    if (use == PinUse::Analog && (gpio < 1 || gpio > 10)) {
        return PinProblem{
            "battery sensing needs an ADC1 pin (GPIO 1 to 10); ADC2 stops working "
            "while Wi-Fi is on"};
    }
    if (gpio == 43 || gpio == 44) {
        return PinProblem{"GPIO 43 and 44 are the serial port TX and RX", true};
    }
    if (gpio == 0 || gpio == 3 || gpio == 45 || gpio == 46) {
        return PinProblem{"GPIO " + std::to_string(gpio) +
                              " is a strapping pin: the board may not boot if it is pulled the "
                              "wrong way at power-on",
                          true};
    }
    return std::nullopt;
}

}  // namespace

std::optional<PinProblem> check_pin(Chip chip, bool has_psram, int gpio, PinUse use) {
    if (gpio == -1) {
        return std::nullopt;
    }
    if (gpio < 0) {
        return PinProblem{"use -1 for a function that is not connected"};
    }
    switch (chip) {
        case Chip::Esp32:
            return check_esp32(has_psram, gpio, use);
        case Chip::Esp32S3:
            return check_esp32s3(gpio, use);
    }
    return std::nullopt;
}

}  // namespace tapwave
