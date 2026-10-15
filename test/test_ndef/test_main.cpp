#include "doctest_main.hpp"

#include "core/ndef.hpp"

using namespace tapwave;
using namespace tapwave::ndef;

namespace {

Bytes bytes_of(std::initializer_list<int> values) {
    Bytes out;
    for (int v : values) {
        out.push_back(static_cast<std::uint8_t>(v));
    }
    return out;
}

void append(Bytes& out, std::string_view text) {
    const ByteView view = as_bytes(text);
    out.insert(out.end(), view.begin(), view.end());
}

}  // namespace

TEST_CASE("parses the Text record Android writes") {
    // "Hello" in English, as written by NFC Tools.
    const Bytes message =
        bytes_of({0xD1, 0x01, 0x08, 'T', 0x02, 'e', 'n', 'H', 'e', 'l', 'l', 'o'});
    const auto records = parse(message);
    REQUIRE(records);
    REQUIRE(records->size() == 1);
    const auto t = text(records->front());
    REQUIRE(t);
    CHECK(t->language == "en");
    CHECK(t->text == "Hello");
    CHECK_FALSE(uri(records->front()));
}

TEST_CASE("expands URI prefix codes") {
    Bytes message = bytes_of({0xD1, 0x01, 0x0B, 'U', 0x02});
    append(message, "google.com");
    const auto records = parse(message);
    REQUIRE(records);
    CHECK(uri(records->front()) == "https://www.google.com");
}

TEST_CASE("reserved URI prefix codes mean no prefix") {
    Bytes message = bytes_of({0xD1, 0x01, 0x04, 'U', 0x24});
    append(message, "a:b");
    const auto records = parse(message);
    REQUIRE(records);
    CHECK(uri(records->front()) == "a:b");
}

TEST_CASE("absolute URI records keep the URI in the type field") {
    const Record record{Tnf::AbsoluteUri, "tapwave://cmd/next", {}, {}};
    const auto records = parse(encode(std::vector{record}));
    REQUIRE(records);
    CHECK(uri(records->front()) == "tapwave://cmd/next");
}

TEST_CASE("multi-record messages round-trip") {
    const std::vector<Record> records{
        uri_record("https://www.home-assistant.io/tag/abc"),
        text_record("Kitchen lights"),
        mime_record("application/json", as_bytes(R"({"a":1})")),
        Record{Tnf::External, "tapwave.dev:card", {}, {0x01, 0x02}},
    };
    const Bytes message = encode(records);
    CHECK(message[0] == 0x91);        // MB, SR, well-known
    CHECK((message[0] & 0x40) == 0);  // ME only on the last record
    const auto parsed = parse(message);
    REQUIRE(parsed);
    CHECK(*parsed == records);
}

TEST_CASE("long records use a four-byte payload length") {
    const Record record = mime_record("application/octet-stream", Bytes(300, 0xAB));
    const Bytes message = encode(std::vector{record});
    CHECK((message[0] & 0x10) == 0);  // not a short record
    CHECK(message[2] == 0x00);
    CHECK(message[3] == 0x00);
    CHECK(message[4] == 0x01);
    CHECK(message[5] == 0x2C);
    const auto parsed = parse(message);
    REQUIRE(parsed);
    CHECK(parsed->front() == record);
}

TEST_CASE("record IDs round-trip") {
    Record record = text_record("x");
    record.id = {'i', 'd', '1'};
    const Bytes message = encode(std::vector{record});
    CHECK((message[0] & 0x08) != 0);
    const auto parsed = parse(message);
    REQUIRE(parsed);
    CHECK(parsed->front().id == Bytes{'i', 'd', '1'});
}

TEST_CASE("the empty NDEF message is blank") {
    const Bytes empty = encode(std::vector<Record>{});
    CHECK(empty == bytes_of({0xD0, 0x00, 0x00}));
    const auto parsed = parse(empty);
    REQUIRE(parsed);
    CHECK(parsed->size() == 1);
    CHECK(is_blank(*parsed));
    CHECK(is_blank(std::vector<Record>{}));
    CHECK_FALSE(is_blank(std::vector{text_record("x")}));
}

TEST_CASE("chunked records are reassembled") {
    Bytes message = bytes_of({0xB2, 10, 3});
    append(message, "text/plainHel");
    message.insert(message.end(), {0x36, 0, 3});
    append(message, "lo ");
    message.insert(message.end(), {0x56, 0, 5});
    append(message, "World");
    const auto parsed = parse(message);
    REQUIRE(parsed);
    REQUIRE(parsed->size() == 1);
    CHECK(parsed->front().tnf == Tnf::Media);
    CHECK(parsed->front().type == "text/plain");
    CHECK(to_string(parsed->front().payload) == "Hello World");
}

TEST_CASE("bytes after the message end are ignored") {
    Bytes message = encode(std::vector{text_record("hi")});
    message.insert(message.end(), {0xFE, 0x00, 0x00});
    const auto parsed = parse(message);
    REQUIRE(parsed);
    CHECK(parsed->size() == 1);
}

TEST_CASE("a missing message-end flag after a complete record is tolerated") {
    Bytes message = encode(std::vector{text_record("hi")});
    message[0] &= static_cast<std::uint8_t>(~0x40);
    const auto parsed = parse(message);
    REQUIRE(parsed);
    CHECK(text(parsed->front())->text == "hi");
}

TEST_CASE("malformed messages are rejected with a reason") {
    CHECK(parse(Bytes{}).error() == Error::Empty);
    CHECK(parse(bytes_of({0x11, 0x01, 0x00, 'T'})).error() == Error::MissingBegin);
    CHECK(parse(bytes_of({0xD1, 0x01, 0x05, 'T', 0x02})).error() == Error::Truncated);
    CHECK(parse(bytes_of({0xD1})).error() == Error::Truncated);
    CHECK(parse(bytes_of({0xC1, 0x01, 0x00, 0x00})).error() == Error::Truncated);  // long length
    CHECK(parse(bytes_of({0xD9, 0x01, 0x00})).error() == Error::Truncated);  // missing ID length
    CHECK(parse(bytes_of({0xD6, 0x00, 0x00})).error() == Error::BadChunk);   // unchanged, no chunk
    CHECK(parse(bytes_of({0xF2, 0x01, 0x00, 'x'})).error() == Error::BadChunk);  // ME inside chunk
    CHECK(parse(bytes_of({0xB2, 0x01, 0x00, 'x'})).error() ==
          Error::Truncated);  // chunk never ends
    CHECK(parse(bytes_of({0xB2, 0x01, 0x00, 'x', 0x52, 0x01, 0x00, 'y'})).error() ==
          Error::BadChunk);  // final chunk with its own type
    for (auto error : {Error::Empty, Error::Truncated, Error::MissingBegin, Error::BadChunk}) {
        CHECK_FALSE(describe(error).empty());
    }
}

TEST_CASE("uri_record compresses the longest matching prefix") {
    CHECK(uri_record("https://www.example.com").payload[0] == 0x02);
    CHECK(uri_record("https://example.com").payload[0] == 0x04);
    CHECK(uri_record("http://example.com").payload[0] == 0x03);
    CHECK(uri_record("tel:+123").payload[0] == 0x05);
    CHECK(uri_record("urn:nfc:wkt:U").payload[0] == 0x23);
    CHECK(uri_record("tapwave://sd/Music").payload[0] == 0x00);
    for (std::string_view u : {"https://www.example.com/x", "tapwave://cmd/next", "urn:epc:id:1"}) {
        CHECK(uri(uri_record(u)) == std::string(u));
    }
}

TEST_CASE("UTF-16 text is converted to UTF-8") {
    // status: UTF-16 flag, language "en"; text "Hi" then U+1F3B5 (a surrogate pair).
    Record big_endian{Tnf::WellKnown,
                      "T",
                      {},
                      bytes_of({0x82, 'e', 'n', 0x00, 'H', 0x00, 'i', 0xD8, 0x3C, 0xDF, 0xB5})};
    CHECK(text(big_endian)->text == "Hi\xF0\x9F\x8E\xB5");

    Record little_endian{
        Tnf::WellKnown, "T", {}, bytes_of({0x82, 'e', 'n', 0xFF, 0xFE, 'O', 0x00, 'k', 0x00})};
    CHECK(text(little_endian)->text == "Ok");

    Record bom_big{Tnf::WellKnown, "T", {}, bytes_of({0x82, 'e', 'n', 0xFE, 0xFF, 0x00, 0xE9})};
    CHECK(text(bom_big)->text == "\xC3\xA9");

    Record lone_surrogate{Tnf::WellKnown, "T", {}, bytes_of({0x80, 0xD8, 0x00})};
    CHECK(text(lone_surrogate)->text == "\xEF\xBF\xBD");
}

TEST_CASE("Text records with a bad language length are rejected") {
    const Record broken{Tnf::WellKnown, "T", {}, bytes_of({0x05, 'e', 'n'})};
    CHECK_FALSE(text(broken));
    const Record empty{Tnf::WellKnown, "T", {}, {}};
    CHECK_FALSE(text(empty));
}

TEST_CASE("text_record writes the language length in the status byte") {
    const Record record = text_record("Gruffalo", "de");
    CHECK(record.payload[0] == 0x02);
    CHECK(text(record)->language == "de");
    CHECK(text(record)->text == "Gruffalo");
}
