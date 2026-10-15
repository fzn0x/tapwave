#include "drivers/nfc/common/type2_tag_reader.hpp"

#include <algorithm>
#include <variant>

#include "core/tag_memory.hpp"
#include "drivers/nfc/common/iso14443a.hpp"

namespace tapwave::nfc {

namespace {

using tag_memory::CapabilityContainer;
using tag_memory::NdefTlv;
using tag_memory::NeedMore;
using tag_memory::TlvError;

constexpr std::string_view kNotFormatted = "the tag is not formatted for NDEF";
constexpr std::string_view kDamaged = "the NDEF data on the tag is damaged";

std::string read_failed(Error error) {
    return "reading the tag failed: " + std::string(describe(error));
}

// Pulls the NDEF message out of a data area read piece by piece. `more` appends at least one
// byte to `data` or returns the error that stopped it.
template <class More>
void extract_ndef(TagRead& out, Bytes& data, std::size_t capacity, More&& more) {
    while (true) {
        const auto search = tag_memory::find_ndef_tlv(data, capacity);
        if (const auto* need = std::get_if<NeedMore>(&search)) {
            if (const auto error = more(need->bytes)) {
                out.ndef_issue = read_failed(*error);
                return;
            }
            continue;
        }
        if (const auto* error = std::get_if<TlvError>(&search)) {
            if (*error == TlvError::NoNdef) {
                out.ndef = Bytes{};  // formatted, but blank
            } else {
                out.ndef_issue = std::string(kDamaged);
            }
            return;
        }
        const auto tlv = std::get<NdefTlv>(search);
        while (data.size() < tlv.offset + tlv.length) {
            if (const auto error = more(tlv.offset + tlv.length)) {
                out.ndef_issue = read_failed(*error);
                return;
            }
        }
        out.ndef = Bytes(data.begin() + static_cast<std::ptrdiff_t>(tlv.offset),
                         data.begin() + static_cast<std::ptrdiff_t>(tlv.offset + tlv.length));
        return;
    }
}

}  // namespace

Type2TagReader::Type2TagReader(std::unique_ptr<Iso14443aReader> frontend)
    : frontend_(std::move(frontend)) {}

std::expected<std::optional<Uid>, Error> Type2TagReader::detect() {
    const auto target = frontend_->select();
    if (target) {
        return target->uid;
    }
    if (target.error() == Error::NoTag || target.error() == Error::Collision ||
        target.error() == Error::Crc || target.error() == Error::Protocol) {
        // A tag moving through the field produces broken frames; that is not a reader fault.
        return std::optional<Uid>{};
    }
    return std::unexpected(target.error());
}

std::expected<TagRead, Error> Type2TagReader::read() {
    const auto target = frontend_->select();
    if (!target) {
        return std::unexpected(target.error());
    }
    TagRead out;
    out.target = *target;
    out.family = iso14443a::family_from_sak(target->sak);
    if (is_type2(out.family)) {
        read_type2(out);
    } else if (is_classic(out.family)) {
        read_classic(out);
    } else {
        out.ndef_issue = "Tapwave reads NDEF from NTAG, MIFARE Ultralight and MIFARE Classic tags";
    }
    return out;
}

void Type2TagReader::read_type2(TagRead& out) {
    // READ 3 returns pages 3 to 6: the capability container and the first 12 data bytes.
    const auto first = frontend_->mifare_read(tag_memory::kCapabilityPage);
    if (!first) {
        out.ndef_issue = read_failed(first.error());
        return;
    }
    const auto cc = CapabilityContainer::parse(ByteView(*first).first(4));
    if (!cc || !cc->readable()) {
        out.ndef_issue = std::string(kNotFormatted);
        out.family = identify_type2(out);
        return;
    }
    const std::size_t capacity = std::min(cc->data_area_size(), kMaxDataArea);
    out.capacity = capacity;
    out.writable = cc->writable();

    Bytes data(first->begin() + 4, first->end());
    std::uint8_t next_page = tag_memory::kFirstDataPage + 3;
    extract_ndef(out, data, capacity, [&](std::size_t) -> std::optional<Error> {
        if (data.size() >= capacity) {
            return Error::Protocol;  // the TLV promised more than the data area holds
        }
        const auto block = frontend_->mifare_read(next_page);
        if (!block) {
            return block.error();
        }
        next_page = static_cast<std::uint8_t>(next_page + 4);
        const std::size_t take = std::min<std::size_t>(block->size(), capacity - data.size());
        data.insert(data.end(), block->begin(), block->begin() + static_cast<std::ptrdiff_t>(take));
        return std::nullopt;
    });
    out.family = identify_type2(out);
}

TagFamily Type2TagReader::identify_type2(const TagRead& out) {
    // Last, because tags that do not know GET_VERSION (original Ultralight, Ultralight C)
    // answer with a NAK and drop back to idle.
    const auto version = frontend_->transceive(Bytes{iso14443a::kGetVersion});
    if (version && version->size() == 8) {
        return iso14443a::family_from_version(*version);
    }
    if (out.capacity == 48) {
        return TagFamily::Ultralight;
    }
    return TagFamily::Type2;
}

void Type2TagReader::read_classic(TagRead& out) {
    out.writable = false;  // MIFARE Classic is read-only by design
    if (!frontend_->classic_auth(1, KeyType::A, tag_memory::kMadKey)) {
        out.ndef_issue = std::string(kNotFormatted);
        return;
    }
    const auto mad1 = frontend_->mifare_read(1);
    const auto mad2 = frontend_->mifare_read(2);
    if (!mad1 || !mad2) {
        out.ndef_issue = read_failed(!mad1 ? mad1.error() : mad2.error());
        return;
    }
    const auto sectors = tag_memory::classic_ndef_sectors(*mad1, *mad2);
    if (sectors.empty()) {
        out.ndef_issue = std::string(kNotFormatted);
        return;
    }
    std::size_t capacity = 0;
    for (std::uint8_t sector : sectors) {
        capacity += std::size_t{tag_memory::classic_data_blocks(sector)} * 16;
    }
    capacity = std::min(capacity, kMaxDataArea);
    out.capacity = capacity;

    Bytes data;
    std::size_t next_sector = 0;
    extract_ndef(out, data, capacity, [&](std::size_t) -> std::optional<Error> {
        if (next_sector >= sectors.size()) {
            return Error::Protocol;
        }
        const std::uint8_t sector = sectors[next_sector++];
        const std::uint8_t first_block = tag_memory::classic_first_block(sector);
        if (const auto auth =
                frontend_->classic_auth(first_block, KeyType::A, tag_memory::kNdefKey);
            !auth) {
            return auth.error();
        }
        for (std::uint8_t i = 0; i < tag_memory::classic_data_blocks(sector); ++i) {
            const auto block = frontend_->mifare_read(static_cast<std::uint8_t>(first_block + i));
            if (!block) {
                return block.error();
            }
            data.insert(data.end(), block->begin(), block->end());
        }
        return std::nullopt;
    });
}

std::expected<void, Error> Type2TagReader::write_ndef(ByteView) {
    return std::unexpected(Error::NotSupported);  // milestone 2
}

}  // namespace tapwave::nfc
