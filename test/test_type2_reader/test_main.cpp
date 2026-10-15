#include "doctest_main.hpp"

#include <memory>

#include "core/scan.hpp"
#include "drivers/nfc/common/type2_tag_reader.hpp"
#include "fake_tags.hpp"

using namespace tapwave;
using namespace tapwave::nfc;
using namespace tapwave::test;

namespace {

// A frontend that talks straight to a simulated tag, with no reader chip in between.
class FakeFrontend final : public Iso14443aReader {
public:
    FakeTag* tag = nullptr;  // nullptr: empty field
    std::optional<Error> fail_select;
    int selects = 0;

    const ReaderInfo& info() const override { return info_; }

    std::expected<Target, Error> select() override {
        ++selects;
        if (fail_select) {
            return std::unexpected(*fail_select);
        }
        if (!tag) {
            return std::unexpected(Error::NoTag);
        }
        return Target{*Uid::from(tag->uid), tag->atqa, tag->sak};
    }

    std::expected<Block, Error> mifare_read(std::uint8_t block) override {
        const auto data = ask(Bytes{0x30, block});
        if (!data) {
            return std::unexpected(data.error());
        }
        Block out{};
        std::copy(data->begin(), data->end(), out.begin());
        return out;
    }

    std::expected<void, Error> ultralight_write(std::uint8_t page, const Page& d) override {
        const auto data = ask(Bytes{0xA2, page, d[0], d[1], d[2], d[3]});
        if (!data) {
            return std::unexpected(data.error());
        }
        return {};
    }

    std::expected<void, Error> classic_auth(std::uint8_t block, KeyType,
                                            const ClassicKey& key) override {
        auto* classic = dynamic_cast<FakeClassicTag*>(tag);
        if (!classic || !classic->authenticate(block, key)) {
            return std::unexpected(Error::AuthFailed);
        }
        return {};
    }

    std::expected<Bytes, Error> transceive(ByteView command) override { return ask(command); }

private:
    std::expected<Bytes, Error> ask(ByteView command) {
        if (!tag) {
            return std::unexpected(Error::NoTag);
        }
        auto answer = tag->answer(command);
        if (!answer) {
            return std::unexpected(tag->nak ? Error::NotSupported : Error::Timeout);
        }
        return *answer;
    }

    ReaderInfo info_{"fake", "Fake", "test", "1.0"};
};

struct Rig {
    FakeFrontend* frontend;
    Type2TagReader reader;

    Rig() : Rig(std::make_unique<FakeFrontend>()) {}
    explicit Rig(std::unique_ptr<FakeFrontend> f) : frontend(f.get()), reader(std::move(f)) {}
};

const Bytes kHaMessage =
    ndef_message({ndef::uri_record("https://www.home-assistant.io/tag/8f3b2c1e-6a5d")});

}  // namespace

TEST_CASE("detect reports the UID or an empty field") {
    Rig rig;
    auto empty = rig.reader.detect();
    REQUIRE(empty);
    CHECK_FALSE(empty->has_value());

    FakeType2Tag tag = FakeType2Tag::ntag(215, kHaMessage);
    rig.frontend->tag = &tag;
    const auto found = rig.reader.detect();
    REQUIRE(found);
    REQUIRE(found->has_value());
    CHECK((*found)->to_string() == "04-A2-1B-3C-4D-5E-80");
}

TEST_CASE("detect treats a garbled answer as an empty field, but not a bus failure") {
    Rig rig;
    rig.frontend->fail_select = Error::Crc;
    CHECK(rig.reader.detect().value() == std::nullopt);
    rig.frontend->fail_select = Error::Collision;
    CHECK(rig.reader.detect().value() == std::nullopt);
    rig.frontend->fail_select = Error::Transport;
    CHECK(rig.reader.detect().error() == Error::Transport);
}

TEST_CASE("reads an NTAG215 written by the Home Assistant app") {
    Rig rig;
    FakeType2Tag tag = FakeType2Tag::ntag(215, kHaMessage);
    rig.frontend->tag = &tag;
    const auto read = rig.reader.read();
    REQUIRE(read);
    CHECK(read->family == TagFamily::Ntag215);
    CHECK(read->capacity == 496);
    CHECK(read->writable);
    REQUIRE(read->ndef);
    CHECK(*read->ndef == kHaMessage);
    CHECK_FALSE(read->ndef_issue);

    const auto records = ndef::parse(*read->ndef);
    REQUIRE(records);
    const Scan scan = classify(read->target.uid.to_string(), "NTAG215", *records);
    CHECK(scan.id() == "8f3b2c1e-6a5d");
}

TEST_CASE("reads only the pages the message needs") {
    Rig rig;
    FakeType2Tag tag = FakeType2Tag::ntag(216, ndef_message({ndef::text_record("hi")}));
    rig.frontend->tag = &tag;
    REQUIRE(rig.reader.read());
    CHECK(tag.reads == 1);  // READ 3 covers the CC and the whole 9-byte message
}

TEST_CASE("reads a message that spans many pages and a long TLV") {
    Rig rig;
    const Bytes long_message = ndef_message({ndef::text_record(std::string(400, 'x'))});
    REQUIRE(long_message.size() > 0xFF);
    FakeType2Tag tag = FakeType2Tag::ntag(215, long_message);
    rig.frontend->tag = &tag;
    const auto read = rig.reader.read();
    REQUIRE(read);
    REQUIRE(read->ndef);
    CHECK(*read->ndef == long_message);
}

TEST_CASE("a formatted, empty tag is blank, not an error") {
    Rig rig;
    FakeType2Tag tag = FakeType2Tag::ntag(213, std::nullopt);
    rig.frontend->tag = &tag;
    const auto read = rig.reader.read();
    REQUIRE(read);
    CHECK(read->family == TagFamily::Ntag213);
    REQUIRE(read->ndef);
    CHECK(read->ndef->empty());
    CHECK_FALSE(read->ndef_issue);
}

TEST_CASE("a tag without a terminator or NDEF TLV is blank") {
    Rig rig;
    FakeType2Tag tag = FakeType2Tag::ntag(213, std::nullopt);
    tag.write_data_area(Bytes{0xFE});
    rig.frontend->tag = &tag;
    const auto read = rig.reader.read();
    REQUIRE(read);
    REQUIRE(read->ndef);
    CHECK(read->ndef->empty());
}

TEST_CASE("an unformatted tag reports why") {
    Rig rig;
    FakeType2Tag tag = FakeType2Tag::ntag(213, std::nullopt);
    tag.pages[3] = {0, 0, 0, 0};
    rig.frontend->tag = &tag;
    const auto read = rig.reader.read();
    REQUIRE(read);
    CHECK_FALSE(read->ndef);
    REQUIRE(read->ndef_issue);
    CHECK(read->ndef_issue->find("not formatted") != std::string::npos);
    CHECK(read->family == TagFamily::Ntag213);
}

TEST_CASE("a TLV longer than the tag is reported as damaged") {
    Rig rig;
    FakeType2Tag tag = FakeType2Tag::ntag(213, std::nullopt);
    tag.write_data_area(Bytes{0x03, 0xF0});
    rig.frontend->tag = &tag;
    const auto read = rig.reader.read();
    REQUIRE(read);
    CHECK_FALSE(read->ndef);
    REQUIRE(read->ndef_issue);
    CHECK(read->ndef_issue->find("damaged") != std::string::npos);
}

TEST_CASE("tags that NAK GET_VERSION are named from their capability container") {
    Rig rig;
    FakeType2Tag ultralight = FakeType2Tag::ntag(213, ndef_message({ndef::text_record("ok")}));
    ultralight.pages.resize(16);
    ultralight.pages[3] = {0xE1, 0x10, 0x06, 0x00};
    ultralight.version.reset();
    rig.frontend->tag = &ultralight;
    const auto read = rig.reader.read();
    REQUIRE(read);
    CHECK(read->family == TagFamily::Ultralight);
    REQUIRE(read->ndef);

    FakeType2Tag other = FakeType2Tag::ntag(213, std::nullopt);
    other.version.reset();
    rig.frontend->tag = &other;
    CHECK(rig.reader.read()->family == TagFamily::Type2);
}

TEST_CASE("reads NDEF from a MIFARE Classic tag with the public keys") {
    Rig rig;
    const Bytes message = ndef_message(
        {ndef::uri_record("tapwave://sd/Music"), ndef::text_record(std::string(100, 'y'))});
    FakeClassicTag tag = FakeClassicTag::ndef_formatted(message);
    rig.frontend->tag = &tag;
    const auto read = rig.reader.read();
    REQUIRE(read);
    CHECK(read->family == TagFamily::MifareClassic1k);
    CHECK_FALSE(read->writable);
    CHECK(read->capacity == 720);
    REQUIRE(read->ndef);
    CHECK(*read->ndef == message);
    CHECK(read->target.uid.to_string() == "DE-AD-BE-EF");
}

TEST_CASE("MIFARE Classic without the NFC Forum keys has only a UID") {
    Rig rig;
    FakeClassicTag tag = FakeClassicTag::ndef_formatted(Bytes{0xD0, 0x00, 0x00});
    tag.key_a[0] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // a factory-fresh card
    rig.frontend->tag = &tag;
    const auto read = rig.reader.read();
    REQUIRE(read);
    CHECK_FALSE(read->ndef);
    CHECK(read->ndef_issue);
}

TEST_CASE("an NDEF sector with a changed key stops the read") {
    Rig rig;
    FakeClassicTag tag =
        FakeClassicTag::ndef_formatted(ndef_message({ndef::text_record(std::string(80, 'z'))}));
    tag.key_a[2] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    rig.frontend->tag = &tag;
    const auto read = rig.reader.read();
    REQUIRE(read);
    CHECK_FALSE(read->ndef);
    REQUIRE(read->ndef_issue);
    CHECK(read->ndef_issue->find("authentication") != std::string::npos);
}

TEST_CASE("other ISO 14443-4 tags are identified but not read") {
    Rig rig;
    FakeType2Tag phone = FakeType2Tag::ntag(213, std::nullopt);
    phone.sak = 0x20;
    phone.uid = {0x08, 0x12, 0x34, 0x56};
    rig.frontend->tag = &phone;
    const auto read = rig.reader.read();
    REQUIRE(read);
    CHECK(read->family == TagFamily::Iso14443_4);
    CHECK_FALSE(read->ndef);
    CHECK(read->ndef_issue);
    CHECK(phone.reads == 0);
}

TEST_CASE("read fails cleanly when the tag leaves") {
    Rig rig;
    CHECK(rig.reader.read().error() == Error::NoTag);
}

TEST_CASE("writing is not available yet") {
    Rig rig;
    CHECK(rig.reader.write_ndef(Bytes{0xD0, 0x00, 0x00}).error() == Error::NotSupported);
    CHECK(rig.reader.info().model == "Fake");
}
