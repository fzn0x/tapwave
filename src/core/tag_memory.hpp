// Where NDEF lives inside tag memory.
//
// NFC Forum Type 2 tags (NTAG21x, MIFARE Ultralight) keep a capability container in page 3 and
// a sequence of TLV blocks from page 4. MIFARE Classic tags formatted for NDEF list their NDEF
// sectors in the MIFARE Application Directory (MAD) in sector 0, and the same TLV blocks span
// those sectors. Both are pure memory layouts, independent of the reader chip.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "core/bytes.hpp"

namespace tapwave::tag_memory {

// ---- Type 2 tags -------------------------------------------------------------------------

inline constexpr std::size_t kPageSize = 4;
inline constexpr std::uint8_t kCapabilityPage = 3;
inline constexpr std::uint8_t kFirstDataPage = 4;

struct CapabilityContainer {
    std::uint8_t magic = 0;    // 0xE1 when the tag is NDEF formatted
    std::uint8_t version = 0;  // 0x10 for version 1.0
    std::uint8_t size = 0;     // data area size divided by 8
    std::uint8_t access = 0;   // high nibble read access, low nibble write access

    // Parses page 3. Returns nullopt unless the magic number marks an NDEF-formatted tag.
    static std::optional<CapabilityContainer> parse(ByteView page);

    std::size_t data_area_size() const { return std::size_t{size} * 8; }
    bool readable() const { return (access & 0xF0) == 0; }
    bool writable() const { return (access & 0x0F) == 0; }
};

// ---- TLV blocks, shared by Type 2 and MIFARE Classic -------------------------------------

// The NDEF message TLV: where its value starts within the data area and how long it is.
struct NdefTlv {
    std::size_t offset = 0;
    std::size_t length = 0;  // 0 means the tag is formatted but holds no message
};

// More of the data area is needed before the search can continue: at least `bytes` in total.
struct NeedMore {
    std::size_t bytes = 0;
};

enum class TlvError : std::uint8_t {
    NoNdef,     // a terminator TLV or the end of the data area came first
    Malformed,  // a TLV runs past the end of the data area
};

using TlvSearch = std::variant<NdefTlv, NeedMore, TlvError>;

// Walks the TLV blocks at the start of `data` (the data area read so far) looking for the NDEF
// message TLV. NULL, lock control, memory control and proprietary TLVs are skipped.
// `capacity` is the full size of the data area.
TlvSearch find_ndef_tlv(ByteView data, std::size_t capacity);

// ---- MIFARE Classic ----------------------------------------------------------------------

using ClassicKey = std::array<std::uint8_t, 6>;

// Public key A of the MAD sector (MIFARE application directory).
inline constexpr ClassicKey kMadKey{0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};
// Public key A of NFC Forum NDEF sectors.
inline constexpr ClassicKey kNdefKey{0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7};

// First block of a sector: four blocks per sector up to sector 31, sixteen after (4K tags).
constexpr std::uint8_t classic_first_block(std::uint8_t sector) {
    return sector < 32 ? static_cast<std::uint8_t>(sector * 4)
                       : static_cast<std::uint8_t>(128 + (sector - 32) * 16);
}

// Data blocks per sector, not counting the sector trailer.
constexpr std::uint8_t classic_data_blocks(std::uint8_t sector) {
    return sector < 32 ? 3 : 15;
}

// Reads MAD1 (blocks 1 and 2 of sector 0) and returns the NDEF sectors among sectors 1 to 15:
// the first run of consecutive sectors whose application ID is the NFC Forum NDEF AID 0x03E1.
std::vector<std::uint8_t> classic_ndef_sectors(ByteView block1, ByteView block2);

}  // namespace tapwave::tag_memory
