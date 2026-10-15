// ISO 14443-A building blocks that do not depend on a reader chip.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "drivers/nfc/common/nfc_types.hpp"

namespace tapwave::nfc::iso14443a {

inline constexpr std::uint8_t kReqA = 0x26;
inline constexpr std::uint8_t kWupA = 0x52;
inline constexpr std::uint8_t kHltA = 0x50;
inline constexpr std::uint8_t kCascadeTag = 0x88;
inline constexpr std::array<std::uint8_t, 3> kSelectCommands{0x93, 0x95, 0x97};

inline constexpr std::uint8_t kMifareRead = 0x30;
inline constexpr std::uint8_t kUltralightWrite = 0xA2;
inline constexpr std::uint8_t kGetVersion = 0x60;
inline constexpr std::uint8_t kAck = 0x0A;  // four-bit ACK to writes

// CRC_A from ISO 14443-3 annex B, sent low byte first.
std::uint16_t crc_a(ByteView data);

// Appends CRC_A to a command.
Bytes with_crc(ByteView data);

// Checks and strips the CRC_A at the end of a response.
std::optional<Bytes> check_crc(ByteView response);

// The tag family a SAK suggests. Type 2 tags all answer SAK 0x00; GET_VERSION refines them.
TagFamily family_from_sak(std::uint8_t sak);

// Refines a Type 2 family from a GET_VERSION answer (8 bytes).
TagFamily family_from_version(ByteView version);

// One cascade level of the anticollision loop: CT or UID bytes plus BCC.
using CascadeLevel = std::array<std::uint8_t, 5>;

bool bcc_valid(const CascadeLevel& level);

// The SELECT frame for a cascade level (0, 1 or 2), CRC included.
Bytes select_frame(int cascade, const CascadeLevel& level);

// True when the SAK says the UID continues in the next cascade level.
constexpr bool uid_incomplete(std::uint8_t sak) {
    return (sak & 0x04) != 0;
}

// The UID bytes a cascade level contributes: all four, or three after the cascade tag.
void append_uid_part(Bytes& uid, const CascadeLevel& level, bool more_levels);

}  // namespace tapwave::nfc::iso14443a
