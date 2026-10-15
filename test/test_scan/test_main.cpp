#include "doctest_main.hpp"

#include "core/scan.hpp"
#include "core/scan_json.hpp"

using namespace tapwave;

namespace {

constexpr const char* kUid = "04-A2-1B-3C-4D-5E-80";

Scan classify_records(std::vector<ndef::Record> records) {
    return classify(kUid, "NTAG215", std::move(records));
}

JsonDocument json_of(const Scan& scan, const ScanContext& context = {}) {
    JsonDocument doc;
    deserializeJson(doc, scan_to_json(scan, context));
    return doc;
}

}  // namespace

TEST_CASE("a tag without NDEF is identified by its UID") {
    const Scan scan = classify_records({});
    CHECK(scan.kind == ScanKind::Uid);
    CHECK(scan.id() == kUid);
    CHECK_FALSE(scan.uri);
}

TEST_CASE("Home Assistant tag URLs report their tag ID") {
    const Scan scan = classify_records({ndef::uri_record(
        "https://www.home-assistant.io/tag/8f3b2c1e-6a5d-4d1e-9c0b-2f4e6a8b0c1d")});
    CHECK(scan.kind == ScanKind::HaTag);
    CHECK(scan.id() == "8f3b2c1e-6a5d-4d1e-9c0b-2f4e6a8b0c1d");
    CHECK(scan.uri == "https://www.home-assistant.io/tag/8f3b2c1e-6a5d-4d1e-9c0b-2f4e6a8b0c1d");
}

TEST_CASE("home_assistant_tag_id accepts the URL variants and trims padding") {
    CHECK(home_assistant_tag_id("https://www.home-assistant.io/tag/abc") == "abc");
    CHECK(home_assistant_tag_id("https://home-assistant.io/tag/abc") == "abc");
    CHECK(home_assistant_tag_id("HTTP://WWW.HOME-ASSISTANT.IO/tag/MyTag") == "MyTag");
    CHECK(home_assistant_tag_id(std::string("https://www.home-assistant.io/tag/abc \0", 39)) ==
          "abc");
    CHECK_FALSE(home_assistant_tag_id("https://www.home-assistant.io/tag/"));
    CHECK_FALSE(home_assistant_tag_id("https://www.home-assistant.io/blog/abc"));
}

TEST_CASE("tapwave URIs carry a parsed action") {
    const Scan scan = classify_records({ndef::uri_record("tapwave://sd/Music?mode=shuffle")});
    CHECK(scan.kind == ScanKind::Tapwave);
    REQUIRE(scan.action);
    CHECK(scan.action->target == "Music");
    CHECK(scan.id() == kUid);
}

TEST_CASE("a broken tapwave URI is still a tapwave tag, with the reason") {
    const Scan scan = classify_records({ndef::uri_record("tapwave://sd/50%")});
    CHECK(scan.kind == ScanKind::Tapwave);
    CHECK_FALSE(scan.action);
    CHECK(scan.action_error == uri::Error::BadEncoding);
}

TEST_CASE("music links and Music Assistant URIs") {
    CHECK(is_music_uri("https://open.spotify.com/album/4aawyAB9vmqN3uQ7FjRGTy"));
    CHECK(is_music_uri("spotify:album:4aawyAB9vmqN3uQ7FjRGTy"));
    CHECK(is_music_uri("https://music.apple.com/us/album/1"));
    CHECK(is_music_uri("sonos-2://x"));
    CHECK(is_music_uri("tidal://track/12345"));
    CHECK(is_music_uri("library://album/12"));
    CHECK(is_music_uri("my_provider://playlist/abc"));
    CHECK_FALSE(is_music_uri("https://track/album"));
    CHECK_FALSE(is_music_uri("tapwave://album/x"));
    CHECK_FALSE(is_music_uri("custom://thing/x"));
    CHECK_FALSE(is_music_uri("custom://album/"));
    CHECK_FALSE(is_music_uri("https://example.com/song.mp3"));

    const Scan scan = classify_records({ndef::uri_record("spotify://album/4aaw")});
    CHECK(scan.kind == ScanKind::Music);
    CHECK(scan.music_uri == "spotify://album/4aaw");
}

TEST_CASE("plain URLs and text") {
    CHECK(classify_records({ndef::uri_record("https://example.com/live.mp3")}).kind ==
          ScanKind::Url);
    const Scan text = classify_records({ndef::text_record("Gruffalo")});
    CHECK(text.kind == ScanKind::Text);
    CHECK(text.text == "Gruffalo");
}

TEST_CASE("the most specific meaning wins, and every field is kept") {
    // tagreader's music tags pair a URI with a Text record for the title.
    const Scan scan = classify_records({
        ndef::text_record("Bedtime"),
        ndef::uri_record("spotify://playlist/abc"),
        ndef::uri_record("tapwave://cmd/sleep/20"),
        ndef::uri_record("https://www.home-assistant.io/tag/bedtime"),
    });
    CHECK(scan.kind == ScanKind::HaTag);
    CHECK(scan.id() == "bedtime");
    CHECK(scan.text == "Bedtime");
    CHECK(scan.music_uri == "spotify://playlist/abc");
    REQUIRE(scan.action);
    CHECK(scan.action->target == "sleep/20");
    CHECK(scan.uri == "spotify://playlist/abc");
}

TEST_CASE("kind names match the documentation") {
    CHECK(to_string(ScanKind::HaTag) == "ha_tag");
    CHECK(to_string(ScanKind::Tapwave) == "tapwave");
    CHECK(to_string(ScanKind::Music) == "music");
    CHECK(to_string(ScanKind::Url) == "url");
    CHECK(to_string(ScanKind::Text) == "text");
    CHECK(to_string(ScanKind::Uid) == "uid");
}

TEST_CASE("scan JSON for a Home Assistant tag") {
    const Scan scan = classify_records({ndef::uri_record("https://www.home-assistant.io/tag/abc"),
                                        ndef::text_record("Kitchen lights")});
    const JsonDocument doc = json_of(scan, {"tapwave-a1b2c3", "2026-10-15T08:30:12Z"});
    CHECK(doc["id"] == "abc");
    CHECK(doc["uid"] == kUid);
    CHECK(doc["kind"] == "ha_tag");
    CHECK(doc["tag"] == "NTAG215");
    CHECK(doc["text"] == "Kitchen lights");
    CHECK(doc["reader"] == "tapwave-a1b2c3");
    CHECK(doc["time"] == "2026-10-15T08:30:12Z");
    CHECK(doc["records"].size() == 2);
    CHECK(doc["records"][0]["tnf"] == 1);
    CHECK(doc["records"][0]["type"] == "U");
    CHECK(doc["records"][0]["uri"] == "https://www.home-assistant.io/tag/abc");
    CHECK(doc["records"][1]["lang"] == "en");
    CHECK_FALSE(doc["action"].is<JsonObjectConst>());
    CHECK_FALSE(doc["ndef_error"].is<const char*>());
}

TEST_CASE("scan JSON describes the tapwave action") {
    const JsonDocument good =
        json_of(classify_records({ndef::uri_record("tapwave://sd/Music?mode=shuffle")}));
    CHECK(good["action"]["name"] == "sd");
    CHECK(good["action"]["target"] == "Music");
    CHECK(good["action"]["supported"] == true);
    CHECK(good["action"]["options"]["mode"] == "shuffle");

    const JsonDocument bad = json_of(classify_records({ndef::uri_record("tapwave://sd/50%")}));
    CHECK(bad["action"]["uri"] == "tapwave://sd/50%");
    CHECK(bad["action"]["supported"] == false);
    CHECK(bad["action"]["error"].is<const char*>());
}

TEST_CASE("scan JSON shows MIME payloads as hex and, when printable, as text") {
    Scan scan =
        classify_records({ndef::mime_record("application/json", as_bytes(R"({"a":1})")),
                          ndef::mime_record("application/octet-stream", Bytes{0x00, 0xFF})});
    scan.ndef_error = "cut short";
    const JsonDocument doc = json_of(scan);
    CHECK(doc["records"][0]["payload"] == "7B2261223A317D");
    CHECK(doc["records"][0]["payload_text"] == R"({"a":1})");
    CHECK(doc["records"][1]["payload"] == "00FF");
    CHECK_FALSE(doc["records"][1]["payload_text"].is<const char*>());
    CHECK(doc["ndef_error"] == "cut short");
    CHECK_FALSE(doc["reader"].is<const char*>());
}

TEST_CASE("is_printable_utf8") {
    CHECK(is_printable_utf8(as_bytes("plain text\twith tab\n")));
    CHECK(is_printable_utf8(as_bytes("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x8E\xB5")));
    CHECK(is_printable_utf8(Bytes{}));
    CHECK_FALSE(is_printable_utf8(Bytes{0x00}));
    CHECK_FALSE(is_printable_utf8(Bytes{0x7F}));
    CHECK_FALSE(is_printable_utf8(Bytes{0xC3}));                    // cut off
    CHECK_FALSE(is_printable_utf8(Bytes{0xC0, 0xAF}));              // overlong
    CHECK_FALSE(is_printable_utf8(Bytes{0xED, 0xA0, 0x80}));        // surrogate
    CHECK_FALSE(is_printable_utf8(Bytes{0xF4, 0x90, 0x80, 0x80}));  // past U+10FFFF
    CHECK_FALSE(is_printable_utf8(Bytes{0xFF}));
}
