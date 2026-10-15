#include "drivers/nfc/common/pn532_frame.hpp"

#include <numeric>
#include <optional>

namespace tapwave::nfc::pn532 {

namespace {

std::uint8_t sum(ByteView bytes) {
    return static_cast<std::uint8_t>(std::accumulate(bytes.begin(), bytes.end(), 0u));
}

Bytes frame_with(std::uint8_t tfi, std::uint8_t code, ByteView data) {
    Bytes body{tfi, code};
    body.insert(body.end(), data.begin(), data.end());
    const auto length = static_cast<std::uint8_t>(body.size());
    Bytes out{0x00, 0x00, 0xFF, length, static_cast<std::uint8_t>(0x100 - length)};
    out.insert(out.end(), body.begin(), body.end());
    out.push_back(static_cast<std::uint8_t>(0x100 - sum(body)));
    out.push_back(0x00);
    return out;
}

// Index of the byte after the 00 FF start code, skipping leading zeros.
std::optional<std::size_t> start_of(ByteView bytes) {
    std::size_t i = 0;
    while (i < bytes.size() && bytes[i] == 0x00) {
        ++i;
    }
    if (i == 0 || i >= bytes.size() || bytes[i] != 0xFF) {
        return std::nullopt;
    }
    return i + 1;
}

}  // namespace

Bytes build_command(std::uint8_t command, ByteView params) {
    return frame_with(kHostToPn532, command, params);
}

Bytes build_response(std::uint8_t code, ByteView data) {
    return frame_with(kPn532ToHost, code, data);
}

std::optional<std::size_t> frame_length(ByteView header) {
    const auto start = start_of(header);
    if (!start || *start + 2 > header.size()) {
        return std::nullopt;
    }
    const std::uint8_t length = header[*start];
    const std::uint8_t lcs = header[*start + 1];
    if ((length == 0x00 && lcs == 0xFF) || (length == 0xFF && lcs == 0x00)) {
        return *start + 3;  // ACK or NACK: LEN, LCS, postamble
    }
    return *start + 2 + length + 2;  // LEN, LCS, body, DCS, postamble
}

std::expected<Frame, FrameError> parse_frame(ByteView bytes) {
    const auto start = start_of(bytes);
    if (!start) {
        // All zeros so far means the frame has not started yet.
        for (std::uint8_t b : bytes) {
            if (b != 0x00) {
                return std::unexpected(FrameError::NoStartCode);
            }
        }
        return std::unexpected(FrameError::Incomplete);
    }
    std::size_t i = *start;
    if (i + 2 > bytes.size()) {
        return std::unexpected(FrameError::Incomplete);
    }
    const std::uint8_t length = bytes[i];
    const std::uint8_t lcs = bytes[i + 1];
    i += 2;
    if (length == 0x00 && lcs == 0xFF) {
        return Frame{FrameKind::Ack, 0, {}};
    }
    if (length == 0xFF && lcs == 0x00) {
        return Frame{FrameKind::Nack, 0, {}};
    }
    if (length == 0xFF && lcs == 0xFF) {
        return std::unexpected(FrameError::Extended);
    }
    if (static_cast<std::uint8_t>(length + lcs) != 0) {
        return std::unexpected(FrameError::BadLength);
    }
    if (i + length + 1 > bytes.size()) {
        return std::unexpected(FrameError::Incomplete);
    }
    const ByteView body = bytes.subspan(i, length);
    const std::uint8_t dcs = bytes[i + length];
    if (static_cast<std::uint8_t>(sum(body) + dcs) != 0) {
        return std::unexpected(FrameError::BadChecksum);
    }
    if (length == 1 && body[0] == 0x7F) {
        return Frame{FrameKind::ErrorFrame, 0, {}};
    }
    if (length < 2) {
        return std::unexpected(FrameError::BadLength);
    }
    if (body[0] != kPn532ToHost) {
        return std::unexpected(FrameError::WrongDirection);
    }
    return Frame{FrameKind::Data, body[1], Bytes(body.begin() + 2, body.end())};
}

}  // namespace tapwave::nfc::pn532
