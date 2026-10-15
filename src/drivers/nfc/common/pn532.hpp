// The PN532 command layer, independent of the bus. The I2C and SPI transports live in
// drivers/nfc/pn532_i2c.cpp and drivers/nfc/pn532_spi.cpp.
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "drivers/nfc/common/readers.hpp"

namespace tapwave::nfc {

// The longest response Tapwave asks for: InListPassiveTarget with a 10-byte UID is 25 bytes.
inline constexpr std::size_t kPn532MaxFrame = 64;

// Moves PN532 frames over one bus. Implementations wait for the chip's ready signal and strip
// bus framing (the I2C status byte, the SPI direction byte).
class Pn532Transport {
public:
    virtual ~Pn532Transport() = default;

    virtual std::string_view bus() const = 0;

    // Wakes the chip from power-down or after reset.
    virtual void wake() = 0;

    virtual std::expected<void, Error> write(ByteView frame) = 0;

    // Waits until the chip has data, then reads one complete frame (ACK, NACK or data).
    // `max_length` bounds the frame: 6 for an ACK, larger for responses. Buses that cannot
    // stop mid-frame (I2C) read that many bytes and cut the frame out of them.
    virtual std::expected<Bytes, Error> read(std::uint32_t timeout_ms, std::size_t max_length) = 0;

    virtual void sleep_ms(std::uint32_t ms) = 0;
};

class Pn532 final : public Iso14443aReader {
public:
    Pn532(std::unique_ptr<Pn532Transport> transport, std::string driver_id);

    // Reads the firmware version and configures the chip. Fails when no PN532 answers, which is
    // how auto-detection probes for one.
    std::expected<void, Error> begin();

    const ReaderInfo& info() const override { return info_; }
    std::expected<Target, Error> select() override;
    std::expected<Block, Error> mifare_read(std::uint8_t block) override;
    std::expected<void, Error> ultralight_write(std::uint8_t page, const Page& data) override;
    std::expected<void, Error> classic_auth(std::uint8_t block, KeyType type,
                                            const ClassicKey& key) override;
    std::expected<Bytes, Error> transceive(ByteView command) override;

private:
    // Sends a command, waits for the ACK and the response, and returns the response data.
    std::expected<Bytes, Error> command(std::uint8_t code, ByteView params,
                                        std::uint32_t timeout_ms = 100);
    // InDataExchange or InCommunicateThru: checks the status byte that leads the answer.
    std::expected<Bytes, Error> exchange(std::uint8_t code, ByteView params);

    std::unique_ptr<Pn532Transport> transport_;
    ReaderInfo info_;
    bool target_active_ = false;
    Target target_;
};

// Maps the status byte of InDataExchange and InCommunicateThru to an error.
Error pn532_status_error(std::uint8_t status);

}  // namespace tapwave::nfc
