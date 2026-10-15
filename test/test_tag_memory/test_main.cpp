#include "doctest_main.hpp"

#include "core/tag_memory.hpp"

using namespace tapwave;
using namespace tapwave::tag_memory;

TEST_CASE("capability container of a fresh NTAG215") {
    const Bytes page{0xE1, 0x10, 0x3E, 0x00};
    const auto cc = CapabilityContainer::parse(page);
    REQUIRE(cc);
    CHECK(cc->data_area_size() == 496);
    CHECK(cc->readable());
    CHECK(cc->writable());
}

TEST_CASE("capability container sizes of common tags") {
    CHECK(CapabilityContainer::parse(Bytes{0xE1, 0x10, 0x12, 0x00})->data_area_size() == 144);
    CHECK(CapabilityContainer::parse(Bytes{0xE1, 0x10, 0x6D, 0x00})->data_area_size() == 872);
    CHECK(CapabilityContainer::parse(Bytes{0xE1, 0x10, 0x06, 0x00})->data_area_size() == 48);
}

TEST_CASE("a locked tag is readable but not writable") {
    const auto cc = CapabilityContainer::parse(Bytes{0xE1, 0x10, 0x12, 0x0F});
    REQUIRE(cc);
    CHECK(cc->readable());
    CHECK_FALSE(cc->writable());
}

TEST_CASE("an unformatted tag has no capability container") {
    CHECK_FALSE(CapabilityContainer::parse(Bytes{0x00, 0x00, 0x00, 0x00}));
    CHECK_FALSE(CapabilityContainer::parse(Bytes{0xE1, 0x10}));
}

TEST_CASE("finds the NDEF TLV at the start of the data area") {
    const Bytes data{0x03, 0x0C, 0xD1, 0x01};
    const auto found = find_ndef_tlv(data, 144);
    REQUIRE(std::holds_alternative<NdefTlv>(found));
    CHECK(std::get<NdefTlv>(found).offset == 2);
    CHECK(std::get<NdefTlv>(found).length == 12);
}

TEST_CASE("skips NULL and lock control TLVs") {
    // NULL, lock control TLV (3 bytes), memory control TLV (3 bytes), then NDEF.
    const Bytes data{0x00, 0x01, 0x03, 0xA0, 0x0C, 0x34, 0x02, 0x03,
                     0x00, 0x00, 0x00, 0x03, 0x05, 0xD1, 0x01, 0x01};
    const auto found = find_ndef_tlv(data, 144);
    REQUIRE(std::holds_alternative<NdefTlv>(found));
    CHECK(std::get<NdefTlv>(found).offset == 13);
    CHECK(std::get<NdefTlv>(found).length == 5);
}

TEST_CASE("reads the three-byte length format") {
    const Bytes data{0x03, 0xFF, 0x01, 0x2C};
    const auto found = find_ndef_tlv(data, 496);
    REQUIRE(std::holds_alternative<NdefTlv>(found));
    CHECK(std::get<NdefTlv>(found).offset == 4);
    CHECK(std::get<NdefTlv>(found).length == 300);
}

TEST_CASE("asks for more data when a TLV header is cut off") {
    CHECK(std::get<NeedMore>(find_ndef_tlv(Bytes{}, 144)).bytes == 1);
    CHECK(std::get<NeedMore>(find_ndef_tlv(Bytes{0x00, 0x00}, 144)).bytes == 3);
    CHECK(std::get<NeedMore>(find_ndef_tlv(Bytes{0x03}, 144)).bytes == 2);
    CHECK(std::get<NeedMore>(find_ndef_tlv(Bytes{0x03, 0xFF, 0x01}, 496)).bytes == 4);
    // A lock control TLV fully read, but the next TLV not yet.
    CHECK(std::get<NeedMore>(find_ndef_tlv(Bytes{0x01, 0x03, 0xA0, 0x0C, 0x34}, 144)).bytes == 6);
}

TEST_CASE("an empty NDEF TLV means a formatted, empty tag") {
    const auto found = find_ndef_tlv(Bytes{0x03, 0x00, 0xFE}, 144);
    REQUIRE(std::holds_alternative<NdefTlv>(found));
    CHECK(std::get<NdefTlv>(found).length == 0);
}

TEST_CASE("a terminator or the end of the data area means no NDEF") {
    CHECK(std::get<TlvError>(find_ndef_tlv(Bytes{0xFE, 0x00}, 144)) == TlvError::NoNdef);
    CHECK(std::get<TlvError>(find_ndef_tlv(Bytes{0x00, 0x00, 0x00, 0x00}, 4)) == TlvError::NoNdef);
}

TEST_CASE("a TLV longer than the data area is malformed") {
    CHECK(std::get<TlvError>(find_ndef_tlv(Bytes{0x03, 0x90}, 144)) == TlvError::Malformed);
    CHECK(std::get<TlvError>(find_ndef_tlv(Bytes{0x01, 0x90}, 48)) == TlvError::Malformed);
}

TEST_CASE("MIFARE Classic sector layout") {
    CHECK(classic_first_block(0) == 0);
    CHECK(classic_first_block(1) == 4);
    CHECK(classic_first_block(15) == 60);
    CHECK(classic_first_block(31) == 124);
    CHECK(classic_first_block(32) == 128);
    CHECK(classic_first_block(39) == 240);
    CHECK(classic_data_blocks(1) == 3);
    CHECK(classic_data_blocks(32) == 15);
}

TEST_CASE("reads NDEF sectors from MAD1") {
    // An NDEF-formatted 1K tag: every sector holds the NDEF AID 03 E1.
    Bytes block1{0x14, 0x01};
    for (int i = 0; i < 7; ++i) {
        block1.insert(block1.end(), {0x03, 0xE1});
    }
    Bytes block2;
    for (int i = 0; i < 8; ++i) {
        block2.insert(block2.end(), {0x03, 0xE1});
    }
    const auto sectors = classic_ndef_sectors(block1, block2);
    REQUIRE(sectors.size() == 15);
    CHECK(sectors.front() == 1);
    CHECK(sectors.back() == 15);
}

TEST_CASE("stops at the first gap after the NDEF run") {
    Bytes block1(16, 0x00);
    // Sector 1 free, sectors 2 and 3 NDEF, sector 4 another application, sector 5 NDEF again.
    block1[4] = 0x03;
    block1[5] = 0xE1;
    block1[6] = 0x03;
    block1[7] = 0xE1;
    block1[8] = 0x01;
    block1[9] = 0x48;
    block1[10] = 0x03;
    block1[11] = 0xE1;
    const auto sectors = classic_ndef_sectors(block1, Bytes(16, 0x00));
    CHECK(sectors == std::vector<std::uint8_t>{2, 3});
}

TEST_CASE("a tag without a MAD has no NDEF sectors") {
    CHECK(classic_ndef_sectors(Bytes(16, 0x00), Bytes(16, 0x00)).empty());
    CHECK(classic_ndef_sectors(Bytes(4, 0x00), Bytes(16, 0x00)).empty());
}
