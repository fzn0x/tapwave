// A passive buzzer playing RTTTL melodies without blocking the loop.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/rtttl.hpp"

namespace tapwave::io {

class Buzzer {
public:
    bool begin(int pin);
    bool active() const { return pin_ >= 0; }

    void set_enabled(bool enabled);

    // Replaces whatever is playing. Returns false for a melody that does not parse.
    bool play(std::string_view rtttl_melody);
    void stop();

    void update(std::uint32_t now_ms);

private:
    int pin_ = -1;
    bool enabled_ = true;
    std::vector<rtttl::Tone> tones_;
    std::size_t next_ = 0;
    std::uint32_t next_at_ = 0;
    bool sounding_ = false;
};

}  // namespace tapwave::io
