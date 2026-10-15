#include "drivers/nfc/common/pn532.hpp"

#include <algorithm>

#include "drivers/nfc/common/pn532_frame.hpp"

namespace tapwave::nfc {

namespace {

constexpr std::uint32_t kAckTimeoutMs = 30;
constexpr std::uint8_t kIcPn532 = 0x32;

}  // namespace

Error pn532_status_error(std::uint8_t status) {
    switch (status & 0x3F) {
        case 0x01:
            return Error::Timeout;  // the tag did not answer
        case 0x02:
            return Error::Crc;
        case 0x14:
            return Error::AuthFailed;
        case 0x0A:  // RF field not switched on in time
        case 0x27:  // command not acceptable in the current state
            return Error::NoTag;
        default:
            return Error::Protocol;
    }
}

Pn532::Pn532(std::unique_ptr<Pn532Transport> transport, std::string driver_id)
    : transport_(std::move(transport)) {
    info_.driver = std::move(driver_id);
    info_.model = "PN532";
    info_.bus = std::string(transport_->bus());
}

std::expected<Bytes, Error> Pn532::command(std::uint8_t code, ByteView params,
                                           std::uint32_t timeout_ms) {
    const Bytes frame = pn532::build_command(code, params);
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (auto written = transport_->write(frame); !written) {
            return std::unexpected(written.error());
        }
        const auto ack = transport_->read(kAckTimeoutMs);
        if (!ack) {
            return std::unexpected(ack.error());
        }
        const auto ack_frame = pn532::parse_frame(*ack);
        if (ack_frame && ack_frame->kind == pn532::FrameKind::Ack) {
            break;
        }
        if (attempt == 1) {
            return std::unexpected(Error::Protocol);
        }
    }

    const auto raw = transport_->read(timeout_ms);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    const auto response = pn532::parse_frame(*raw);
    if (!response) {
        return std::unexpected(
            response.error() == pn532::FrameError::BadChecksum ? Error::Crc : Error::Protocol);
    }
    if (response->kind != pn532::FrameKind::Data || response->code != code + 1) {
        return std::unexpected(Error::Protocol);
    }
    return response->data;
}

std::expected<void, Error> Pn532::begin() {
    transport_->wake();
    const auto version = command(pn532::kGetFirmwareVersion, {});
    if (!version) {
        return std::unexpected(version.error());
    }
    if (version->size() != 4 || (*version)[0] != kIcPn532) {
        return std::unexpected(Error::NotSupported);
    }
    info_.firmware = std::to_string((*version)[1]) + "." + std::to_string((*version)[2]);

    // Normal mode, no SAM, IRQ pin used.
    if (auto sam = command(pn532::kSamConfiguration, Bytes{0x01, 0x14, 0x01}); !sam) {
        return std::unexpected(sam.error());
    }
    // MaxRetries: give up on passive activation after two tries, so polling an empty field
    // returns in a few milliseconds instead of waiting for a tag.
    if (auto retries = command(pn532::kRfConfiguration, Bytes{0x05, 0xFF, 0x01, 0x02}); !retries) {
        return std::unexpected(retries.error());
    }
    return {};
}

std::expected<Target, Error> Pn532::select() {
    if (target_active_) {
        // Switch the field off so the tag selected last time powers down and answers again.
        target_active_ = false;
        if (auto off = command(pn532::kRfConfiguration, Bytes{0x01, 0x00}); !off) {
            return std::unexpected(off.error());
        }
        transport_->sleep_ms(5);
    }
    // One target, 106 kbit/s type A. The PN532 switches the field on by itself.
    const auto data = command(pn532::kInListPassiveTarget, Bytes{0x01, 0x00});
    if (!data) {
        return std::unexpected(data.error());
    }
    // NbTg, Tg, SENS_RES (2), SEL_RES, NFCIDLength, NFCID...
    if (data->empty() || (*data)[0] == 0) {
        return std::unexpected(Error::NoTag);
    }
    if (data->size() < 6) {
        return std::unexpected(Error::Protocol);
    }
    const std::size_t uid_length = (*data)[5];
    if (data->size() < 6 + uid_length) {
        return std::unexpected(Error::Protocol);
    }
    const auto uid = Uid::from(ByteView(*data).subspan(6, uid_length));
    if (!uid) {
        return std::unexpected(Error::Protocol);
    }
    target_.uid = *uid;
    target_.atqa = static_cast<std::uint16_t>(((*data)[2] << 8) | (*data)[3]);
    target_.sak = (*data)[4];
    target_active_ = true;
    return target_;
}

std::expected<Bytes, Error> Pn532::exchange(std::uint8_t code, ByteView params) {
    if (!target_active_) {
        return std::unexpected(Error::NoTag);
    }
    const auto data = command(code, params);
    if (!data) {
        return std::unexpected(data.error());
    }
    if (data->empty()) {
        return std::unexpected(Error::Protocol);
    }
    if (((*data)[0] & 0x3F) != 0) {
        return std::unexpected(pn532_status_error((*data)[0]));
    }
    return Bytes(data->begin() + 1, data->end());
}

std::expected<Block, Error> Pn532::mifare_read(std::uint8_t block) {
    const auto data = exchange(pn532::kInDataExchange, Bytes{0x01, 0x30, block});
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

std::expected<void, Error> Pn532::ultralight_write(std::uint8_t page, const Page& data) {
    const Bytes params{0x01, 0xA2, page, data[0], data[1], data[2], data[3]};
    const auto result = exchange(pn532::kInDataExchange, params);
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

std::expected<void, Error> Pn532::classic_auth(std::uint8_t block, KeyType type,
                                               const ClassicKey& key) {
    Bytes params{0x01, static_cast<std::uint8_t>(type), block};
    params.insert(params.end(), key.begin(), key.end());
    // Authentication uses the last four UID bytes (the whole UID on 4-byte tags).
    const ByteView uid = target_.uid.bytes();
    const ByteView last4 = uid.size() >= 4 ? uid.last(4) : uid;
    params.insert(params.end(), last4.begin(), last4.end());
    const auto result = exchange(pn532::kInDataExchange, params);
    if (!result) {
        // A refused authentication leaves the tag halted; the next select resets the field.
        return std::unexpected(result.error());
    }
    return {};
}

std::expected<Bytes, Error> Pn532::transceive(ByteView command_bytes) {
    return exchange(pn532::kInCommunicateThru, command_bytes);
}

}  // namespace tapwave::nfc
