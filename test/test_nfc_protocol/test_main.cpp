#include "doctest_main.hpp"

#include "drivers/nfc/common/iso14443a.hpp"
#include "drivers/nfc/common/nfc_types.hpp"
#include "drivers/nfc/common/pn532_frame.hpp"

using namespace tapwave;
using namespace tapwave::nfc;

TEST_CASE("CRC_A matches known command frames") {
    using iso14443a::crc_a;
    // HLTA is sent as 50 00 57 CD, READ block 0 as 30 00 02 A8.
    CHECK(crc_a(Bytes{0x50, 0x00}) == 0xCD57);
    CHECK(crc_a(Bytes{0x30, 0x00}) == 0xA802);
    CHECK(iso14443a::with_crc(Bytes{0x50, 0x00}) == Bytes{0x50, 0x00, 0x57, 0xCD});
}

TEST_CASE("check_crc strips a valid CRC and refuses a bad one") {
    CHECK(iso14443a::check_crc(Bytes{0x30, 0x00, 0x02, 0xA8}) == Bytes{0x30, 0x00});
    CHECK_FALSE(iso14443a::check_crc(Bytes{0x30, 0x00, 0x02, 0xA9}));
    CHECK_FALSE(iso14443a::check_crc(Bytes{0x02, 0xA8}));
}

TEST_CASE("SAK values map to tag families") {
    using iso14443a::family_from_sak;
    CHECK(family_from_sak(0x00) == TagFamily::Type2);
    CHECK(family_from_sak(0x08) == TagFamily::MifareClassic1k);
    CHECK(family_from_sak(0x88) == TagFamily::MifareClassic1k);
    CHECK(family_from_sak(0x18) == TagFamily::MifareClassic4k);
    CHECK(family_from_sak(0x09) == TagFamily::MifareClassicMini);
    CHECK(family_from_sak(0x20) == TagFamily::Iso14443_4);
    CHECK(family_from_sak(0x60) == TagFamily::Iso14443_4);
    CHECK(family_from_sak(0x01) == TagFamily::Unknown);
}

TEST_CASE("GET_VERSION identifies NTAG and Ultralight EV1 models") {
    using iso14443a::family_from_version;
    auto version = [](std::uint8_t type, std::uint8_t storage) {
        return Bytes{0x00, 0x04, type, 0x02, 0x01, 0x00, storage, 0x03};
    };
    CHECK(family_from_version(version(0x04, 0x0F)) == TagFamily::Ntag213);
    CHECK(family_from_version(version(0x04, 0x11)) == TagFamily::Ntag215);
    CHECK(family_from_version(version(0x04, 0x13)) == TagFamily::Ntag216);
    CHECK(family_from_version(version(0x04, 0x0B)) == TagFamily::Ntag210);
    CHECK(family_from_version(version(0x04, 0x0E)) == TagFamily::Ntag212);
    CHECK(family_from_version(version(0x04, 0x99)) == TagFamily::Type2);
    CHECK(family_from_version(version(0x03, 0x0B)) == TagFamily::UltralightEv1);
    CHECK(family_from_version(Bytes{0x00, 0x05, 0x04}) == TagFamily::Type2);
}

TEST_CASE("anticollision helpers") {
    const iso14443a::CascadeLevel level{0x88, 0x04, 0xA2, 0x1B, 0x88 ^ 0x04 ^ 0xA2 ^ 0x1B};
    CHECK(iso14443a::bcc_valid(level));
    iso14443a::CascadeLevel broken = level;
    broken[4] ^= 0x01;
    CHECK_FALSE(iso14443a::bcc_valid(broken));

    const Bytes select = iso14443a::select_frame(1, level);
    CHECK(select.size() == 9);
    CHECK(select[0] == 0x95);
    CHECK(select[1] == 0x70);
    CHECK(iso14443a::check_crc(select));

    Bytes uid;
    iso14443a::append_uid_part(uid, level, true);
    CHECK(uid == Bytes{0x04, 0xA2, 0x1B});
    iso14443a::append_uid_part(uid, {0x3C, 0x4D, 0x5E, 0x80, 0}, false);
    CHECK(uid == Bytes{0x04, 0xA2, 0x1B, 0x3C, 0x4D, 0x5E, 0x80});
    CHECK(iso14443a::uid_incomplete(0x04));
    CHECK_FALSE(iso14443a::uid_incomplete(0x08));
}

TEST_CASE("PN532 command frames") {
    // GetFirmwareVersion, from the PN532 user manual.
    CHECK(pn532::build_command(0x02, {}) ==
          Bytes{0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A, 0x00});
    const Bytes sam = pn532::build_command(0x14, Bytes{0x01, 0x14, 0x01});
    CHECK(sam == Bytes{0x00, 0x00, 0xFF, 0x05, 0xFB, 0xD4, 0x14, 0x01, 0x14, 0x01, 0x02, 0x00});
}

TEST_CASE("PN532 response frames") {
    const Bytes response = pn532::build_response(0x03, Bytes{0x32, 0x01, 0x06, 0x07});
    const auto frame = pn532::parse_frame(response);
    REQUIRE(frame);
    CHECK(frame->kind == pn532::FrameKind::Data);
    CHECK(frame->code == 0x03);
    CHECK(frame->data == Bytes{0x32, 0x01, 0x06, 0x07});

    Bytes padded{0x00, 0x00};
    padded.insert(padded.end(), response.begin(), response.end());
    CHECK(pn532::parse_frame(padded));
}

TEST_CASE("PN532 ACK, NACK and error frames") {
    CHECK(pn532::parse_frame(pn532::kAck)->kind == pn532::FrameKind::Ack);
    CHECK(pn532::parse_frame(pn532::kNack)->kind == pn532::FrameKind::Nack);
    const Bytes error{0x00, 0x00, 0xFF, 0x01, 0xFF, 0x7F, 0x81, 0x00};
    CHECK(pn532::parse_frame(error)->kind == pn532::FrameKind::ErrorFrame);
}

TEST_CASE("PN532 frame errors") {
    using pn532::FrameError;
    CHECK(pn532::parse_frame(Bytes{0x00, 0x00}).error() == FrameError::Incomplete);
    CHECK(pn532::parse_frame(Bytes{0x00, 0x00, 0xFF, 0x04}).error() == FrameError::Incomplete);
    CHECK(pn532::parse_frame(Bytes{0x12, 0x34}).error() == FrameError::NoStartCode);
    CHECK(pn532::parse_frame(Bytes{0x00, 0x00, 0xFF, 0x03, 0x00, 0xD5, 0x03, 0x00, 0x00}).error() ==
          FrameError::BadLength);
    Bytes bad = pn532::build_response(0x03, Bytes{0x32});
    bad[bad.size() - 2] ^= 0x01;
    CHECK(pn532::parse_frame(bad).error() == FrameError::BadChecksum);
    CHECK(pn532::parse_frame(pn532::build_command(0x02, {})).error() == FrameError::WrongDirection);
    CHECK(pn532::parse_frame(Bytes{0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x02, 0xFF}).error() ==
          FrameError::Extended);
    const Bytes too_short = pn532::build_response(0x03, {});
    Bytes one_byte{0x00, 0x00, 0xFF, 0x01, 0xFF, 0xD5, 0x2B, 0x00};
    CHECK(pn532::parse_frame(one_byte).error() == FrameError::BadLength);
    CHECK(pn532::parse_frame(too_short));
}

TEST_CASE("frame_length tells transports how much to read") {
    CHECK(pn532::frame_length(pn532::kAck) == 6);
    CHECK(pn532::frame_length(pn532::kNack) == 6);
    const Bytes response = pn532::build_response(0x03, Bytes{0x32, 0x01, 0x06, 0x07});
    CHECK(pn532::frame_length(response) == response.size());
    CHECK(pn532::frame_length(ByteView(response).first(5)) == response.size());
    CHECK_FALSE(pn532::frame_length(ByteView(response).first(4)));
    CHECK_FALSE(pn532::frame_length(ByteView(response).first(3)));
    CHECK_FALSE(pn532::frame_length(Bytes{0x00, 0x00}));
}

TEST_CASE("NFC type names") {
    CHECK(to_string(TagFamily::Ntag215) == "NTAG215");
    CHECK(to_string(TagFamily::MifareClassic1k) == "MIFARE Classic 1K");
    CHECK(is_type2(TagFamily::Ultralight));
    CHECK_FALSE(is_type2(TagFamily::MifareClassic4k));
    CHECK(is_classic(TagFamily::MifareClassicMini));
    CHECK_FALSE(describe(Error::Collision).empty());
    const ReaderInfo info{"rc522-spi", "RC522", "spi", "2.0"};
    CHECK(info.summary() == "RC522 2.0 (spi)");
    CHECK(ReaderInfo{"x", "PN5180", "", ""}.summary() == "PN5180");
}
