// The two NFC driver interfaces (ARCHITECTURE.md section 6).
#pragma once

#include <expected>
#include <optional>
#include <span>

#include "drivers/nfc/common/nfc_types.hpp"

namespace tapwave::nfc {

// What the app uses. A reader that understands NDEF itself implements this directly.
class TagReader {
public:
    virtual ~TagReader() = default;

    virtual const ReaderInfo& info() const = 0;

    // Cheap presence check: the UID of the tag in the field, or nullopt when the field is empty.
    // An error means the reader itself is in trouble (bus failure, no answer).
    virtual std::expected<std::optional<Uid>, Error> detect() = 0;

    // Full read of the tag in the field.
    virtual std::expected<TagRead, Error> read() = 0;

    // Writes an NDEF message to the tag in the field. Milestone 2.
    virtual std::expected<void, Error> write_ndef(ByteView message) = 0;
};

// What ISO 14443-A frontends (PN532, RC522, PN5180) implement: raw tag commands, no memory
// layouts. Type2TagReader turns one into a TagReader.
class Iso14443aReader {
public:
    virtual ~Iso14443aReader() = default;

    virtual const ReaderInfo& info() const = 0;

    // Activates one Type A tag, whatever state an earlier select left it in.
    // Error::NoTag when the field is empty.
    virtual std::expected<Target, Error> select() = 0;

    // MIFARE READ: 16 bytes from a block (Classic) or four pages (Type 2).
    virtual std::expected<Block, Error> mifare_read(std::uint8_t block) = 0;

    // Type 2 WRITE of one four-byte page.
    virtual std::expected<void, Error> ultralight_write(std::uint8_t page, const Page& data) = 0;

    // MIFARE Classic authentication for the sector holding `block`, with the selected tag's UID.
    virtual std::expected<void, Error> classic_auth(std::uint8_t block, KeyType type,
                                                    const ClassicKey& key) = 0;

    // Sends a raw command (CRC added by the reader) and returns the answer without CRC.
    virtual std::expected<Bytes, Error> transceive(ByteView command) = 0;
};

}  // namespace tapwave::nfc
