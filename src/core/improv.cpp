#include "core/improv.hpp"

#include <numeric>

namespace tapwave::improv {

namespace {

constexpr std::string_view kHeader = "IMPROV";

std::uint8_t checksum(ByteView bytes) {
    return static_cast<std::uint8_t>(std::accumulate(bytes.begin(), bytes.end(), 0u));
}

}  // namespace

std::expected<Rpc, Error> parse_rpc(ByteView frame, bool with_checksum) {
    if (frame.size() < 2 + (with_checksum ? 1u : 0u)) {
        return std::unexpected(Error::InvalidRpc);
    }
    const std::size_t length = frame[1];
    if (frame.size() != 2 + length + (with_checksum ? 1u : 0u)) {
        return std::unexpected(Error::InvalidRpc);
    }
    if (with_checksum && checksum(frame.first(2 + length)) != frame[2 + length]) {
        return std::unexpected(Error::InvalidRpc);
    }

    Rpc rpc;
    rpc.command = frame[0];
    const ByteView data = frame.subspan(2, length);
    std::size_t pos = 0;
    while (pos < data.size()) {
        const std::size_t size = data[pos++];
        if (pos + size > data.size()) {
            return std::unexpected(Error::InvalidRpc);
        }
        rpc.args.push_back(to_string(data.subspan(pos, size)));
        pos += size;
    }

    switch (static_cast<Command>(rpc.command)) {
        case Command::WifiSettings:
            if (rpc.args.size() != 2 || rpc.args[0].empty()) {
                return std::unexpected(Error::InvalidRpc);
            }
            break;
        case Command::Hostname:
        case Command::DeviceName:
            if (rpc.args.size() > 1) {
                return std::unexpected(Error::InvalidRpc);
            }
            break;
        case Command::CurrentStateOrIdentify:
        case Command::DeviceInfo:
        case Command::WifiNetworks:
        case Command::NetworkState:
            break;
        default:
            return std::unexpected(Error::UnknownRpc);
    }
    return rpc;
}

Bytes build_rpc_result(std::uint8_t command, std::span<const std::string> strings,
                       bool with_checksum) {
    Bytes out{command, 0x00};
    for (const std::string& s : strings) {
        const std::size_t size = std::min<std::size_t>(s.size(), 255);
        out.push_back(static_cast<std::uint8_t>(size));
        out.insert(out.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(size));
    }
    out[1] = static_cast<std::uint8_t>(out.size() - 2);
    if (with_checksum) {
        out.push_back(checksum(out));
    }
    return out;
}

Bytes serial_packet(PacketType type, ByteView data) {
    Bytes out(kHeader.begin(), kHeader.end());
    out.push_back(kSerialVersion);
    out.push_back(static_cast<std::uint8_t>(type));
    out.push_back(static_cast<std::uint8_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    out.push_back(checksum(out));
    out.push_back('\n');
    return out;
}

std::optional<std::expected<SerialParser::Packet, Error>> SerialParser::feed(std::uint8_t byte) {
    const std::size_t pos = buffer_.size();
    if (pos < kHeader.size()) {
        if (byte != static_cast<std::uint8_t>(kHeader[pos])) {
            buffer_.clear();
            // The byte may start a new header.
            if (byte == static_cast<std::uint8_t>(kHeader[0])) {
                buffer_.push_back(byte);
            }
            return std::nullopt;
        }
        buffer_.push_back(byte);
        return std::nullopt;
    }
    if (pos == kHeader.size() && byte != kSerialVersion) {
        buffer_.clear();
        return std::nullopt;
    }
    buffer_.push_back(byte);
    if (buffer_.size() < 9) {
        return std::nullopt;
    }
    const std::size_t length = buffer_[8];
    if (buffer_.size() < 9 + length + 1) {
        return std::nullopt;
    }

    Bytes packet;
    packet.swap(buffer_);
    const std::uint8_t expected = checksum(ByteView(packet).first(packet.size() - 1));
    if (expected != packet.back()) {
        return std::unexpected(Error::InvalidRpc);
    }
    return Packet{static_cast<PacketType>(packet[7]),
                  Bytes(packet.begin() + 9, packet.begin() + 9 + static_cast<long>(length))};
}

std::optional<std::expected<Rpc, Error>> BleRpcAssembler::feed(ByteView piece) {
    buffer_.insert(buffer_.end(), piece.begin(), piece.end());
    if (buffer_.size() < 2) {
        return std::nullopt;
    }
    const std::size_t total = 2 + std::size_t{buffer_[1]} + 1;
    if (buffer_.size() < total) {
        return std::nullopt;
    }
    Bytes frame;
    frame.swap(buffer_);
    if (frame.size() > total) {
        return std::unexpected(Error::InvalidRpc);  // more bytes than the frame announced
    }
    return parse_rpc(frame, /*with_checksum=*/true);
}

Bytes advertisement_service_data(State state, std::uint8_t capabilities) {
    return {static_cast<std::uint8_t>(state), capabilities, 0x00, 0x00, 0x00, 0x00};
}

}  // namespace tapwave::improv
