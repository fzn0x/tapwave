#include "drivers/io/status_led.hpp"

#include <Arduino.h>

#include <cmath>

namespace tapwave::io {

namespace {

// 10 MHz RMT clock: 100 ns per tick. WS2812 bits: 0 = 400 ns high, 850 ns low;
// 1 = 800 ns high, 450 ns low.
constexpr std::uint32_t kRmtFrequency = 10000000;

Color scale(Color c, std::uint32_t factor_256) {
    return {static_cast<std::uint8_t>(c.r * factor_256 / 256),
            static_cast<std::uint8_t>(c.g * factor_256 / 256),
            static_cast<std::uint8_t>(c.b * factor_256 / 256)};
}

// 0..256 triangle wave over `period_ms`, for breathing patterns.
std::uint32_t breathe(std::uint32_t now_ms, std::uint32_t period_ms) {
    const std::uint32_t phase = now_ms % period_ms;
    const std::uint32_t half = period_ms / 2;
    const std::uint32_t level =
        phase < half ? phase * 256 / half : (period_ms - phase) * 256 / half;
    return 24 + level * 232 / 256;  // never fully dark
}

bool blink(std::uint32_t now_ms, std::uint32_t period_ms) {
    return (now_ms % period_ms) < period_ms / 2;
}

}  // namespace

bool StatusLed::begin(int pin, int count, std::uint8_t brightness) {
    if (pin < 0 || count < 1) {
        return false;
    }
    if (!rmtInit(pin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, kRmtFrequency)) {
        return false;
    }
    pin_ = pin;
    count_ = count;
    brightness_ = brightness;
    symbols_.resize(static_cast<std::size_t>(count) * 24);
    show(colors::kOff);
    return true;
}

void StatusLed::flash(Color color, std::uint32_t duration_ms, int blinks) {
    flash_color_ = color;
    flash_duration_ = duration_ms;
    flash_blinks_ = blinks < 1 ? 1 : blinks;
    flash_pending_ = true;
}

Color StatusLed::pattern_color(std::uint32_t now_ms) const {
    switch (pattern_) {
        case LedPattern::Off:
            return colors::kOff;
        case LedPattern::Booting:
            return scale(colors::kBlue, breathe(now_ms, 2000));
        case LedPattern::Setup:
            return blink(now_ms, 1600) ? colors::kAmber : colors::kOff;
        case LedPattern::Connecting:
            return blink(now_ms, 500) ? colors::kBlue : colors::kOff;
        case LedPattern::Ready:
            return scale(colors::kGreen, 40);
        case LedPattern::NoReader:
            return blink(now_ms, 2000) ? colors::kRed : colors::kOff;
        case LedPattern::Updating:
            return scale(colors::kPurple, breathe(now_ms, 1200));
    }
    return colors::kOff;
}

void StatusLed::update(std::uint32_t now_ms) {
    if (pin_ < 0) {
        return;
    }
    if (flash_pending_) {
        flash_pending_ = false;
        flash_start_ = now_ms;
        flash_until_ = now_ms + flash_duration_;
    }
    Color color = pattern_color(now_ms);
    if (static_cast<std::int32_t>(flash_until_ - now_ms) > 0) {
        const std::uint32_t slot = flash_duration_ / static_cast<std::uint32_t>(flash_blinks_);
        const std::uint32_t into = (now_ms - flash_start_) % (slot == 0 ? 1 : slot);
        color = (flash_blinks_ == 1 || into < slot * 2 / 3) ? flash_color_ : colors::kOff;
    }
    if (!enabled_) {
        color = colors::kOff;
    }
    show(scale(color, brightness_ + 1u));
}

void StatusLed::show(Color color) {
    if (color == shown_) {
        return;
    }
    shown_ = color;
    // WS2812 expects green, red, blue, most significant bit first.
    const std::uint8_t grb[3] = {color.g, color.r, color.b};
    std::size_t i = 0;
    for (int led = 0; led < count_; ++led) {
        for (std::uint8_t channel : grb) {
            for (int bit = 7; bit >= 0; --bit) {
                const bool one = (channel >> bit) & 1;
                rmt_data_t& s = symbols_[i++];
                s.level0 = 1;
                s.duration0 = one ? 8 : 4;
                s.level1 = 0;
                s.duration1 = one ? 4 : 8;
            }
        }
    }
    rmtWrite(pin_, symbols_.data(), symbols_.size(), RMT_WAIT_FOR_EVER);
}

}  // namespace tapwave::io
