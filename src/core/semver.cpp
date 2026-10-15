#include "core/semver.hpp"

#include <algorithm>

namespace tapwave {

namespace {

bool all_digits(std::string_view s) {
    return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

std::optional<std::uint32_t> component(std::string_view s) {
    if (!all_digits(s) || s.size() > 9 || (s.size() > 1 && s.front() == '0')) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (char c : s) {
        value = value * 10 + static_cast<std::uint32_t>(c - '0');
    }
    return value;
}

bool valid_identifier(std::string_view s) {
    return !s.empty() && std::ranges::all_of(s, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               c == '-';
    });
}

std::strong_ordering compare_identifier(const std::string& a, const std::string& b) {
    const bool a_num = all_digits(a);
    const bool b_num = all_digits(b);
    if (a_num && b_num) {
        if (a.size() != b.size()) {
            return a.size() <=> b.size();
        }
        return a.compare(b) <=> 0;
    }
    if (a_num != b_num) {
        return a_num ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    return a.compare(b) <=> 0;
}

}  // namespace

std::optional<Version> Version::parse(std::string_view text) {
    if (text.starts_with('v') || text.starts_with('V')) {
        text.remove_prefix(1);
    }
    if (const auto plus = text.find('+'); plus != std::string_view::npos) {
        text = text.substr(0, plus);
    }
    std::string_view pre;
    if (const auto dash = text.find('-'); dash != std::string_view::npos) {
        pre = text.substr(dash + 1);
        text = text.substr(0, dash);
        if (pre.empty()) {
            return std::nullopt;
        }
    }
    Version v;
    std::uint32_t* parts[] = {&v.major, &v.minor, &v.patch};
    for (int i = 0; i < 3; ++i) {
        const std::size_t dot = text.find('.');
        if ((i < 2) == (dot == std::string_view::npos)) {
            return std::nullopt;
        }
        const auto value = component(text.substr(0, dot));
        if (!value) {
            return std::nullopt;
        }
        *parts[i] = *value;
        text = dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);
    }
    while (!pre.empty()) {
        const std::size_t dot = pre.find('.');
        const std::string_view id = pre.substr(0, dot);
        if (!valid_identifier(id)) {
            return std::nullopt;
        }
        v.prerelease.emplace_back(id);
        pre = dot == std::string_view::npos ? std::string_view{} : pre.substr(dot + 1);
        if (dot != std::string_view::npos && pre.empty()) {
            return std::nullopt;
        }
    }
    return v;
}

std::strong_ordering operator<=>(const Version& a, const Version& b) {
    if (auto c = a.major <=> b.major; c != 0) {
        return c;
    }
    if (auto c = a.minor <=> b.minor; c != 0) {
        return c;
    }
    if (auto c = a.patch <=> b.patch; c != 0) {
        return c;
    }
    // A release sorts above its pre-releases.
    if (a.prerelease.empty() || b.prerelease.empty()) {
        return b.prerelease.size() == a.prerelease.size()
                   ? std::strong_ordering::equal
                   : (a.prerelease.empty() ? std::strong_ordering::greater
                                           : std::strong_ordering::less);
    }
    const std::size_t n = std::min(a.prerelease.size(), b.prerelease.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (auto c = compare_identifier(a.prerelease[i], b.prerelease[i]); c != 0) {
            return c;
        }
    }
    return a.prerelease.size() <=> b.prerelease.size();
}

bool is_newer(std::string_view candidate, std::string_view installed) {
    const auto c = Version::parse(candidate);
    if (!c) {
        return false;
    }
    const auto i = Version::parse(installed);
    return !i || *c > *i;
}

}  // namespace tapwave
