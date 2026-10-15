#include "doctest_main.hpp"

#include "core/rtttl.hpp"

using namespace tapwave::rtttl;

TEST_CASE("tagreader's success melody") {
    const auto tones = parse(kSuccess);
    REQUIRE(tones);
    REQUIRE(tones->size() == 3);
    // b=100: a whole note is 2400 ms, so a sixteenth is 150 ms.
    CHECK((*tones)[0] == Tone{523, 150});  // C5
    CHECK((*tones)[1] == Tone{784, 150});  // G5
    CHECK((*tones)[2] == Tone{988, 150});  // B5
}

TEST_CASE("durations, sharps, dots, octaves and rests") {
    const auto tones = parse("t:d=4,o=4,b=120:8a,a#,2p,a.,a5,16c#6.,h");
    REQUIRE(tones);
    REQUIRE(tones->size() == 7);
    CHECK((*tones)[0] == Tone{440, 250});   // eighth A4 at 120 bpm
    CHECK((*tones)[1] == Tone{466, 500});   // A#4
    CHECK((*tones)[2] == Tone{0, 1000});    // half rest
    CHECK((*tones)[3] == Tone{440, 750});   // dotted quarter
    CHECK((*tones)[4] == Tone{880, 500});   // A5
    CHECK((*tones)[5] == Tone{1109, 188});  // sixteenth C#6, dot after the octave
    CHECK((*tones)[6] == Tone{494, 500});   // h is B
}

TEST_CASE("missing defaults use the RTTTL defaults") {
    const auto tones = parse("x::c");
    REQUIRE(tones);
    // d=4, o=6, b=63
    CHECK((*tones)[0].frequency_hz == 1047);
    CHECK((*tones)[0].duration_ms == 952);
}

TEST_CASE("spaces and upper case are tolerated") {
    const auto tones = parse("Tune: D=8, O=5, B=100 : C, E , G");
    REQUIRE(tones);
    CHECK(tones->size() == 3);
}

TEST_CASE("the built-in melodies parse") {
    for (auto melody : {kSuccess, kError, kWrite, kIdentify}) {
        CHECK_MESSAGE(parse(melody), melody);
    }
}

TEST_CASE("malformed melodies are rejected") {
    CHECK(parse("no sections").error() == Error::Format);
    CHECK(parse("x:d=4").error() == Error::Format);
    CHECK(parse("x:d=3:c").error() == Error::Format);
    CHECK(parse("x:o=9:c").error() == Error::Format);
    CHECK(parse("x:b=1:c").error() == Error::Format);
    CHECK(parse("x:q=4:c").error() == Error::Format);
    CHECK(parse("x:d=4:").error() == Error::Format);
    CHECK(parse("x:d=4:x").error() == Error::Note);
    CHECK(parse("x:d=4:3c").error() == Error::Note);
    CHECK(parse("x:d=4:8").error() == Error::Note);
    CHECK(parse("x:d=4:p#").error() == Error::Note);
    CHECK(parse("x:d=4:c9").error() == Error::Note);
}

TEST_CASE("very long melodies are refused") {
    std::string melody = "x:d=32,o=5,b=900:";
    for (int i = 0; i < 300; ++i) {
        melody += "c,";
    }
    CHECK(parse(melody).error() == Error::TooLong);
}
