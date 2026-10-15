#include "core/tag_memory.hpp"

namespace tapwave::tag_memory {

namespace {

constexpr std::uint8_t kNdefMagic = 0xE1;

constexpr std::uint8_t kTlvNull = 0x00;
constexpr std::uint8_t kTlvNdef = 0x03;
constexpr std::uint8_t kTlvTerminator = 0xFE;

// MAD entries are two bytes: application code, then function cluster code.
constexpr std::uint8_t kNdefApplicationCode = 0x03;
constexpr std::uint8_t kNdefFunctionCluster = 0xE1;

}  // namespace

std::optional<CapabilityContainer> CapabilityContainer::parse(ByteView page) {
    if (page.size() < kPageSize || page[0] != kNdefMagic) {
        return std::nullopt;
    }
    return CapabilityContainer{page[0], page[1], page[2], page[3]};
}

TlvSearch find_ndef_tlv(ByteView data, std::size_t capacity) {
    std::size_t pos = 0;
    while (true) {
        if (pos >= capacity) {
            return TlvError::NoNdef;
        }
        if (pos >= data.size()) {
            return NeedMore{pos + 1};
        }
        const std::uint8_t type = data[pos];
        if (type == kTlvNull) {
            ++pos;
            continue;
        }
        if (type == kTlvTerminator) {
            return TlvError::NoNdef;
        }
        if (pos + 1 >= data.size()) {
            return NeedMore{pos + 2};
        }
        std::size_t length = data[pos + 1];
        std::size_t header = 2;
        if (length == 0xFF) {  // three-byte length format
            if (pos + 3 >= data.size()) {
                return NeedMore{pos + 4};
            }
            length = (std::size_t{data[pos + 2]} << 8) | data[pos + 3];
            header = 4;
        }
        const std::size_t value = pos + header;
        if (value + length > capacity) {
            return TlvError::Malformed;
        }
        if (type == kTlvNdef) {
            return NdefTlv{value, length};
        }
        pos = value + length;  // lock control, memory control, proprietary or unknown TLV
    }
}

std::vector<std::uint8_t> classic_ndef_sectors(ByteView block1, ByteView block2) {
    // Block 1: CRC, info byte, then the AIDs of sectors 1 to 7. Block 2: sectors 8 to 15.
    std::array<std::uint8_t, 32> entries{};
    if (block1.size() < 16 || block2.size() < 16) {
        return {};
    }
    for (std::size_t i = 0; i < 14; ++i) {
        entries[i] = block1[2 + i];
    }
    for (std::size_t i = 0; i < 16; ++i) {
        entries[14 + i] = block2[i];
    }

    std::vector<std::uint8_t> sectors;
    for (std::uint8_t sector = 1; sector <= 15; ++sector) {
        const std::size_t at = (sector - 1) * 2;
        const bool ndef =
            entries[at] == kNdefApplicationCode && entries[at + 1] == kNdefFunctionCluster;
        if (ndef) {
            sectors.push_back(sector);
        } else if (!sectors.empty()) {
            break;  // the NDEF message spans one contiguous run of sectors
        }
    }
    return sectors;
}

}  // namespace tapwave::tag_memory
