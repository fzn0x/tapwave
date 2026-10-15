// RTTTL (Ring Tone Text Transfer Language) melodies for the buzzer, the format tagreader users
// already send from Home Assistant: "name:d=16,o=5,b=100:c,g,b".
#pragma once

#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

namespace tapwave::rtttl {

struct Tone {
    std::uint16_t frequency_hz = 0;  // 0 is a rest
    std::uint16_t duration_ms = 0;

    friend bool operator==(const Tone&, const Tone&) = default;
};

enum class Error : std::uint8_t {
    Format,   // missing sections or a malformed default
    Note,     // a note that cannot be read
    TooLong,  // more notes than a buzzer should play
};

inline constexpr std::size_t kMaxNotes = 256;

std::expected<std::vector<Tone>, Error> parse(std::string_view melody);

// Melodies for scan feedback. kSuccess and kWrite are the ones adonno/tagreader plays.
inline constexpr std::string_view kSuccess = "success:d=16,o=5,b=100:c,g,b";
inline constexpr std::string_view kError = "error:d=8,o=5,b=100:b,p,b";
inline constexpr std::string_view kWrite = "write:d=16,o=5,b=100:b";
inline constexpr std::string_view kIdentify = "identify:d=8,o=6,b=180:c,e,g,c7";

}  // namespace tapwave::rtttl
