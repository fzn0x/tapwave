// Byte buffers and hex text, shared by every module.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tapwave {

using Bytes = std::vector<std::uint8_t>;
using ByteView = std::span<const std::uint8_t>;

// Formats bytes as hex. With a separator, bytes are joined by it ("04-A2-1B").
std::string to_hex(ByteView bytes, std::string_view separator = {}, bool uppercase = true);

// Parses hex text. Accepts upper or lower case and ignores ':', '-' and ' ' between bytes.
// Returns nullopt for odd digit counts or any other character.
std::optional<Bytes> from_hex(std::string_view text);

inline ByteView as_bytes(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

inline std::string to_string(ByteView bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

}  // namespace tapwave
