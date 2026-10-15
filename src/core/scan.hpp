// A scan: what was read from a tag and what it means (ARCHITECTURE.md section 8).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/ndef.hpp"
#include "core/tag_uri.hpp"

namespace tapwave {

// The most specific meaning a tag has, highest first.
enum class ScanKind : std::uint8_t {
    HaTag,    // a Home Assistant tag URL
    Tapwave,  // a tapwave:// action
    Music,    // a music service or Music Assistant URI
    Url,      // any other URI
    Text,     // text, no URI
    Uid,      // nothing usable on the tag: only the UID identifies it
};

std::string_view to_string(ScanKind kind);

struct Scan {
    std::string uid;       // "04-A2-1B-3C-4D-5E-80"
    std::string tag_type;  // "NTAG215", "MIFARE Classic 1K", ...
    std::vector<ndef::Record> records;
    std::optional<std::string> ndef_error;  // why the NDEF message could not be read

    ScanKind kind = ScanKind::Uid;
    std::optional<std::string> ha_tag_id;
    std::optional<std::string> tapwave_uri;  // as written on the tag
    std::optional<uri::TapwaveUri> action;   // parsed tapwave_uri
    std::optional<uri::Error> action_error;  // why tapwave_uri did not parse
    std::optional<std::string> music_uri;
    std::optional<std::string> uri;   // the first URI record of any kind
    std::optional<std::string> text;  // the first Text record

    // The tag ID Home Assistant sees: the Home Assistant tag ID if the tag has one, else the UID.
    const std::string& id() const { return ha_tag_id ? *ha_tag_id : uid; }
};

// Builds a scan and fills in the classification from the NDEF records.
Scan classify(std::string uid, std::string tag_type, std::vector<ndef::Record> records);

// The tag ID inside a Home Assistant tag URL (https://www.home-assistant.io/tag/<id>).
std::optional<std::string> home_assistant_tag_id(std::string_view uri);

// Music service links and Music Assistant URIs, the ones adonno/tagreader routes to Music
// Assistant plus the generic "<provider>://<media type>/<item>" form.
bool is_music_uri(std::string_view uri);

}  // namespace tapwave
