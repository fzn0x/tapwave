#include "doctest_main.hpp"

#include <deque>
#include <memory>

#include "drivers/nfc/common/pn532.hpp"
#include "drivers/nfc/common/pn532_frame.hpp"
#include "drivers/nfc/common/type2_tag_reader.hpp"
#include "fake_tags.hpp"

using namespace tapwave;
using namespace tapwave::nfc;
using namespace tapwave::test;

namespace {

// A simulated PN532 behind a transport: it decodes host frames, ACKs them and answers the
// commands Tapwave uses, talking to a simulated tag.
class FakePn532 final : public Pn532Transport {
public:
    FakeTag* tag = nullptr;
    bool present = true;  // false: nothing on the bus answers
    std::uint8_t ic = 0x32;
    bool nack_once = false;  // answer the next command with a NACK instead of an ACK
    int field_resets = 0;
    int wakes = 0;
    std::vector<std::uint8_t> commands;

    std::string_view bus() const override { return "test"; }
    void wake() override { ++wakes; }
    void sleep_ms(std::uint32_t) override {}

    std::expected<void, Error> write(ByteView frame) override {
        if (!present) {
            return {};
        }
        // 00 00 FF LEN LCS D4 CMD PARAMS DCS 00
        REQUIRE(frame.size() >= 9);
        const std::size_t length = frame[3];
        REQUIRE(static_cast<std::uint8_t>(frame[3] + frame[4]) == 0);
        REQUIRE(frame[5] == pn532::kHostToPn532);
        const std::uint8_t code = frame[6];
        const Bytes params(frame.begin() + 7, frame.begin() + 5 + static_cast<long>(length));
        if (nack_once) {
            nack_once = false;
            outbox_.emplace_back(pn532::kNack.begin(), pn532::kNack.end());
            return {};
        }
        commands.push_back(code);
        outbox_.emplace_back(pn532::kAck.begin(), pn532::kAck.end());
        outbox_.push_back(
            pn532::build_response(static_cast<std::uint8_t>(code + 1), handle(code, params)));
        return {};
    }

    std::expected<Bytes, Error> read(std::uint32_t, std::size_t max_length) override {
        if (outbox_.empty()) {
            return std::unexpected(Error::Timeout);
        }
        Bytes out = std::move(outbox_.front());
        outbox_.pop_front();
        REQUIRE(out.size() <= max_length);
        return out;
    }

private:
    Bytes handle(std::uint8_t code, const Bytes& params) {
        switch (code) {
            case pn532::kGetFirmwareVersion:
                return {ic, 0x01, 0x06, 0x07};
            case pn532::kSamConfiguration:
                return {};
            case pn532::kRfConfiguration:
                if (params.size() == 2 && params[0] == 0x01 && params[1] == 0x00) {
                    ++field_resets;
                    active_ = false;
                }
                return {};
            case pn532::kInListPassiveTarget: {
                // A tag left selected ignores new activation until the field is reset.
                if (!tag || active_) {
                    return {0x00};
                }
                active_ = true;
                Bytes out{0x01,
                          0x01,
                          static_cast<std::uint8_t>(tag->atqa >> 8),
                          static_cast<std::uint8_t>(tag->atqa & 0xFF),
                          tag->sak,
                          static_cast<std::uint8_t>(tag->uid.size())};
                out.insert(out.end(), tag->uid.begin(), tag->uid.end());
                return out;
            }
            case pn532::kInDataExchange: {
                if (params.size() >= 13 && (params[1] == 0x60 || params[1] == 0x61)) {
                    auto* classic = dynamic_cast<FakeClassicTag*>(tag);
                    tapwave::tag_memory::ClassicKey key{};
                    std::copy(params.begin() + 3, params.begin() + 9, key.begin());
                    last_auth_uid = Bytes(params.begin() + 9, params.end());
                    return {static_cast<std::uint8_t>(
                        classic && classic->authenticate(params[2], key) ? 0x00 : 0x14)};
                }
                return relay(ByteView(params).subspan(1));
            }
            case pn532::kInCommunicateThru:
                return relay(params);
            default:
                return {0x27};
        }
    }

    Bytes relay(ByteView command) {
        if (!tag || !active_) {
            return {0x01};
        }
        const auto answer = tag->answer(command);
        if (!answer) {
            return {0x01};
        }
        Bytes out{0x00};
        out.insert(out.end(), answer->begin(), answer->end());
        return out;
    }

    bool active_ = false;
    std::deque<Bytes> outbox_;

public:
    Bytes last_auth_uid;
};

struct Rig {
    FakePn532* chip;
    std::unique_ptr<Pn532> pn532;

    Rig() {
        auto transport = std::make_unique<FakePn532>();
        chip = transport.get();
        pn532 = std::make_unique<Pn532>(std::move(transport), "pn532-test");
    }
};

}  // namespace

TEST_CASE("begin identifies the chip and configures it") {
    Rig rig;
    REQUIRE(rig.pn532->begin());
    CHECK(rig.chip->wakes == 1);
    CHECK(rig.pn532->info().firmware == "1.6");
    CHECK(rig.pn532->info().summary() == "PN532 1.6 (test)");
    CHECK(rig.chip->commands == std::vector<std::uint8_t>{0x02, 0x14, 0x32});
}

TEST_CASE("begin fails when no PN532 answers, which is how probing works") {
    Rig rig;
    rig.chip->present = false;
    CHECK(rig.pn532->begin().error() == Error::Timeout);
}

TEST_CASE("begin refuses other chips") {
    Rig rig;
    rig.chip->ic = 0x33;
    CHECK(rig.pn532->begin().error() == Error::NotSupported);
}

TEST_CASE("a NACK makes the driver send the command again") {
    Rig rig;
    rig.chip->nack_once = true;
    REQUIRE(rig.pn532->begin());
    CHECK(rig.pn532->info().firmware == "1.6");
}

TEST_CASE("select finds a tag, and reselecting resets the field first") {
    Rig rig;
    REQUIRE(rig.pn532->begin());
    CHECK(rig.pn532->select().error() == Error::NoTag);

    FakeType2Tag tag = FakeType2Tag::ntag(213, std::nullopt);
    rig.chip->tag = &tag;
    const auto first = rig.pn532->select();
    REQUIRE(first);
    CHECK(first->uid.to_string() == "04-A2-1B-3C-4D-5E-80");
    CHECK(first->atqa == 0x0044);
    CHECK(first->sak == 0x00);
    CHECK(rig.chip->field_resets == 0);

    REQUIRE(rig.pn532->select());  // the same tag, still on the reader
    CHECK(rig.chip->field_resets == 1);

    rig.chip->tag = nullptr;
    CHECK(rig.pn532->select().error() == Error::NoTag);
}

TEST_CASE("Type2TagReader over a PN532 reads an NTAG") {
    Rig rig;
    REQUIRE(rig.pn532->begin());
    const Bytes message = ndef_message({ndef::uri_record("tapwave://stream/https://x/y.mp3"),
                                        ndef::text_record(std::string(120, 'r'))});
    FakeType2Tag tag = FakeType2Tag::ntag(215, message);
    rig.chip->tag = &tag;
    Type2TagReader reader(std::move(rig.pn532));
    const auto read = reader.read();
    REQUIRE(read);
    CHECK(read->family == TagFamily::Ntag215);
    REQUIRE(read->ndef);
    CHECK(*read->ndef == message);
}

TEST_CASE("Classic authentication sends the last four UID bytes") {
    Rig rig;
    REQUIRE(rig.pn532->begin());
    const Bytes message = ndef_message({ndef::text_record("classic")});
    FakeClassicTag tag = FakeClassicTag::ndef_formatted(message);
    tag.uid = {0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    rig.chip->tag = &tag;
    auto* chip = rig.chip;
    Type2TagReader reader(std::move(rig.pn532));
    const auto read = reader.read();
    REQUIRE(read);
    REQUIRE(read->ndef);
    CHECK(*read->ndef == message);
    CHECK(chip->last_auth_uid == Bytes{0x33, 0x44, 0x55, 0x66});
}

TEST_CASE("tag errors map from the PN532 status byte") {
    CHECK(pn532_status_error(0x01) == Error::Timeout);
    CHECK(pn532_status_error(0x02) == Error::Crc);
    CHECK(pn532_status_error(0x14) == Error::AuthFailed);
    CHECK(pn532_status_error(0x54) == Error::AuthFailed);  // the top bits are flags
    CHECK(pn532_status_error(0x27) == Error::NoTag);
    CHECK(pn532_status_error(0x13) == Error::Protocol);
}

TEST_CASE("commands need a selected tag") {
    Rig rig;
    REQUIRE(rig.pn532->begin());
    CHECK(rig.pn532->mifare_read(4).error() == Error::NoTag);
    CHECK(rig.pn532->transceive(Bytes{0x60}).error() == Error::NoTag);
}

TEST_CASE("page writes reach the tag") {
    Rig rig;
    REQUIRE(rig.pn532->begin());
    FakeType2Tag tag = FakeType2Tag::ntag(213, std::nullopt);
    rig.chip->tag = &tag;
    REQUIRE(rig.pn532->select());
    REQUIRE(rig.pn532->ultralight_write(4, Page{0x03, 0x00, 0xFE, 0x00}));
    CHECK(tag.pages[4] == std::array<std::uint8_t, 4>{0x03, 0x00, 0xFE, 0x00});
    CHECK(rig.pn532->ultralight_write(2, Page{}).error() == Error::Timeout);  // locked area
}
