// Improv Wi-Fi: the open provisioning protocol used by ESPHome, ESP Web Tools and Home
// Assistant (https://www.improv-wifi.com). One codec serves both transports:
//
//   Serial: "IMPROV" 0x01 <type> <length> <data...> <checksum>, checksum = sum of all
//           preceding bytes, keep the low byte.
//   BLE:    the RPC characteristic carries <command> <length> <data...> <checksum>, checksum
//           = sum of the preceding bytes, keep the low byte; writes may arrive in pieces.
//
// RPC data and results are lists of length-prefixed UTF-8 strings.
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/bytes.hpp"

namespace tapwave::improv {

enum class State : std::uint8_t {
    Stopped = 0x00,
    AuthorizationRequired = 0x01,  // BLE only
    Authorized = 0x02,             // ready to receive credentials
    Provisioning = 0x03,
    Provisioned = 0x04,
};

enum class Error : std::uint8_t {
    None = 0x00,
    InvalidRpc = 0x01,
    UnknownRpc = 0x02,
    UnableToConnect = 0x03,
    NotAuthorized = 0x04,
    BadHostname = 0x05,
    Unknown = 0xFF,
};

enum class Command : std::uint8_t {
    WifiSettings = 0x01,
    CurrentStateOrIdentify = 0x02,  // serial: request current state; BLE: identify
    DeviceInfo = 0x03,
    WifiNetworks = 0x04,
    Hostname = 0x05,
    DeviceName = 0x06,
    NetworkState = 0x07,
};

// BLE capability bits for the capabilities characteristic and advertisement.
inline constexpr std::uint8_t kCapabilityIdentify = 0x01;
inline constexpr std::uint8_t kCapabilityDeviceInfo = 0x02;
inline constexpr std::uint8_t kCapabilityWifiScan = 0x04;
inline constexpr std::uint8_t kCapabilityHostname = 0x08;

// Network state flags (command 0x07).
inline constexpr std::uint8_t kNetworkOnline = 0x01;
inline constexpr std::uint8_t kNetworkWifi = 0x02;

struct Rpc {
    std::uint8_t command = 0;
    std::vector<std::string> args;  // WifiSettings: {ssid, password}; Hostname: {} or {name}

    friend bool operator==(const Rpc&, const Rpc&) = default;
};

// Parses <command> <length> <data> [<checksum>]. Errors map to the Improv error codes.
std::expected<Rpc, Error> parse_rpc(ByteView frame, bool with_checksum);

// <command> <length> <strings...> [<checksum>]
Bytes build_rpc_result(std::uint8_t command, std::span<const std::string> strings,
                       bool with_checksum);

// ---- Serial framing ------------------------------------------------------------------------

enum class PacketType : std::uint8_t {
    CurrentState = 0x01,
    ErrorState = 0x02,
    Rpc = 0x03,
    RpcResult = 0x04,
};

inline constexpr std::uint8_t kSerialVersion = 0x01;

// A complete serial packet, followed by a newline so serial monitors stay readable.
Bytes serial_packet(PacketType type, ByteView data);

// Finds Improv packets in a serial byte stream. Bytes that are not part of a packet are
// ignored, so log output and Improv share the port.
class SerialParser {
public:
    struct Packet {
        PacketType type;
        Bytes data;
    };

    // Feeds one byte. Returns a packet when one completes with a valid checksum, or the error
    // to report when a packet completes with a bad checksum.
    std::optional<std::expected<Packet, Error>> feed(std::uint8_t byte);

private:
    Bytes buffer_;
};

// ---- BLE -----------------------------------------------------------------------------------

// Collects RPC writes that BLE splits into 20-byte pieces.
class BleRpcAssembler {
public:
    // Returns a parsed RPC (or its error) once the frame is complete, otherwise nullopt.
    std::optional<std::expected<Rpc, Error>> feed(ByteView piece);

private:
    Bytes buffer_;
};

// Service data for the advertisement: state, capabilities, four reserved zero bytes.
Bytes advertisement_service_data(State state, std::uint8_t capabilities);

}  // namespace tapwave::improv
