// Semantic Versioning 2.0 comparison, for "is the release newer than what is installed".
#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tapwave {

struct Version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
    std::vector<std::string> prerelease;  // "rc.1" -> {"rc", "1"}; build metadata is dropped

    // Accepts an optional leading "v".
    static std::optional<Version> parse(std::string_view text);

    friend std::strong_ordering operator<=>(const Version& a, const Version& b);
    friend bool operator==(const Version& a, const Version& b) {
        return (a <=> b) == std::strong_ordering::equal;
    }
};

// True when `candidate` parses and sorts above `installed`. Unparseable installed versions
// (development builds) count as older than any release.
bool is_newer(std::string_view candidate, std::string_view installed);

}  // namespace tapwave
