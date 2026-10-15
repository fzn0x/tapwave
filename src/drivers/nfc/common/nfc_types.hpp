// Types shared by every NFC driver.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/bytes.hpp"
#include "core/tag_memory.hpp"
#include "core/uid.hpp"

namespace tapwave::nfc {

enum class Error : std::uint8_t {
    NoTag,         // nothing in the field
    Timeout,       // the reader or the tag did not answer in time
    Transport,     // the I2C or SPI bus failed
    Protocol,      // an answer that does not follow the protocol
    Crc,           // a frame with a bad checksum
    AuthFailed,    // MIFARE Classic authentication refused
    NotSupported,  // the tag or reader cannot do this
    Collision,     // more than one tag in the field
    ReadOnly,      // the tag is locked
    TooLarge,      // the data does not fit on the tag
};

std::string_view describe(Error error);

struct ReaderInfo {
    std::string driver;    // registry ID: "pn532-i2c"
    std::string model;     // "PN532"
    std::string bus;       // "i2c" or "spi"
    std::string firmware;  // "1.6", or the version register for an RC522

    std::string summary() const;  // "PN532 1.6 (i2c)"
};

using Block = std::array<std::uint8_t, 16>;
using Page = std::array<std::uint8_t, 4>;
using tag_memory::ClassicKey;

enum class KeyType : std::uint8_t { A = 0x60, B = 0x61 };

// An activated ISO 14443-A tag.
struct Target {
    Uid uid;
    std::uint16_t atqa = 0;  // SENS_RES, for example 0x0044 for NTAG
    std::uint8_t sak = 0;    // SEL_RES
};

enum class TagFamily : std::uint8_t {
    Unknown,
    Type2,  // NFC Forum Type 2, model not identified
    Ntag210,
    Ntag212,
    Ntag213,
    Ntag215,
    Ntag216,
    Ultralight,
    UltralightEv1,
    MifareClassicMini,
    MifareClassic1k,
    MifareClassic4k,
    Iso14443_4,  // DESFire, phones, bank cards
};

std::string_view to_string(TagFamily family);

bool is_type2(TagFamily family);
bool is_classic(TagFamily family);

// Everything read from one tag.
struct TagRead {
    Target target;
    TagFamily family = TagFamily::Unknown;
    std::optional<Bytes> ndef;              // the NDEF message; empty when the tag is blank
    std::optional<std::string> ndef_issue;  // why there is no NDEF message, when there is none
    std::size_t capacity = 0;               // size of the NDEF data area in bytes
    bool writable = false;
};

}  // namespace tapwave::nfc
