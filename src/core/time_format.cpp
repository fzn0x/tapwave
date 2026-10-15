#include "core/time_format.hpp"

#include <array>

namespace tapwave {

// Howard Hinnant's days_from_civil inverse, http://howardhinnant.github.io/date_algorithms.html
CivilDate civil_from_days(std::int64_t days) {
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const auto doe = static_cast<std::uint32_t>(days - era * 146097);                 // [0, 146096]
    const std::uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0, 399]
    const std::uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);                // [0, 365]
    const std::uint32_t mp = (5 * doy + 2) / 153;                                     // [0, 11]
    const unsigned day = doy - (153 * mp + 2) / 5 + 1;
    const unsigned month = mp < 10 ? mp + 3 : mp - 9;
    const auto year = static_cast<std::int32_t>(static_cast<std::int64_t>(yoe) + era * 400 +
                                                (month <= 2 ? 1 : 0));
    return {year, month, day};
}

std::string iso8601_utc(std::int64_t unix_seconds) {
    std::int64_t days = unix_seconds / 86400;
    std::int64_t rest = unix_seconds % 86400;
    if (rest < 0) {
        rest += 86400;
        --days;
    }
    const CivilDate date = civil_from_days(days);
    const auto hour = static_cast<unsigned>(rest / 3600);
    const auto minute = static_cast<unsigned>(rest % 3600 / 60);
    const auto second = static_cast<unsigned>(rest % 60);

    auto two = [](unsigned v) {
        return std::array<char, 2>{static_cast<char>('0' + v / 10),
                                   static_cast<char>('0' + v % 10)};
    };
    std::string out = std::to_string(date.year);
    while (out.size() < 4) {
        out.insert(out.begin(), '0');
    }
    out += '-';
    for (auto [part, separator] :
         {std::pair{date.month, '-'}, std::pair{date.day, 'T'}, std::pair{hour, ':'},
          std::pair{minute, ':'}, std::pair{second, 'Z'}}) {
        const auto digits = two(part);
        out.append(digits.data(), digits.size());
        out += separator;
    }
    return out;
}

}  // namespace tapwave
