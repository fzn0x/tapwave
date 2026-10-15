#include "core/scan_json.hpp"

namespace tapwave {

bool is_printable_utf8(ByteView bytes) {
    std::size_t i = 0;
    while (i < bytes.size()) {
        const std::uint8_t b = bytes[i];
        std::size_t extra = 0;
        std::uint32_t cp = 0;
        if (b < 0x80) {
            if (b < 0x20 && b != '\t' && b != '\n' && b != '\r') {
                return false;
            }
            if (b == 0x7F) {
                return false;
            }
            ++i;
            continue;
        }
        if ((b & 0xE0) == 0xC0) {
            extra = 1;
            cp = b & 0x1F;
        } else if ((b & 0xF0) == 0xE0) {
            extra = 2;
            cp = b & 0x0F;
        } else if ((b & 0xF8) == 0xF0) {
            extra = 3;
            cp = b & 0x07;
        } else {
            return false;
        }
        if (i + extra >= bytes.size()) {
            return false;  // the sequence is cut off
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            if ((bytes[i + k] & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (bytes[i + k] & 0x3F);
        }
        // Reject overlong forms, surrogates and values past U+10FFFF.
        constexpr std::uint32_t kMinimum[] = {0, 0x80, 0x800, 0x10000};
        if (cp < kMinimum[extra] || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
            return false;
        }
        i += extra + 1;
    }
    return true;
}

void write_record_json(const ndef::Record& record, JsonObject out) {
    out["tnf"] = static_cast<int>(record.tnf);
    out["type"] = record.type;
    if (!record.id.empty()) {
        out["id"] = to_hex(record.id);
    }
    if (const auto u = ndef::uri(record)) {
        out["uri"] = *u;
        return;
    }
    if (const auto t = ndef::text(record)) {
        out["lang"] = t->language;
        out["text"] = t->text;
        return;
    }
    if (record.tnf == ndef::Tnf::Empty) {
        return;
    }
    out["payload"] = to_hex(record.payload);
    if (!record.payload.empty() && is_printable_utf8(record.payload)) {
        out["payload_text"] = to_string(record.payload);
    }
}

void write_scan_json(const Scan& scan, JsonObject out, const ScanContext& context) {
    out["id"] = scan.id();
    out["uid"] = scan.uid;
    out["kind"] = to_string(scan.kind);
    if (!scan.tag_type.empty()) {
        out["tag"] = scan.tag_type;
    }
    if (scan.uri) {
        out["uri"] = *scan.uri;
    }
    if (scan.text) {
        out["text"] = *scan.text;
    }
    if (scan.music_uri) {
        out["music_uri"] = *scan.music_uri;
    }
    if (scan.tapwave_uri) {
        JsonObject action = out["action"].to<JsonObject>();
        action["uri"] = *scan.tapwave_uri;
        if (scan.action) {
            action["name"] = scan.action->action_name;
            action["target"] = scan.action->target;
            action["supported"] = scan.action->supported();
            if (!scan.action->options.empty()) {
                JsonObject options = action["options"].to<JsonObject>();
                for (const auto& option : scan.action->options) {
                    options[option.key] = option.value;
                }
            }
        } else if (scan.action_error) {
            action["supported"] = false;
            action["error"] = uri::describe(*scan.action_error);
        }
    }
    JsonArray records = out["records"].to<JsonArray>();
    for (const ndef::Record& record : scan.records) {
        write_record_json(record, records.add<JsonObject>());
    }
    if (scan.ndef_error) {
        out["ndef_error"] = *scan.ndef_error;
    }
    if (!context.reader.empty()) {
        out["reader"] = context.reader;
    }
    if (!context.time.empty()) {
        out["time"] = context.time;
    }
}

std::string scan_to_json(const Scan& scan, const ScanContext& context) {
    JsonDocument doc;
    write_scan_json(scan, doc.to<JsonObject>(), context);
    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace tapwave
