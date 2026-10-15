// The scan JSON published on MQTT, sent over the WebSocket and returned by the API
// (ARCHITECTURE.md section 12).
#pragma once

#include <ArduinoJson.h>

#include <string>
#include <string_view>

#include "core/scan.hpp"

namespace tapwave {

struct ScanContext {
    std::string_view reader;  // the device name, so one broker can tell readers apart
    std::string_view time;    // ISO 8601 UTC, empty until the clock is set
};

void write_scan_json(const Scan& scan, JsonObject out, const ScanContext& context = {});

std::string scan_to_json(const Scan& scan, const ScanContext& context = {});

// One NDEF record: "uri" for URI records, "lang" and "text" for Text records, otherwise
// "payload" as hex plus "payload_text" when the payload is printable UTF-8.
void write_record_json(const ndef::Record& record, JsonObject out);

// True for valid UTF-8 without control characters other than tab, line feed and carriage return.
bool is_printable_utf8(ByteView bytes);

}  // namespace tapwave
