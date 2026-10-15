// Simulated tags for driver tests: an NFC Forum Type 2 tag (NTAG, Ultralight) and a MIFARE
// Classic tag. They answer raw ISO 14443-A commands (without CRC) the way real tags do, so the
// same tag can sit behind a fake PN532, a fake RC522 or a fake frontend.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "core/bytes.hpp"
#include "core/ndef.hpp"
#include "core/tag_memory.hpp"
#include "core/uid.hpp"

namespace tapwave::test {

struct FakeTag {
    Bytes uid;
    std::uint16_t atqa = 0x0044;
    std::uint8_t sak = 0x00;
    int reads = 0;  // READ commands answered, to check readers do not over-read

    virtual ~FakeTag() = default;

    // Answers a command. nullopt means the tag stays silent (or NAKs, see `nak`).
    virtual std::optional<Bytes> answer(ByteView command) = 0;
    bool nak = false;  // set when the last answer was a four-bit NAK
};

// NTAG21x or MIFARE Ultralight with `pages` four-byte pages.
struct FakeType2Tag : FakeTag {
    std::vector<std::array<std::uint8_t, 4>> pages;
    std::optional<Bytes> version;  // GET_VERSION answer; nullopt for tags that NAK it

    // An NTAG of the given model holding `message` (or nothing) in an NDEF TLV.
    static FakeType2Tag ntag(int model, std::optional<Bytes> message) {
        FakeType2Tag tag;
        tag.uid = {0x04, 0xA2, 0x1B, 0x3C, 0x4D, 0x5E, 0x80};
        std::uint8_t size = 0x12;
        std::size_t total_pages = 45;
        std::uint8_t storage = 0x0F;
        if (model == 215) {
            size = 0x3E;
            total_pages = 135;
            storage = 0x11;
        } else if (model == 216) {
            size = 0x6D;
            total_pages = 231;
            storage = 0x13;
        }
        tag.pages.assign(total_pages, {0, 0, 0, 0});
        tag.pages[3] = {0xE1, 0x10, size, 0x00};
        tag.version = Bytes{0x00, 0x04, 0x04, 0x02, 0x01, 0x00, storage, 0x03};
        Bytes data;
        if (message) {
            data.push_back(0x03);
            if (message->size() < 0xFF) {
                data.push_back(static_cast<std::uint8_t>(message->size()));
            } else {
                data.push_back(0xFF);
                data.push_back(static_cast<std::uint8_t>(message->size() >> 8));
                data.push_back(static_cast<std::uint8_t>(message->size() & 0xFF));
            }
            data.insert(data.end(), message->begin(), message->end());
        } else {
            data = {0x03, 0x00};
        }
        data.push_back(0xFE);
        tag.write_data_area(data);
        return tag;
    }

    void write_data_area(ByteView data) {
        for (std::size_t i = 0; i < data.size(); ++i) {
            pages[4 + i / 4][i % 4] = data[i];
        }
    }

    std::optional<Bytes> answer(ByteView command) override {
        nak = false;
        if (command.size() == 2 && command[0] == 0x30) {
            ++reads;
            Bytes out;
            for (std::size_t i = 0; i < 4; ++i) {
                const auto& page = pages[(command[1] + i) % pages.size()];  // reads wrap around
                out.insert(out.end(), page.begin(), page.end());
            }
            return out;
        }
        if (command.size() == 6 && command[0] == 0xA2) {
            if (command[1] < 4 || command[1] >= pages.size()) {
                nak = true;
                return std::nullopt;
            }
            pages[command[1]] = {command[2], command[3], command[4], command[5]};
            return Bytes{0x0A};
        }
        if (command.size() == 1 && command[0] == 0x60) {
            if (!version) {
                nak = true;
                return std::nullopt;
            }
            return version;
        }
        nak = true;
        return std::nullopt;
    }
};

// MIFARE Classic 1K: 64 blocks, key A per sector.
struct FakeClassicTag : FakeTag {
    std::vector<std::array<std::uint8_t, 16>> blocks =
        std::vector<std::array<std::uint8_t, 16>>(64);
    std::vector<tag_memory::ClassicKey> key_a = std::vector<tag_memory::ClassicKey>(16);
    std::optional<std::uint8_t> authenticated_sector;

    static FakeClassicTag ndef_formatted(ByteView message) {
        FakeClassicTag tag;
        tag.uid = {0xDE, 0xAD, 0xBE, 0xEF};
        tag.atqa = 0x0004;
        tag.sak = 0x08;
        tag.key_a[0] = tag_memory::kMadKey;
        for (std::size_t s = 1; s < 16; ++s) {
            tag.key_a[s] = tag_memory::kNdefKey;
        }
        // MAD1: CRC, info, then 03 E1 for sectors 1 to 15.
        std::array<std::uint8_t, 32> mad{};
        mad[0] = 0x14;
        mad[1] = 0x01;
        for (std::size_t i = 2; i < 32; i += 2) {
            mad[i] = 0x03;
            mad[i + 1] = 0xE1;
        }
        std::copy(mad.begin(), mad.begin() + 16, tag.blocks[1].begin());
        std::copy(mad.begin() + 16, mad.end(), tag.blocks[2].begin());
        Bytes data{0x03};
        if (message.size() < 0xFF) {
            data.push_back(static_cast<std::uint8_t>(message.size()));
        } else {
            data.push_back(0xFF);
            data.push_back(static_cast<std::uint8_t>(message.size() >> 8));
            data.push_back(static_cast<std::uint8_t>(message.size() & 0xFF));
        }
        data.insert(data.end(), message.begin(), message.end());
        data.push_back(0xFE);
        std::size_t pos = 0;
        for (std::uint8_t sector = 1; sector < 16 && pos < data.size(); ++sector) {
            for (std::uint8_t b = 0; b < 3 && pos < data.size(); ++b) {
                auto& block = tag.blocks[sector * 4 + b];
                for (std::size_t i = 0; i < 16 && pos < data.size(); ++i) {
                    block[i] = data[pos++];
                }
            }
        }
        return tag;
    }

    bool authenticate(std::uint8_t block, const tag_memory::ClassicKey& key) {
        const std::uint8_t sector = block / 4;
        if (sector >= 16 || key != key_a[sector]) {
            authenticated_sector.reset();
            return false;
        }
        authenticated_sector = sector;
        return true;
    }

    std::optional<Bytes> answer(ByteView command) override {
        nak = false;
        if (command.size() == 2 && command[0] == 0x30) {
            if (!authenticated_sector || command[1] / 4 != *authenticated_sector) {
                nak = true;
                return std::nullopt;
            }
            ++reads;
            const auto& block = blocks[command[1]];
            return Bytes(block.begin(), block.end());
        }
        nak = true;
        return std::nullopt;
    }
};

inline Bytes ndef_message(std::initializer_list<ndef::Record> records) {
    return ndef::encode(std::vector<ndef::Record>(records));
}

}  // namespace tapwave::test
