#include "doctest_main.hpp"

#include "core/tag_uri.hpp"

using namespace tapwave::uri;

TEST_CASE("parses an SD card action with options") {
    const auto uri = parse("tapwave://sd/Audiobooks/The%20Gruffalo?mode=audiobook&volume=40");
    REQUIRE(uri);
    CHECK(uri->action == Action::Sd);
    CHECK(uri->action_name == "sd");
    CHECK(uri->target == "Audiobooks/The Gruffalo");
    CHECK(uri->supported());
    CHECK(uri->option("mode") == "audiobook");
    CHECK(uri->option("volume") == "40");
    CHECK_FALSE(uri->option("track"));
}

TEST_CASE("stream targets keep their scheme and slashes") {
    const auto uri = parse("tapwave://stream/https://icecast.example.org/jazz.mp3");
    REQUIRE(uri);
    CHECK(uri->action == Action::Stream);
    CHECK(uri->target == "https://icecast.example.org/jazz.mp3");
    CHECK(uri->options.empty());
}

TEST_CASE("an encoded question mark stays in the target") {
    const auto uri = parse("tapwave://stream/https://radio.example/live.mp3%3Fkey=1?volume=30");
    REQUIRE(uri);
    CHECK(uri->target == "https://radio.example/live.mp3?key=1");
    CHECK(uri->option("volume") == "30");
}

TEST_CASE("Home Assistant media IDs with their own scheme") {
    const auto uri =
        parse("tapwave://ha/spotify://playlist/37i9dQZF1DX4sWSpwq3LiO?player=media_player.kids");
    REQUIRE(uri);
    CHECK(uri->action == Action::Ha);
    CHECK(uri->target == "spotify://playlist/37i9dQZF1DX4sWSpwq3LiO");
    CHECK(uri->option("player") == "media_player.kids");
}

TEST_CASE("commands keep their arguments in the target") {
    const auto sleep = parse("tapwave://cmd/sleep/20");
    REQUIRE(sleep);
    CHECK(sleep->action == Action::Cmd);
    CHECK(sleep->target == "sleep/20");
    CHECK(parse("tapwave://cmd/volume/-10")->target == "volume/-10");
    CHECK(parse("tapwave://cmd/volume/+5")->target == "volume/+5");
}

TEST_CASE("scheme and action are case-insensitive, the target is not") {
    const auto uri = parse("TapWave://SD/Music/ABC");
    REQUIRE(uri);
    CHECK(uri->action == Action::Sd);
    CHECK(uri->action_name == "sd");
    CHECK(uri->target == "Music/ABC");
    CHECK(has_tapwave_scheme("TAPWAVE://cmd/next"));
    CHECK_FALSE(has_tapwave_scheme("https://example.com"));
}

TEST_CASE("unknown actions parse but are not supported") {
    const auto uri = parse("tapwave://lights/kitchen?brightness=50");
    REQUIRE(uri);
    CHECK(uri->action == Action::Unknown);
    CHECK(uri->action_name == "lights");
    CHECK_FALSE(uri->supported());
    CHECK(uri->option("brightness") == "50");
}

TEST_CASE("an action without a target or with an empty query") {
    const auto bare = parse("tapwave://sd");
    REQUIRE(bare);
    CHECK(bare->target.empty());
    const auto query_only = parse("tapwave://cmd?x=1&&y");
    REQUIRE(query_only);
    CHECK(query_only->target.empty());
    CHECK(query_only->options.size() == 2);
    CHECK(query_only->option("y") == "");
}

TEST_CASE("fragments are dropped") {
    CHECK(parse("tapwave://sd/Music#ignored")->target == "Music");
}

TEST_CASE("invalid URIs are rejected with a reason") {
    CHECK(parse("https://example.com").error() == Error::NotTapwave);
    CHECK(parse("tapwave:sd/x").error() == Error::NotTapwave);
    CHECK(parse("tapwave://").error() == Error::MissingAction);
    CHECK(parse("tapwave:///Music").error() == Error::MissingAction);
    CHECK(parse("tapwave://s_d/x").error() == Error::BadAction);
    CHECK(parse("tapwave://sd/50%").error() == Error::BadEncoding);
    CHECK(parse("tapwave://sd/%G1").error() == Error::BadEncoding);
    CHECK(parse("tapwave://sd/x?a=%2").error() == Error::BadEncoding);
    for (auto e : {Error::NotTapwave, Error::MissingAction, Error::BadAction, Error::BadEncoding}) {
        CHECK_FALSE(describe(e).empty());
    }
}

TEST_CASE("percent_decode") {
    CHECK(percent_decode("a%20b") == "a b");
    CHECK(percent_decode("%C3%A9") == "\xC3\xA9");
    CHECK(percent_decode("100%25") == "100%");
    CHECK_FALSE(percent_decode("%"));
    CHECK_FALSE(percent_decode("%4"));
    CHECK_FALSE(percent_decode("%zz"));
}

TEST_CASE("format writes the canonical form and round-trips") {
    TapwaveUri uri;
    uri.action = Action::Sd;
    uri.action_name = "sd";
    uri.target = "Hörbücher/Die Maus #1?";
    uri.options = {{"mode", "audiobook"}, {"note", "a&b=c"}};
    const std::string text = format(uri);
    CHECK(text ==
          "tapwave://sd/H%C3%B6rb%C3%BCcher/Die%20Maus%20%231%3F?mode=audiobook&note=a%26b%3Dc");
    const auto parsed = parse(text);
    REQUIRE(parsed);
    CHECK(*parsed == uri);
}

TEST_CASE("format leaves stream URLs readable") {
    TapwaveUri uri{Action::Stream, "stream", "https://radio.example/live.mp3", {}};
    CHECK(format(uri) == "tapwave://stream/https://radio.example/live.mp3");
    CHECK(to_string(Action::Stream) == "stream");
    CHECK(to_string(Action::Unknown) == "unknown");
}
