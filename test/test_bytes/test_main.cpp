#include "doctest_main.hpp"

#include "core/bytes.hpp"
#include "core/uid.hpp"

using namespace tapwave;

TEST_CASE("to_hex formats bytes with an optional separator") {
    const Bytes bytes{0x04, 0xA2, 0x1B, 0x0F};
    CHECK(to_hex(bytes) == "04A21B0F");
    CHECK(to_hex(bytes, "-") == "04-A2-1B-0F");
    CHECK(to_hex(bytes, ":", false) == "04:a2:1b:0f");
    CHECK(to_hex(Bytes{}) == "");
}

TEST_CASE("from_hex accepts common separators and either case") {
    CHECK(from_hex("04A21B") == Bytes{0x04, 0xA2, 0x1B});
    CHECK(from_hex("04-a2-1b") == Bytes{0x04, 0xA2, 0x1B});
    CHECK(from_hex("04:A2 1B") == Bytes{0x04, 0xA2, 0x1B});
    CHECK(from_hex("") == Bytes{});
}

TEST_CASE("from_hex rejects malformed text") {
    CHECK_FALSE(from_hex("0"));
    CHECK_FALSE(from_hex("0G"));
    CHECK_FALSE(from_hex("0-4"));
    CHECK_FALSE(from_hex("04_A2"));
}

TEST_CASE("Uid uses the tagreader text format") {
    const Bytes seven{0x04, 0xA2, 0x1B, 0x3C, 0x4D, 0x5E, 0x80};
    const auto uid = Uid::from(seven);
    REQUIRE(uid);
    CHECK(uid->to_string() == "04-A2-1B-3C-4D-5E-80");
    CHECK(uid->size() == 7);
}

TEST_CASE("Uid round-trips through its text form") {
    const auto uid = Uid::parse("de-ad-be-ef");
    REQUIRE(uid);
    CHECK(uid->to_string() == "DE-AD-BE-EF");
    CHECK(Uid::parse(uid->to_string()) == uid);
}

TEST_CASE("Uid rejects empty and oversized input") {
    CHECK_FALSE(Uid::from(Bytes{}));
    CHECK_FALSE(Uid::from(Bytes(11, 0x01)));
    CHECK(Uid::from(Bytes(10, 0x01)));
    CHECK_FALSE(Uid::parse("zz"));
}

TEST_CASE("Uid equality compares bytes and length") {
    CHECK(*Uid::parse("01020304") == *Uid::parse("01-02-03-04"));
    CHECK_FALSE(*Uid::parse("01020304") == *Uid::parse("0102030405"));
    CHECK(Uid{}.empty());
}
