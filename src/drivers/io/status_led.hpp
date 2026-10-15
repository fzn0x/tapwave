// A WS2812 status LED (or a ring of them) driven by the RMT peripheral.
//
// A background pattern shows the device state; flashes show scan feedback on top of it for a
// moment. update() runs from the loop and only writes to the LEDs when the color changes.
#pragma once

#include <cstdint>
#include <vector>

#include <esp32-hal-rmt.h>

namespace tapwave::io {

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    friend bool operator==(const Color&, const Color&) = default;
};

namespace colors {
inline constexpr Color kOff{0, 0, 0};
inline constexpr Color kGreen{0, 255, 40};
inline constexpr Color kRed{255, 0, 0};
inline constexpr Color kBlue{0, 60, 255};
inline constexpr Color kAmber{255, 110, 0};
inline constexpr Color kPurple{170, 0, 255};
inline constexpr Color kWhite{255, 255, 255};
}  // namespace colors

enum class LedPattern : std::uint8_t {
    Off,
    Booting,     // blue, breathing
    Setup,       // amber, slow blink: waiting for Wi-Fi details
    Connecting,  // blue, quick blink
    Ready,       // dim green
    NoReader,    // red, slow blink
    Updating,    // purple, breathing
};

class StatusLed {
public:
    bool begin(int pin, int count, std::uint8_t brightness);
    bool active() const { return pin_ >= 0; }

    void set_enabled(bool enabled) { enabled_ = enabled; }
    void set_brightness(std::uint8_t brightness) { brightness_ = brightness; }
    void set_pattern(LedPattern pattern) { pattern_ = pattern; }

    // Shows `color` for `duration_ms`, blinking `blinks` times, over the pattern.
    void flash(Color color, std::uint32_t duration_ms, int blinks = 1);

    void update(std::uint32_t now_ms);

private:
    Color pattern_color(std::uint32_t now_ms) const;
    void show(Color color);

    int pin_ = -1;
    int count_ = 0;
    bool enabled_ = true;
    std::uint8_t brightness_ = 64;
    LedPattern pattern_ = LedPattern::Booting;
    Color flash_color_;
    std::uint32_t flash_until_ = 0;
    std::uint32_t flash_start_ = 0;
    std::uint32_t flash_duration_ = 0;
    int flash_blinks_ = 1;
    bool flash_pending_ = false;
    Color shown_{1, 1, 1};  // forces the first write
    std::vector<rmt_data_t> symbols_;
};

}  // namespace tapwave::io
