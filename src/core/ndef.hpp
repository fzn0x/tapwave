// NFC Data Exchange Format: parsing and encoding NDEF messages.
//
// Follows the NFC Forum NDEF 1.0 specification plus the URI (RTD-URI) and Text (RTD-Text)
// record type definitions. Chunked records are reassembled. The parser is strict about
// lengths and lenient about a missing message-end flag after the last complete record, which
// some writers leave out.
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/bytes.hpp"

namespace tapwave::ndef {

// Type Name Format: how to read a record's type field.
enum class Tnf : std::uint8_t {
    Empty = 0,
    WellKnown = 1,    // NFC Forum RTD, for example "U" (URI) and "T" (Text)
    Media = 2,        // MIME media type, for example "application/json"
    AbsoluteUri = 3,  // the type field is a URI
    External = 4,     // "domain:type"
    Unknown = 5,
    Unchanged = 6,  // only valid inside a chunked record
    Reserved = 7,
};

struct Record {
    Tnf tnf = Tnf::Empty;
    std::string type;
    Bytes id;
    Bytes payload;

    friend bool operator==(const Record&, const Record&) = default;
};

enum class Error : std::uint8_t {
    Empty,         // no bytes at all
    Truncated,     // a length field points past the end of the data
    MissingBegin,  // the first record does not have the message-begin flag
    BadChunk,      // a chunked record breaks the chunking rules
};

std::string_view describe(Error error);

std::expected<std::vector<Record>, Error> parse(ByteView message);

// Encodes records as one NDEF message. An empty list encodes the empty NDEF message (D0 00 00).
Bytes encode(std::span<const Record> records);

// True when the message carries nothing: no records, or only records of TNF Empty.
bool is_blank(std::span<const Record> records);

// The URI of a well-known URI record (prefix code expanded) or of an absolute-URI record.
std::optional<std::string> uri(const Record& record);

struct Text {
    std::string language;  // IANA language code, for example "en"
    std::string text;      // always UTF-8, converted from UTF-16 when needed
};

// The language and text of a well-known Text record.
std::optional<Text> text(const Record& record);

// Builds a URI record, compressing the longest matching NFC Forum prefix into one byte.
Record uri_record(std::string_view uri);

// Builds a UTF-8 Text record.
Record text_record(std::string_view text, std::string_view language = "en");

// Builds a MIME media record.
Record mime_record(std::string_view mime_type, ByteView payload);

}  // namespace tapwave::ndef
