#include "drivers/nfc/common/iso14443a.hpp"

namespace tapwave::nfc::iso14443a {

std::uint16_t crc_a(ByteView data) {
    std::uint16_t crc = 0x6363;
    for (std::uint8_t byte : data) {
        byte = static_cast<std::uint8_t>(byte ^ (crc & 0xFF));
        byte = static_cast<std::uint8_t>(byte ^ (byte << 4));
        crc = static_cast<std::uint16_t>((crc >> 8) ^ (byte << 8) ^ (byte << 3) ^ (byte >> 4));
    }
    return crc;
}

Bytes with_crc(ByteView data) {
    Bytes out(data.begin(), data.end());
    const std::uint16_t crc = crc_a(data);
    out.push_back(static_cast<std::uint8_t>(crc & 0xFF));
    out.push_back(static_cast<std::uint8_t>(crc >> 8));
    return out;
}

std::optional<Bytes> check_crc(ByteView response) {
    if (response.size() < 3) {
        return std::nullopt;
    }
    const ByteView body = response.first(response.size() - 2);
    const std::uint16_t crc = crc_a(body);
    if (response[response.size() - 2] != (crc & 0xFF) || response.back() != (crc >> 8)) {
        return std::nullopt;
    }
    return Bytes(body.begin(), body.end());
}

TagFamily family_from_sak(std::uint8_t sak) {
    switch (sak) {
        case 0x00:
            return TagFamily::Type2;
        case 0x09:
            return TagFamily::MifareClassicMini;
        case 0x08:
        case 0x88:  // Infineon
        case 0x28:  // SmartMX with Classic 1K emulation
            return TagFamily::MifareClassic1k;
        case 0x18:
        case 0x38:  // SmartMX with Classic 4K emulation
            return TagFamily::MifareClassic4k;
        default:
            return (sak & 0x20) ? TagFamily::Iso14443_4 : TagFamily::Unknown;
    }
}

TagFamily family_from_version(ByteView v) {
    // Header 0x00, vendor, product type, subtype, major, minor, storage size, protocol.
    if (v.size() != 8 || v[1] != 0x04) {
        return TagFamily::Type2;
    }
    const std::uint8_t type = v[2];
    const std::uint8_t storage = v[6];
    if (type == 0x04) {  // NTAG
        switch (storage) {
            case 0x0B:
                return TagFamily::Ntag210;
            case 0x0E:
                return TagFamily::Ntag212;
            case 0x0F:
                return TagFamily::Ntag213;
            case 0x11:
                return TagFamily::Ntag215;
            case 0x13:
                return TagFamily::Ntag216;
            default:
                return TagFamily::Type2;
        }
    }
    if (type == 0x03) {  // MIFARE Ultralight EV1 and later
        return TagFamily::UltralightEv1;
    }
    return TagFamily::Type2;
}

bool bcc_valid(const CascadeLevel& level) {
    return (level[0] ^ level[1] ^ level[2] ^ level[3]) == level[4];
}

Bytes select_frame(int cascade, const CascadeLevel& level) {
    const Bytes frame{kSelectCommands[static_cast<std::size_t>(cascade)],
                      0x70,
                      level[0],
                      level[1],
                      level[2],
                      level[3],
                      level[4]};
    return with_crc(frame);
}

void append_uid_part(Bytes& uid, const CascadeLevel& level, bool more_levels) {
    if (more_levels) {
        uid.insert(uid.end(), level.begin() + 1, level.begin() + 4);  // skip the cascade tag
    } else {
        uid.insert(uid.end(), level.begin(), level.begin() + 4);
    }
}

}  // namespace tapwave::nfc::iso14443a
