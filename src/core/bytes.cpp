#include "core/bytes.hpp"

namespace tapwave {

namespace {

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}  // namespace

std::string to_hex(ByteView bytes, std::string_view separator, bool uppercase) {
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * (2 + separator.size()));
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i > 0) {
            out.append(separator);
        }
        out.push_back(digits[bytes[i] >> 4]);
        out.push_back(digits[bytes[i] & 0x0F]);
    }
    return out;
}

std::optional<Bytes> from_hex(std::string_view text) {
    Bytes out;
    int high = -1;
    for (char c : text) {
        if (c == ':' || c == '-' || c == ' ') {
            if (high >= 0) {
                return std::nullopt;  // a separator inside a byte
            }
            continue;
        }
        const int value = hex_value(c);
        if (value < 0) {
            return std::nullopt;
        }
        if (high < 0) {
            high = value;
        } else {
            out.push_back(static_cast<std::uint8_t>((high << 4) | value));
            high = -1;
        }
    }
    if (high >= 0) {
        return std::nullopt;
    }
    return out;
}

}  // namespace tapwave
