// UTC timestamps without the C library's time functions.
//
// With the ESP32 PSRAM cache workaround the C library runs from IRAM, and strftime, gmtime_r
// and tzset alone take over 5 KB of it. Scan timestamps only need UTC in ISO 8601, which is a
// few lines of arithmetic.
#pragma once

#include <cstdint>
#include <string>

namespace tapwave {

struct CivilDate {
    std::int32_t year;
    unsigned month;  // 1 to 12
    unsigned day;    // 1 to 31
};

// The calendar date of a day count since 1970-01-01 (proleptic Gregorian calendar).
CivilDate civil_from_days(std::int64_t days);

// "2026-10-15T08:30:12Z"
std::string iso8601_utc(std::int64_t unix_seconds);

}  // namespace tapwave
