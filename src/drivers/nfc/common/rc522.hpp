// The MFRC522 (RC522) command layer, independent of the bus. Also covers FM17522 clones.
// The SPI bus lives in drivers/nfc/rc522_spi.cpp.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "drivers/nfc/common/readers.hpp"

namespace tapwave::nfc {

// Register access over SPI (or I2C on boards that wire it that way).
class Rc522Bus {
public:
    virtual ~Rc522Bus() = default;

    virtual std::string_view bus() const = 0;
    virtual std::uint8_t read(std::uint8_t reg) = 0;
    virtual void write(std::uint8_t reg, std::uint8_t value) = 0;
    virtual void read_fifo(std::span<std::uint8_t> out) = 0;
    virtual void write_fifo(ByteView data) = 0;
    // Pulses the reset pin when one is wired; otherwise does nothing.
    virtual void hard_reset() {}
    virtual std::uint32_t now_ms() = 0;
    virtual void sleep_ms(std::uint32_t ms) = 0;
};

namespace rc522 {

inline constexpr std::uint8_t kCommandReg = 0x01;
inline constexpr std::uint8_t kComIrqReg = 0x04;
inline constexpr std::uint8_t kDivIrqReg = 0x05;
inline constexpr std::uint8_t kErrorReg = 0x06;
inline constexpr std::uint8_t kStatus2Reg = 0x08;
inline constexpr std::uint8_t kFifoDataReg = 0x09;
inline constexpr std::uint8_t kFifoLevelReg = 0x0A;
inline constexpr std::uint8_t kControlReg = 0x0C;
inline constexpr std::uint8_t kBitFramingReg = 0x0D;
inline constexpr std::uint8_t kCollReg = 0x0E;
inline constexpr std::uint8_t kModeReg = 0x11;
inline constexpr std::uint8_t kTxModeReg = 0x12;
inline constexpr std::uint8_t kRxModeReg = 0x13;
inline constexpr std::uint8_t kTxControlReg = 0x14;
inline constexpr std::uint8_t kTxAskReg = 0x15;
inline constexpr std::uint8_t kModWidthReg = 0x24;
inline constexpr std::uint8_t kRfCfgReg = 0x26;
inline constexpr std::uint8_t kTModeReg = 0x2A;
inline constexpr std::uint8_t kTPrescalerReg = 0x2B;
inline constexpr std::uint8_t kTReloadRegH = 0x2C;
inline constexpr std::uint8_t kTReloadRegL = 0x2D;
inline constexpr std::uint8_t kVersionReg = 0x37;

inline constexpr std::uint8_t kIdle = 0x00;
inline constexpr std::uint8_t kTransceive = 0x0C;
inline constexpr std::uint8_t kMfAuthent = 0x0E;
inline constexpr std::uint8_t kSoftReset = 0x0F;

// VersionReg values of chips that behave like an MFRC522.
bool known_version(std::uint8_t version);

std::string version_name(std::uint8_t version);

}  // namespace rc522

class Rc522 final : public Iso14443aReader {
public:
    Rc522(std::unique_ptr<Rc522Bus> bus, std::string driver_id);

    // Resets and configures the chip. Fails when the version register does not identify an
    // MFRC522 or a known clone, which is how auto-detection probes for one.
    std::expected<void, Error> begin();

    const ReaderInfo& info() const override { return info_; }
    std::expected<Target, Error> select() override;
    std::expected<Block, Error> mifare_read(std::uint8_t block) override;
    std::expected<void, Error> ultralight_write(std::uint8_t page, const Page& data) override;
    std::expected<void, Error> classic_auth(std::uint8_t block, KeyType type,
                                            const ClassicKey& key) override;
    std::expected<Bytes, Error> transceive(ByteView command) override;

private:
    struct Answer {
        Bytes data;
        std::uint8_t last_bits = 0;  // valid bits in the last byte, 0 meaning all eight
    };

    // Sends a frame (CRC already appended when needed) and returns the tag's answer.
    std::expected<Answer, Error> exchange(ByteView frame, std::uint8_t tx_last_bits = 0);
    std::expected<Bytes, Error> exchange_with_crc(ByteView command);
    void set_bits(std::uint8_t reg, std::uint8_t mask);
    void clear_bits(std::uint8_t reg, std::uint8_t mask);
    void halt();

    std::unique_ptr<Rc522Bus> bus_;
    ReaderInfo info_;
    bool target_active_ = false;
    Target target_;
};

}  // namespace tapwave::nfc
