// Tag UIDs and the text form Home Assistant sees.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "core/bytes.hpp"

namespace tapwave {

// A tag UID: 4, 7 or 10 bytes for ISO 14443-A, 8 bytes for ISO 15693.
class Uid {
public:
    static constexpr std::size_t kMaxSize = 10;

    Uid() = default;

    // Returns nullopt for an empty UID or one longer than kMaxSize.
    static std::optional<Uid> from(ByteView bytes);

    // Parses the text form ("04-A2-1B-3C"); separators are optional.
    static std::optional<Uid> parse(std::string_view text);

    ByteView bytes() const { return {bytes_.data(), size_}; }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    // Uppercase hex bytes joined by dashes: the format ESPHome and adonno/tagreader report,
    // so tags registered in Home Assistant by those projects keep their IDs.
    std::string to_string() const;

    friend bool operator==(const Uid& a, const Uid& b) {
        return std::ranges::equal(a.bytes(), b.bytes());
    }

private:
    std::array<std::uint8_t, kMaxSize> bytes_{};
    std::uint8_t size_ = 0;
};

}  // namespace tapwave
