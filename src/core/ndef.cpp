#include "core/ndef.hpp"

#include <array>

namespace tapwave::ndef {

namespace {

constexpr std::uint8_t kMessageBegin = 0x80;
constexpr std::uint8_t kMessageEnd = 0x40;
constexpr std::uint8_t kChunk = 0x20;
constexpr std::uint8_t kShortRecord = 0x10;
constexpr std::uint8_t kIdLength = 0x08;
constexpr std::uint8_t kTnfMask = 0x07;

// NFC Forum URI RTD, table 3: identifier codes 0x00 to 0x23. Codes above are reserved and
// treated as "no prefix".
constexpr std::array<std::string_view, 36> kUriPrefixes{
    "",
    "http://www.",
    "https://www.",
    "http://",
    "https://",
    "tel:",
    "mailto:",
    "ftp://anonymous:anonymous@",
    "ftp://ftp.",
    "ftps://",
    "sftp://",
    "smb://",
    "nfs://",
    "ftp://",
    "dav://",
    "news:",
    "telnet://",
    "imap:",
    "rtsp://",
    "urn:",
    "pop:",
    "sip:",
    "sips:",
    "tftp:",
    "btspp://",
    "btl2cap://",
    "btgoep://",
    "tcpobex://",
    "irdaobex://",
    "file://",
    "urn:epc:id:",
    "urn:epc:tag:",
    "urn:epc:pat:",
    "urn:epc:raw:",
    "urn:epc:",
    "urn:nfc:",
};

// Reads fields from the message, failing once anything would run past the end.
class Reader {
public:
    explicit Reader(ByteView data) : data_(data) {}

    bool done() const { return pos_ >= data_.size(); }
    std::size_t remaining() const { return data_.size() - pos_; }

    std::optional<std::uint8_t> byte() {
        if (done()) {
            return std::nullopt;
        }
        return data_[pos_++];
    }

    std::optional<std::uint32_t> u32() {
        if (remaining() < 4) {
            return std::nullopt;
        }
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            value = (value << 8) | data_[pos_++];
        }
        return value;
    }

    std::optional<ByteView> take(std::size_t count) {
        if (count > remaining()) {
            return std::nullopt;
        }
        const ByteView out = data_.subspan(pos_, count);
        pos_ += count;
        return out;
    }

private:
    ByteView data_;
    std::size_t pos_ = 0;
};

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// UTF-16 to UTF-8. Big endian unless a byte order mark says otherwise (RTD-Text 3.2.1).
// Unpaired surrogates become U+FFFD.
std::string utf16_to_utf8(ByteView bytes) {
    bool little_endian = false;
    std::size_t i = 0;
    if (bytes.size() >= 2) {
        if (bytes[0] == 0xFF && bytes[1] == 0xFE) {
            little_endian = true;
            i = 2;
        } else if (bytes[0] == 0xFE && bytes[1] == 0xFF) {
            i = 2;
        }
    }
    auto unit = [&](std::size_t at) -> std::uint16_t {
        return little_endian ? static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8))
                             : static_cast<std::uint16_t>((bytes[at] << 8) | bytes[at + 1]);
    };
    std::string out;
    while (i + 1 < bytes.size()) {
        const std::uint16_t u = unit(i);
        i += 2;
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < bytes.size()) {
            const std::uint16_t low = unit(i);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                i += 2;
                append_utf8(out, 0x10000 + ((u - 0xD800) << 10) + (low - 0xDC00));
                continue;
            }
        }
        append_utf8(out, (u >= 0xD800 && u <= 0xDFFF) ? 0xFFFD : u);
    }
    return out;
}

}  // namespace

std::string_view describe(Error error) {
    switch (error) {
        case Error::Empty:
            return "the NDEF message is empty";
        case Error::Truncated:
            return "the NDEF message is cut short";
        case Error::MissingBegin:
            return "the NDEF message does not start with a record";
        case Error::BadChunk:
            return "a chunked NDEF record is malformed";
    }
    return "unknown NDEF error";
}

std::expected<std::vector<Record>, Error> parse(ByteView message) {
    if (message.empty()) {
        return std::unexpected(Error::Empty);
    }
    Reader in(message);
    std::vector<Record> records;
    std::optional<Record> chunked;  // the record being reassembled, while inside a chunk
    bool first = true;

    while (!in.done()) {
        const std::uint8_t header = *in.byte();
        if (first && (header & kMessageBegin) == 0) {
            return std::unexpected(Error::MissingBegin);
        }
        first = false;

        const auto type_length = in.byte();
        if (!type_length) {
            return std::unexpected(Error::Truncated);
        }
        std::optional<std::uint32_t> payload_length;
        if (header & kShortRecord) {
            if (auto length = in.byte()) {
                payload_length = *length;
            }
        } else {
            payload_length = in.u32();
        }
        if (!payload_length) {
            return std::unexpected(Error::Truncated);
        }
        std::uint8_t id_length = 0;
        if (header & kIdLength) {
            const auto length = in.byte();
            if (!length) {
                return std::unexpected(Error::Truncated);
            }
            id_length = *length;
        }
        const auto type = in.take(*type_length);
        const auto id = in.take(id_length);
        const auto payload = in.take(*payload_length);
        if (!type || !id || !payload) {
            return std::unexpected(Error::Truncated);
        }

        const auto tnf = static_cast<Tnf>(header & kTnfMask);
        if (chunked) {
            // A middle or final chunk: no type of its own, TNF "unchanged".
            if (tnf != Tnf::Unchanged || !type->empty()) {
                return std::unexpected(Error::BadChunk);
            }
            chunked->payload.insert(chunked->payload.end(), payload->begin(), payload->end());
            if ((header & kChunk) == 0) {
                records.push_back(std::move(*chunked));
                chunked.reset();
            }
        } else {
            if (tnf == Tnf::Unchanged) {
                return std::unexpected(Error::BadChunk);
            }
            Record record{tnf, to_string(*type), Bytes(id->begin(), id->end()),
                          Bytes(payload->begin(), payload->end())};
            if (header & kChunk) {
                chunked = std::move(record);
            } else {
                records.push_back(std::move(record));
            }
        }

        if (header & kMessageEnd) {
            if (chunked) {
                return std::unexpected(Error::BadChunk);
            }
            return records;  // anything after the message end is not part of the message
        }
    }

    if (chunked) {
        return std::unexpected(Error::Truncated);
    }
    return records;
}

Bytes encode(std::span<const Record> records) {
    if (records.empty()) {
        return {kMessageBegin | kMessageEnd | kShortRecord, 0x00, 0x00};
    }
    Bytes out;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const Record& record = records[i];
        const bool short_record = record.payload.size() <= 0xFF;
        std::uint8_t header = static_cast<std::uint8_t>(record.tnf) & kTnfMask;
        if (i == 0) {
            header |= kMessageBegin;
        }
        if (i + 1 == records.size()) {
            header |= kMessageEnd;
        }
        if (short_record) {
            header |= kShortRecord;
        }
        if (!record.id.empty()) {
            header |= kIdLength;
        }
        out.push_back(header);
        out.push_back(static_cast<std::uint8_t>(record.type.size()));
        const auto length = static_cast<std::uint32_t>(record.payload.size());
        if (short_record) {
            out.push_back(static_cast<std::uint8_t>(length));
        } else {
            for (int shift = 24; shift >= 0; shift -= 8) {
                out.push_back(static_cast<std::uint8_t>(length >> shift));
            }
        }
        if (!record.id.empty()) {
            out.push_back(static_cast<std::uint8_t>(record.id.size()));
        }
        const ByteView type = as_bytes(record.type);
        out.insert(out.end(), type.begin(), type.end());
        out.insert(out.end(), record.id.begin(), record.id.end());
        out.insert(out.end(), record.payload.begin(), record.payload.end());
    }
    return out;
}

bool is_blank(std::span<const Record> records) {
    for (const Record& record : records) {
        if (record.tnf != Tnf::Empty) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> uri(const Record& record) {
    if (record.tnf == Tnf::AbsoluteUri) {
        return record.type;
    }
    if (record.tnf != Tnf::WellKnown || record.type != "U" || record.payload.empty()) {
        return std::nullopt;
    }
    const std::uint8_t code = record.payload[0];
    std::string out{code < kUriPrefixes.size() ? kUriPrefixes[code] : std::string_view{}};
    out.append(to_string(ByteView(record.payload).subspan(1)));
    return out;
}

std::optional<Text> text(const Record& record) {
    if (record.tnf != Tnf::WellKnown || record.type != "T" || record.payload.empty()) {
        return std::nullopt;
    }
    const std::uint8_t status = record.payload[0];
    const bool utf16 = (status & 0x80) != 0;
    const std::size_t language_length = status & 0x3F;
    if (1 + language_length > record.payload.size()) {
        return std::nullopt;
    }
    const ByteView payload(record.payload);
    Text out;
    out.language = to_string(payload.subspan(1, language_length));
    const ByteView body = payload.subspan(1 + language_length);
    out.text = utf16 ? utf16_to_utf8(body) : to_string(body);
    return out;
}

Record uri_record(std::string_view uri) {
    std::uint8_t best = 0;
    for (std::size_t code = 1; code < kUriPrefixes.size(); ++code) {
        const std::string_view prefix = kUriPrefixes[code];
        if (uri.starts_with(prefix) && prefix.size() > kUriPrefixes[best].size()) {
            best = static_cast<std::uint8_t>(code);
        }
    }
    Record record{Tnf::WellKnown, "U", {}, {best}};
    const ByteView rest = as_bytes(uri.substr(kUriPrefixes[best].size()));
    record.payload.insert(record.payload.end(), rest.begin(), rest.end());
    return record;
}

Record text_record(std::string_view text, std::string_view language) {
    Record record{Tnf::WellKnown, "T", {}, {}};
    language = language.substr(0, 0x3F);
    record.payload.push_back(static_cast<std::uint8_t>(language.size()));
    const ByteView language_bytes = as_bytes(language);
    const ByteView text_bytes = as_bytes(text);
    record.payload.insert(record.payload.end(), language_bytes.begin(), language_bytes.end());
    record.payload.insert(record.payload.end(), text_bytes.begin(), text_bytes.end());
    return record;
}

Record mime_record(std::string_view mime_type, ByteView payload) {
    return Record{Tnf::Media, std::string(mime_type), {}, Bytes(payload.begin(), payload.end())};
}

}  // namespace tapwave::ndef
