// PN532 host frames (PN532 User Manual, section 6.2).
//
//   normal frame: 00 00 FF LEN LCS TFI PD0..PDn DCS 00
//   ACK:          00 00 FF 00 FF 00
//   NACK:         00 00 FF FF 00 00
//   error frame:  00 00 FF 01 FF 7F 81 00
//
// LEN counts TFI and data; LEN + LCS and TFI + data + DCS are both 0 modulo 256.
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <optional>

#include "core/bytes.hpp"

namespace tapwave::nfc::pn532 {

inline constexpr std::uint8_t kHostToPn532 = 0xD4;
inline constexpr std::uint8_t kPn532ToHost = 0xD5;

inline constexpr std::array<std::uint8_t, 6> kAck{0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
inline constexpr std::array<std::uint8_t, 6> kNack{0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};

// Command codes used by Tapwave.
inline constexpr std::uint8_t kGetFirmwareVersion = 0x02;
inline constexpr std::uint8_t kSamConfiguration = 0x14;
inline constexpr std::uint8_t kRfConfiguration = 0x32;
inline constexpr std::uint8_t kInDataExchange = 0x40;
inline constexpr std::uint8_t kInCommunicateThru = 0x42;
inline constexpr std::uint8_t kInListPassiveTarget = 0x4A;
inline constexpr std::uint8_t kInRelease = 0x52;

// A command frame from the host.
Bytes build_command(std::uint8_t command, ByteView params);

enum class FrameKind : std::uint8_t { Ack, Nack, ErrorFrame, Data };

struct Frame {
    FrameKind kind = FrameKind::Data;
    std::uint8_t code = 0;  // response code (command + 1) for data frames
    Bytes data;             // bytes after the response code
};

enum class FrameError : std::uint8_t {
    Incomplete,      // more bytes are needed
    NoStartCode,     // no 00 FF in the data
    BadLength,       // LEN and LCS do not add up
    BadChecksum,     // data and DCS do not add up
    WrongDirection,  // TFI is not D5
    Extended,        // extended frames are not used by Tapwave
};

// Parses the first frame in `bytes`, skipping leading zero bytes.
std::expected<Frame, FrameError> parse_frame(ByteView bytes);

// For tests and simulators: a frame as the PN532 would send it.
Bytes build_response(std::uint8_t code, ByteView data);

// Total frame length once the header (through LEN) is known, or nullopt if the header is not
// complete. Transports use it to know how many bytes to read.
std::optional<std::size_t> frame_length(ByteView header);

}  // namespace tapwave::nfc::pn532
