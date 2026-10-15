#include "core/uid.hpp"

namespace tapwave {

std::optional<Uid> Uid::from(ByteView bytes) {
    if (bytes.empty() || bytes.size() > kMaxSize) {
        return std::nullopt;
    }
    Uid uid;
    std::ranges::copy(bytes, uid.bytes_.begin());
    uid.size_ = static_cast<std::uint8_t>(bytes.size());
    return uid;
}

std::optional<Uid> Uid::parse(std::string_view text) {
    const auto bytes = from_hex(text);
    if (!bytes) {
        return std::nullopt;
    }
    return from(*bytes);
}

std::string Uid::to_string() const {
    return to_hex(bytes(), "-");
}

}  // namespace tapwave
