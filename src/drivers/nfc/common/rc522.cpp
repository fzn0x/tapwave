#include "drivers/nfc/common/rc522.hpp"

#include <algorithm>
#include <array>

#include "drivers/nfc/common/iso14443a.hpp"

namespace tapwave::nfc {

namespace rc522 {

bool known_version(std::uint8_t version) {
    switch (version) {
        case 0x88:  // FM17522
        case 0x89:  // FM17522E
        case 0xB2:  // FM17522 variant
        case 0x90:  // MFRC522 v0.0
        case 0x91:  // MFRC522 v1.0
        case 0x92:  // MFRC522 v2.0
        case 0x12:  // common counterfeit
            return true;
        default:
            return false;
    }
}

std::string version_name(std::uint8_t version) {
    switch (version) {
        case 0x91:
            return "1.0";
        case 0x92:
            return "2.0";
        case 0x90:
            return "0.0";
        case 0x88:
        case 0x89:
        case 0xB2:
            return "FM17522";
        default: {
            constexpr char kHex[] = "0123456789ABCDEF";
            return std::string{"0x"} + kHex[version >> 4] + kHex[version & 0x0F];
        }
    }
}

}  // namespace rc522

namespace {

constexpr std::uint8_t kRxIrq = 0x20;
constexpr std::uint8_t kIdleIrq = 0x10;
constexpr std::uint8_t kTimerIrq = 0x01;
constexpr std::uint8_t kStartSend = 0x80;
constexpr std::uint8_t kCrypto1On = 0x08;
constexpr std::uint8_t kCollisionError = 0x08;
constexpr std::uint8_t kFatalErrors = 0x13;  // buffer overflow, parity, protocol
constexpr std::uint32_t kCommandTimeoutMs = 40;

}  // namespace

Rc522::Rc522(std::unique_ptr<Rc522Bus> bus, std::string driver_id) : bus_(std::move(bus)) {
    info_.driver = std::move(driver_id);
    info_.model = "RC522";
    info_.bus = std::string(bus_->bus());
}

void Rc522::set_bits(std::uint8_t reg, std::uint8_t mask) {
    bus_->write(reg, static_cast<std::uint8_t>(bus_->read(reg) | mask));
}

void Rc522::clear_bits(std::uint8_t reg, std::uint8_t mask) {
    bus_->write(reg, static_cast<std::uint8_t>(bus_->read(reg) & ~mask));
}

std::expected<void, Error> Rc522::begin() {
    using namespace rc522;
    bus_->hard_reset();
    const std::uint8_t version = bus_->read(kVersionReg);
    if (!known_version(version)) {
        return std::unexpected(Error::NotSupported);
    }
    info_.firmware = version_name(version);

    bus_->write(kCommandReg, kSoftReset);
    const std::uint32_t start = bus_->now_ms();
    bus_->sleep_ms(1);
    while ((bus_->read(kCommandReg) & 0x10) != 0) {  // PowerDown bit clears when ready
        if (bus_->now_ms() - start > 150) {
            return std::unexpected(Error::Timeout);
        }
        bus_->sleep_ms(5);
    }

    bus_->write(kTxModeReg, 0x00);  // 106 kbit/s, no hardware CRC: CRC_A is added in software
    bus_->write(kRxModeReg, 0x00);
    bus_->write(kModWidthReg, 0x26);
    // Timer: TAuto, prescaler 0xA9 and reload 1000 give a 25 ms timeout for tag answers.
    bus_->write(kTModeReg, 0x80);
    bus_->write(kTPrescalerReg, 0xA9);
    bus_->write(kTReloadRegH, 0x03);
    bus_->write(kTReloadRegL, 0xE8);
    bus_->write(kTxAskReg, 0x40);  // 100 % ASK modulation
    bus_->write(kModeReg, 0x3D);   // CRC preset 0x6363
    bus_->write(kRfCfgReg, 0x70);  // maximum receiver gain (48 dB) for range
    const std::uint8_t tx = bus_->read(kTxControlReg);
    if ((tx & 0x03) != 0x03) {
        bus_->write(kTxControlReg, static_cast<std::uint8_t>(tx | 0x03));  // antenna on
    }
    return {};
}

std::expected<Rc522::Answer, Error> Rc522::exchange(ByteView frame, std::uint8_t tx_last_bits) {
    using namespace rc522;
    bus_->write(kCommandReg, kIdle);
    bus_->write(kComIrqReg, 0x7F);
    bus_->write(kFifoLevelReg, 0x80);  // flush
    bus_->write_fifo(frame);
    bus_->write(kBitFramingReg, tx_last_bits);
    bus_->write(kCommandReg, kTransceive);
    set_bits(kBitFramingReg, kStartSend);

    const std::uint32_t start = bus_->now_ms();
    while (true) {
        const std::uint8_t irq = bus_->read(kComIrqReg);
        if (irq & (kRxIrq | kIdleIrq)) {
            break;
        }
        if (irq & kTimerIrq) {
            return std::unexpected(Error::NoTag);  // no answer within 25 ms
        }
        if (bus_->now_ms() - start > kCommandTimeoutMs) {
            return std::unexpected(Error::Timeout);  // the chip itself stopped answering
        }
    }
    clear_bits(kBitFramingReg, kStartSend);

    const std::uint8_t error = bus_->read(kErrorReg);
    if (error & kFatalErrors) {
        return std::unexpected(Error::Protocol);
    }
    Answer answer;
    const std::uint8_t level = bus_->read(kFifoLevelReg) & 0x7F;
    answer.data.resize(level);
    if (level > 0) {
        bus_->read_fifo(answer.data);
    }
    answer.last_bits = bus_->read(kControlReg) & 0x07;
    if (error & kCollisionError) {
        return std::unexpected(Error::Collision);
    }
    return answer;
}

std::expected<Bytes, Error> Rc522::exchange_with_crc(ByteView command) {
    const auto answer = exchange(iso14443a::with_crc(command));
    if (!answer) {
        return std::unexpected(answer.error());
    }
    if (answer->last_bits == 4 && answer->data.size() == 1) {
        return std::unexpected(Error::NotSupported);  // a four-bit NAK
    }
    auto body = iso14443a::check_crc(answer->data);
    if (!body) {
        return std::unexpected(Error::Crc);
    }
    return *body;
}

void Rc522::halt() {
    // HLTA has no answer; the timeout is the expected outcome.
    (void)exchange(iso14443a::with_crc(Bytes{iso14443a::kHltA, 0x00}));
}

std::expected<Target, Error> Rc522::select() {
    using namespace rc522;
    if (target_active_) {
        target_active_ = false;
        clear_bits(kStatus2Reg, kCrypto1On);
        halt();  // the tag answers the WUPA below from its halt state
    }
    clear_bits(kCollReg, 0x80);  // keep received bits after a collision for diagnosis

    // WUPA wakes tags that are idle or halted; seven-bit short frame.
    const auto atqa = exchange(Bytes{iso14443a::kWupA}, 7);
    if (!atqa) {
        return std::unexpected(atqa.error());
    }
    if (atqa->data.size() != 2) {
        return std::unexpected(Error::Protocol);
    }
    Target target;
    target.atqa = static_cast<std::uint16_t>(atqa->data[0] | (atqa->data[1] << 8));

    Bytes uid;
    for (int cascade = 0; cascade < 3; ++cascade) {
        const Bytes anticollision{iso14443a::kSelectCommands[static_cast<std::size_t>(cascade)],
                                  0x20};
        const auto answer = exchange(anticollision);
        if (!answer) {
            return std::unexpected(answer.error());
        }
        if (answer->data.size() != 5) {
            return std::unexpected(Error::Protocol);
        }
        iso14443a::CascadeLevel level{};
        std::ranges::copy(answer->data, level.begin());
        if (!iso14443a::bcc_valid(level)) {
            return std::unexpected(Error::Crc);
        }
        const auto sak = exchange(iso14443a::select_frame(cascade, level));
        if (!sak) {
            return std::unexpected(sak.error());
        }
        const auto sak_body = iso14443a::check_crc(sak->data);
        if (!sak_body || sak_body->size() != 1) {
            return std::unexpected(Error::Crc);
        }
        const bool more = iso14443a::uid_incomplete((*sak_body)[0]);
        if (more && level[0] != iso14443a::kCascadeTag) {
            return std::unexpected(Error::Protocol);
        }
        iso14443a::append_uid_part(uid, level, more);
        if (!more) {
            target.sak = (*sak_body)[0];
            const auto parsed = Uid::from(uid);
            if (!parsed) {
                return std::unexpected(Error::Protocol);
            }
            target.uid = *parsed;
            target_ = target;
            target_active_ = true;
            return target;
        }
    }
    return std::unexpected(Error::Protocol);  // more than three cascade levels
}

std::expected<Block, Error> Rc522::mifare_read(std::uint8_t block) {
    if (!target_active_) {
        return std::unexpected(Error::NoTag);
    }
    const auto data = exchange_with_crc(Bytes{iso14443a::kMifareRead, block});
    if (!data) {
        return std::unexpected(data.error());
    }
    if (data->size() != 16) {
        return std::unexpected(Error::Protocol);
    }
    Block out;
    std::ranges::copy(*data, out.begin());
    return out;
}

std::expected<void, Error> Rc522::ultralight_write(std::uint8_t page, const Page& data) {
    if (!target_active_) {
        return std::unexpected(Error::NoTag);
    }
    const auto answer = exchange(iso14443a::with_crc(
        Bytes{iso14443a::kUltralightWrite, page, data[0], data[1], data[2], data[3]}));
    if (!answer) {
        return std::unexpected(answer.error());
    }
    if (answer->last_bits != 4 || answer->data.size() != 1 ||
        (answer->data[0] & 0x0F) != iso14443a::kAck) {
        return std::unexpected(Error::ReadOnly);
    }
    return {};
}

std::expected<void, Error> Rc522::classic_auth(std::uint8_t block, KeyType type,
                                               const ClassicKey& key) {
    using namespace rc522;
    if (!target_active_) {
        return std::unexpected(Error::NoTag);
    }
    Bytes frame{static_cast<std::uint8_t>(type), block};
    frame.insert(frame.end(), key.begin(), key.end());
    const ByteView uid = target_.uid.bytes();
    const ByteView last4 = uid.size() >= 4 ? uid.last(4) : uid;
    frame.insert(frame.end(), last4.begin(), last4.end());

    bus_->write(kCommandReg, kIdle);
    bus_->write(kComIrqReg, 0x7F);
    bus_->write(kFifoLevelReg, 0x80);
    bus_->write_fifo(frame);
    bus_->write(kCommandReg, kMfAuthent);
    const std::uint32_t start = bus_->now_ms();
    while (true) {
        const std::uint8_t irq = bus_->read(kComIrqReg);
        if (irq & kIdleIrq) {
            break;
        }
        if ((irq & kTimerIrq) || bus_->now_ms() - start > kCommandTimeoutMs) {
            return std::unexpected(Error::AuthFailed);
        }
    }
    if ((bus_->read(kStatus2Reg) & kCrypto1On) == 0) {
        return std::unexpected(Error::AuthFailed);
    }
    return {};
}

std::expected<Bytes, Error> Rc522::transceive(ByteView command) {
    if (!target_active_) {
        return std::unexpected(Error::NoTag);
    }
    return exchange_with_crc(command);
}

}  // namespace tapwave::nfc
