#include "doctest_main.hpp"

#include <array>
#include <deque>
#include <memory>

#include "drivers/nfc/common/iso14443a.hpp"
#include "drivers/nfc/common/rc522.hpp"
#include "drivers/nfc/common/type2_tag_reader.hpp"
#include "fake_tags.hpp"

using namespace tapwave;
using namespace tapwave::nfc;
using namespace tapwave::test;
namespace reg = tapwave::nfc::rc522;

namespace {

// A simulated MFRC522: registers, FIFO, the Transceive and MFAuthent commands, the timer
// interrupt, and a tag in the field with the ISO 14443-3 state machine (idle, ready, active,
// halt) and cascaded anticollision.
class FakeRc522 final : public Rc522Bus {
public:
    FakeTag* tag = nullptr;
    std::uint8_t version = 0x92;
    int hard_resets = 0;

    enum class TagState { Idle, Ready, Active, Halt };
    TagState state = TagState::Idle;

    std::string_view bus() const override { return "test"; }
    void hard_reset() override { ++hard_resets; }
    std::uint32_t now_ms() override { return clock_++; }
    void sleep_ms(std::uint32_t ms) override { clock_ += ms; }

    std::uint8_t read(std::uint8_t r) override {
        if (r == reg::kVersionReg) {
            return version;
        }
        if (r == reg::kFifoLevelReg) {
            return static_cast<std::uint8_t>(fifo_.size());
        }
        return regs_[r];
    }

    void write(std::uint8_t r, std::uint8_t value) override {
        if (r == reg::kComIrqReg || r == reg::kDivIrqReg) {
            // Bit 7 chooses whether the marked bits are set or cleared.
            const auto mask = static_cast<std::uint8_t>(value & 0x7F);
            regs_[r] = (value & 0x80) ? static_cast<std::uint8_t>(regs_[r] | mask)
                                      : static_cast<std::uint8_t>(regs_[r] & ~mask);
            return;
        }
        regs_[r] = value;
        if (r == reg::kFifoLevelReg && (value & 0x80)) {
            fifo_.clear();
        } else if (r == reg::kCommandReg && value == reg::kSoftReset) {
            regs_[reg::kCommandReg] = 0x00;
        } else if (r == reg::kCommandReg && value == reg::kMfAuthent) {
            authenticate();
        } else if (r == reg::kBitFramingReg && (value & 0x80) &&
                   regs_[reg::kCommandReg] == reg::kTransceive) {
            transceive(value & 0x07);
        }
    }

    void read_fifo(std::span<std::uint8_t> out) override {
        for (auto& b : out) {
            b = fifo_.front();
            fifo_.pop_front();
        }
    }

    void write_fifo(ByteView data) override { fifo_.insert(fifo_.end(), data.begin(), data.end()); }

private:
    struct Reply {
        Bytes data;
        std::uint8_t last_bits = 0;
    };

    void transceive(std::uint8_t tx_bits) {
        const Bytes frame(fifo_.begin(), fifo_.end());
        fifo_.clear();
        regs_[reg::kErrorReg] = 0;
        const auto reply = card(frame, tx_bits);
        if (!reply) {
            regs_[reg::kComIrqReg] |= 0x01;  // timer: no answer
            return;
        }
        fifo_.assign(reply->data.begin(), reply->data.end());
        regs_[reg::kControlReg] = reply->last_bits;
        regs_[reg::kComIrqReg] |= 0x30;
    }

    std::optional<Reply> card(const Bytes& frame, std::uint8_t tx_bits) {
        if (!tag) {
            return std::nullopt;
        }
        if (tx_bits == 7 && frame.size() == 1) {
            const bool wakes = frame[0] == iso14443a::kWupA
                                   ? (state == TagState::Idle || state == TagState::Halt)
                                   : state == TagState::Idle;
            if (!wakes) {
                return std::nullopt;
            }
            state = TagState::Ready;
            level_ = 0;
            return Reply{{static_cast<std::uint8_t>(tag->atqa & 0xFF),
                          static_cast<std::uint8_t>(tag->atqa >> 8)}};
        }
        if (state == TagState::Ready && frame.size() == 2 && frame[1] == 0x20) {
            const auto level = cascade(level_);
            return Reply{Bytes(level.begin(), level.end())};
        }
        if (state == TagState::Ready && frame.size() == 9 && frame[1] == 0x70) {
            if (!iso14443a::check_crc(frame)) {
                return std::nullopt;
            }
            const bool last = levels() == level_ + 1;
            ++level_;
            if (last) {
                state = TagState::Active;
            }
            return Reply{iso14443a::with_crc(Bytes{last ? tag->sak : std::uint8_t{0x04}})};
        }
        const auto body = iso14443a::check_crc(frame);
        if (!body) {
            return std::nullopt;
        }
        if (body->size() == 2 && (*body)[0] == iso14443a::kHltA) {
            state = TagState::Halt;
            return std::nullopt;
        }
        if (state != TagState::Active) {
            return std::nullopt;
        }
        const auto answer = tag->answer(*body);
        if (!answer) {
            if (tag->nak) {
                return Reply{{0x00}, 4};
            }
            return std::nullopt;
        }
        if (answer->size() == 1 && (*answer)[0] == iso14443a::kAck) {
            return Reply{{0x0A}, 4};
        }
        return Reply{iso14443a::with_crc(*answer)};
    }

    int levels() const { return tag->uid.size() == 4 ? 1 : tag->uid.size() == 7 ? 2 : 3; }

    iso14443a::CascadeLevel cascade(int level) const {
        iso14443a::CascadeLevel out{};
        const Bytes& u = tag->uid;
        if (levels() == 1) {
            out = {u[0], u[1], u[2], u[3], 0};
        } else if (levels() == 2) {
            out = level == 0 ? iso14443a::CascadeLevel{0x88, u[0], u[1], u[2], 0}
                             : iso14443a::CascadeLevel{u[3], u[4], u[5], u[6], 0};
        }
        out[4] = static_cast<std::uint8_t>(out[0] ^ out[1] ^ out[2] ^ out[3]);
        return out;
    }

    void authenticate() {
        const Bytes frame(fifo_.begin(), fifo_.end());
        fifo_.clear();
        auto* classic = dynamic_cast<FakeClassicTag*>(tag);
        tag_memory::ClassicKey key{};
        std::copy(frame.begin() + 2, frame.begin() + 8, key.begin());
        auth_uid = Bytes(frame.begin() + 8, frame.end());
        if (classic && state == TagState::Active && classic->authenticate(frame[1], key)) {
            regs_[reg::kStatus2Reg] |= 0x08;
            regs_[reg::kComIrqReg] |= 0x10;
        } else {
            state = TagState::Idle;
            regs_[reg::kComIrqReg] |= 0x01;
        }
    }

    std::array<std::uint8_t, 64> regs_{};
    std::deque<std::uint8_t> fifo_;
    std::uint32_t clock_ = 0;
    int level_ = 0;

public:
    Bytes auth_uid;
};

struct Rig {
    FakeRc522* chip;
    std::unique_ptr<Rc522> rc522;

    Rig() {
        auto bus = std::make_unique<FakeRc522>();
        chip = bus.get();
        rc522 = std::make_unique<Rc522>(std::move(bus), "rc522-test");
    }
};

}  // namespace

TEST_CASE("begin recognises MFRC522 chips and common clones") {
    for (std::uint8_t version : {0x91, 0x92, 0x88, 0xB2, 0x12}) {
        Rig rig;
        rig.chip->version = version;
        CHECK_MESSAGE(rig.rc522->begin(), static_cast<int>(version));
    }
    Rig rig;
    REQUIRE(rig.rc522->begin());
    CHECK(rig.chip->hard_resets == 1);
    CHECK(rig.rc522->info().summary() == "RC522 2.0 (test)");
}

TEST_CASE("begin fails on an empty bus") {
    for (std::uint8_t version : {0x00, 0xFF, 0x17}) {
        Rig rig;
        rig.chip->version = version;
        CHECK(rig.rc522->begin().error() == Error::NotSupported);
    }
}

TEST_CASE("version names") {
    CHECK(rc522::version_name(0x91) == "1.0");
    CHECK(rc522::version_name(0x88) == "FM17522");
    CHECK(rc522::version_name(0x12) == "0x12");
}

TEST_CASE("selects a seven-byte UID through two cascade levels") {
    Rig rig;
    REQUIRE(rig.rc522->begin());
    CHECK(rig.rc522->select().error() == Error::NoTag);

    FakeType2Tag tag = FakeType2Tag::ntag(213, std::nullopt);
    rig.chip->tag = &tag;
    const auto target = rig.rc522->select();
    REQUIRE(target);
    CHECK(target->uid.to_string() == "04-A2-1B-3C-4D-5E-80");
    CHECK(target->atqa == 0x0044);
    CHECK(target->sak == 0x00);
    CHECK(rig.chip->state == FakeRc522::TagState::Active);
}

TEST_CASE("selecting again halts and wakes the same tag") {
    Rig rig;
    REQUIRE(rig.rc522->begin());
    FakeType2Tag tag = FakeType2Tag::ntag(213, std::nullopt);
    rig.chip->tag = &tag;
    REQUIRE(rig.rc522->select());
    REQUIRE(rig.rc522->select());
    REQUIRE(rig.rc522->select());
    rig.chip->tag = nullptr;
    CHECK(rig.rc522->select().error() == Error::NoTag);
}

TEST_CASE("Type2TagReader over an RC522 reads an NTAG") {
    Rig rig;
    REQUIRE(rig.rc522->begin());
    const Bytes message =
        ndef_message({ndef::uri_record("https://www.home-assistant.io/tag/kitchen")});
    FakeType2Tag tag = FakeType2Tag::ntag(216, message);
    rig.chip->tag = &tag;
    Type2TagReader reader(std::move(rig.rc522));
    const auto read = reader.read();
    REQUIRE(read);
    CHECK(read->family == TagFamily::Ntag216);
    REQUIRE(read->ndef);
    CHECK(*read->ndef == message);
}

TEST_CASE("Classic over an RC522: four-byte UID and authentication") {
    Rig rig;
    REQUIRE(rig.rc522->begin());
    const Bytes message = ndef_message({ndef::text_record(std::string(60, 'c'))});
    FakeClassicTag tag = FakeClassicTag::ndef_formatted(message);
    rig.chip->tag = &tag;
    auto* chip = rig.chip;
    Type2TagReader reader(std::move(rig.rc522));
    const auto read = reader.read();
    REQUIRE(read);
    CHECK(read->family == TagFamily::MifareClassic1k);
    CHECK(read->target.uid.to_string() == "DE-AD-BE-EF");
    REQUIRE(read->ndef);
    CHECK(*read->ndef == message);
    CHECK(chip->auth_uid == Bytes{0xDE, 0xAD, 0xBE, 0xEF});
}

TEST_CASE("a NAK from the tag is reported") {
    Rig rig;
    REQUIRE(rig.rc522->begin());
    FakeType2Tag tag = FakeType2Tag::ntag(213, std::nullopt);
    tag.version.reset();
    rig.chip->tag = &tag;
    REQUIRE(rig.rc522->select());
    CHECK(rig.rc522->transceive(Bytes{0x60}).error() == Error::NotSupported);
}

TEST_CASE("page writes and their ACK") {
    Rig rig;
    REQUIRE(rig.rc522->begin());
    FakeType2Tag tag = FakeType2Tag::ntag(213, std::nullopt);
    rig.chip->tag = &tag;
    REQUIRE(rig.rc522->select());
    REQUIRE(rig.rc522->ultralight_write(5, Page{1, 2, 3, 4}));
    CHECK(tag.pages[5] == std::array<std::uint8_t, 4>{1, 2, 3, 4});
    CHECK(rig.rc522->ultralight_write(1, Page{}).error() == Error::ReadOnly);
}

TEST_CASE("commands need a selected tag") {
    Rig rig;
    REQUIRE(rig.rc522->begin());
    CHECK(rig.rc522->mifare_read(4).error() == Error::NoTag);
    CHECK(rig.rc522->classic_auth(4, KeyType::A, tag_memory::kNdefKey).error() == Error::NoTag);
}
