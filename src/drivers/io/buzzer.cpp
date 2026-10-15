#include "drivers/io/buzzer.hpp"

#include <Arduino.h>

namespace tapwave::io {

bool Buzzer::begin(int pin) {
    if (pin < 0) {
        return false;
    }
    pin_ = pin;
    pinMode(pin_, OUTPUT);
    digitalWrite(pin_, LOW);
    return true;
}

void Buzzer::set_enabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        stop();
    }
}

bool Buzzer::play(std::string_view rtttl_melody) {
    auto tones = rtttl::parse(rtttl_melody);
    if (!tones) {
        return false;
    }
    if (pin_ < 0 || !enabled_) {
        return true;
    }
    stop();
    tones_ = std::move(*tones);
    next_ = 0;
    next_at_ = millis();
    return true;
}

void Buzzer::stop() {
    if (pin_ >= 0 && sounding_) {
        noTone(pin_);
    }
    sounding_ = false;
    tones_.clear();
    next_ = 0;
}

void Buzzer::update(std::uint32_t now_ms) {
    if (pin_ < 0 || tones_.empty()) {
        return;
    }
    if (static_cast<std::int32_t>(now_ms - next_at_) < 0) {
        return;
    }
    if (next_ >= tones_.size()) {
        stop();
        return;
    }
    const rtttl::Tone& t = tones_[next_++];
    if (t.frequency_hz == 0) {
        if (sounding_) {
            noTone(pin_);
            sounding_ = false;
        }
    } else {
        // Each note sounds for its whole length; tone() switches frequency without a gap.
        tone(pin_, t.frequency_hz);
        sounding_ = true;
    }
    next_at_ = now_ms + t.duration_ms;
}

}  // namespace tapwave::io
