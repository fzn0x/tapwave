#include "doctest_main.hpp"

#include <ctime>

#include "core/time_format.hpp"

using namespace tapwave;

TEST_CASE("ISO 8601 UTC formatting") {
    CHECK(iso8601_utc(0) == "1970-01-01T00:00:00Z");
    CHECK(iso8601_utc(1760517012) == "2025-10-15T08:30:12Z");
    CHECK(iso8601_utc(951782400) == "2000-02-29T00:00:00Z");   // leap day in a century leap year
    CHECK(iso8601_utc(4107542399) == "2100-02-28T23:59:59Z");  // 2100 is not a leap year
    CHECK(iso8601_utc(4107542400) == "2100-03-01T00:00:00Z");
    CHECK(iso8601_utc(-1) == "1969-12-31T23:59:59Z");
}

TEST_CASE("civil_from_days") {
    const CivilDate epoch = civil_from_days(0);
    CHECK(epoch.year == 1970);
    CHECK(epoch.month == 1);
    CHECK(epoch.day == 1);
    const CivilDate leap = civil_from_days(19782);  // 2024-02-29
    CHECK(leap.year == 2024);
    CHECK(leap.month == 2);
    CHECK(leap.day == 29);
}

TEST_CASE("agrees with gmtime across three centuries") {
    for (std::int64_t day = 0; day < 109573; day += 13) {                     // 1970 to 2270
        const std::time_t t = static_cast<std::time_t>(day * 86400 + 45296);  // 12:34:56
        std::tm expected{};
#if defined(_WIN32)
        gmtime_s(&expected, &t);
#else
        gmtime_r(&t, &expected);
#endif
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &expected);
        REQUIRE_MESSAGE(iso8601_utc(day * 86400 + 45296) == buffer, day);
    }
}
