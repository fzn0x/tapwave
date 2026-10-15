#include "doctest_main.hpp"

#include "core/pins.hpp"

using namespace tapwave;

namespace {

bool ok(Chip chip, int gpio, PinUse use, bool psram = true) {
    return !check_pin(chip, psram, gpio, use).has_value();
}

bool warns(Chip chip, int gpio, PinUse use, bool psram = true) {
    const auto problem = check_pin(chip, psram, gpio, use);
    return problem && problem->warning;
}

bool refuses(Chip chip, int gpio, PinUse use, bool psram = true) {
    const auto problem = check_pin(chip, psram, gpio, use);
    return problem && !problem->warning;
}

}  // namespace

TEST_CASE("not connected is always fine, other negatives are not") {
    CHECK(ok(Chip::Esp32, -1, PinUse::Output));
    CHECK(ok(Chip::Esp32S3, -1, PinUse::Analog));
    CHECK(refuses(Chip::Esp32, -2, PinUse::Output));
}

TEST_CASE("ESP32: pins that do not exist or belong to flash") {
    for (int gpio : {20, 24, 28, 29, 30, 31, 40}) {
        CHECK_MESSAGE(refuses(Chip::Esp32, gpio, PinUse::Output), gpio);
    }
    for (int gpio = 6; gpio <= 11; ++gpio) {
        CHECK_MESSAGE(refuses(Chip::Esp32, gpio, PinUse::Input), gpio);
    }
}

TEST_CASE("ESP32: the USB serial pins are kept for logs and Improv") {
    CHECK(refuses(Chip::Esp32, 1, PinUse::Output));
    CHECK(refuses(Chip::Esp32, 3, PinUse::Input));
}

TEST_CASE("ESP32: PSRAM pins only on boards with PSRAM") {
    CHECK(refuses(Chip::Esp32, 16, PinUse::Output, true));
    CHECK(refuses(Chip::Esp32, 17, PinUse::Output, true));
    CHECK(ok(Chip::Esp32, 16, PinUse::Output, false));
}

TEST_CASE("ESP32: input-only pins") {
    CHECK(refuses(Chip::Esp32, 34, PinUse::Output));
    CHECK(refuses(Chip::Esp32, 39, PinUse::Bidirectional));
    CHECK(ok(Chip::Esp32, 36, PinUse::Input));
    CHECK(warns(Chip::Esp32, 35, PinUse::InputPullup));
}

TEST_CASE("ESP32: battery sensing needs ADC1") {
    CHECK(ok(Chip::Esp32, 35, PinUse::Analog));
    CHECK(ok(Chip::Esp32, 32, PinUse::Analog));
    CHECK(refuses(Chip::Esp32, 4, PinUse::Analog));
    CHECK(refuses(Chip::Esp32, 25, PinUse::Analog));
}

TEST_CASE("ESP32: risky strapping pins warn") {
    CHECK(warns(Chip::Esp32, 0, PinUse::InputPullup));
    CHECK(warns(Chip::Esp32, 2, PinUse::Output));
    CHECK(warns(Chip::Esp32, 12, PinUse::Output));
    CHECK(ok(Chip::Esp32, 5, PinUse::Output));
    CHECK(ok(Chip::Esp32, 15, PinUse::Output));
}

TEST_CASE("ESP32: the default wiring is clean") {
    for (int gpio : {21, 22, 18, 19, 23, 5, 4, 13, 14, 25, 26, 27, 32, 33}) {
        CHECK_MESSAGE(ok(Chip::Esp32, gpio, PinUse::Output), gpio);
    }
}

TEST_CASE("ESP32-S3 rules") {
    CHECK(refuses(Chip::Esp32S3, 22, PinUse::Output));  // does not exist
    CHECK(refuses(Chip::Esp32S3, 49, PinUse::Output));
    CHECK(refuses(Chip::Esp32S3, 33, PinUse::Output));  // octal flash and PSRAM
    CHECK(refuses(Chip::Esp32S3, 19, PinUse::Output));  // USB
    CHECK(refuses(Chip::Esp32S3, 20, PinUse::Input));
    CHECK(warns(Chip::Esp32S3, 0, PinUse::InputPullup));
    CHECK(warns(Chip::Esp32S3, 46, PinUse::Output));
    CHECK(warns(Chip::Esp32S3, 43, PinUse::Output));
    CHECK(ok(Chip::Esp32S3, 4, PinUse::Analog));
    CHECK(refuses(Chip::Esp32S3, 11, PinUse::Analog));
    for (int gpio : {8, 9, 10, 11, 12, 13, 38, 48}) {
        CHECK_MESSAGE(ok(Chip::Esp32S3, gpio, PinUse::Output), gpio);
    }
}

TEST_CASE("every refusal explains itself") {
    for (int gpio : {7, 16, 34, 40}) {
        const auto problem = check_pin(Chip::Esp32, true, gpio, PinUse::Output);
        REQUIRE(problem);
        CHECK_FALSE(problem->message.empty());
    }
}
