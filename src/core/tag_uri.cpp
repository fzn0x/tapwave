#include "core/tag_uri.hpp"

#include <algorithm>

namespace tapwave::uri {

namespace {

constexpr std::string_view kScheme = "tapwave://";

char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool starts_with_ignoring_case(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           std::ranges::equal(text.substr(0, prefix.size()), prefix,
                              [](char a, char b) { return lower(a) == lower(b); });
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = lower(c);
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

bool action_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
}

Action action_from(std::string_view name) {
    if (name == "sd") {
        return Action::Sd;
    }
    if (name == "stream") {
        return Action::Stream;
    }
    if (name == "ha") {
        return Action::Ha;
    }
    if (name == "cmd") {
        return Action::Cmd;
    }
    return Action::Unknown;
}

void encode_into(std::string& out, std::string_view text, std::string_view also_reserved) {
    constexpr char kHex[] = "0123456789ABCDEF";
    for (char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        const bool reserved = c == '%' || c == '?' || c == '#' || byte <= 0x20 || byte >= 0x7F ||
                              also_reserved.find(c) != std::string_view::npos;
        if (reserved) {
            out.push_back('%');
            out.push_back(kHex[byte >> 4]);
            out.push_back(kHex[byte & 0x0F]);
        } else {
            out.push_back(c);
        }
    }
}

}  // namespace

std::string_view to_string(Action action) {
    switch (action) {
        case Action::Sd:
            return "sd";
        case Action::Stream:
            return "stream";
        case Action::Ha:
            return "ha";
        case Action::Cmd:
            return "cmd";
        case Action::Unknown:
            break;
    }
    return "unknown";
}

std::string_view describe(Error error) {
    switch (error) {
        case Error::NotTapwave:
            return "not a tapwave:// URI";
        case Error::MissingAction:
            return "the URI has no action after tapwave://";
        case Error::BadAction:
            return "the action may only contain letters, digits and '-'";
        case Error::BadEncoding:
            return "a '%' is not followed by two hex digits";
    }
    return "unknown URI error";
}

std::optional<std::string_view> TapwaveUri::option(std::string_view key) const {
    for (const Option& o : options) {
        if (o.key == key) {
            return o.value;
        }
    }
    return std::nullopt;
}

bool has_tapwave_scheme(std::string_view uri) {
    return starts_with_ignoring_case(uri, "tapwave:");
}

std::optional<std::string> percent_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') {
            out.push_back(text[i]);
            continue;
        }
        if (i + 2 >= text.size()) {
            return std::nullopt;
        }
        const int high = hex_digit(text[i + 1]);
        const int low = hex_digit(text[i + 2]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        out.push_back(static_cast<char>((high << 4) | low));
        i += 2;
    }
    return out;
}

std::expected<TapwaveUri, Error> parse(std::string_view uri) {
    if (!starts_with_ignoring_case(uri, kScheme)) {
        return std::unexpected(Error::NotTapwave);
    }
    std::string_view rest = uri.substr(kScheme.size());
    if (const auto fragment = rest.find('#'); fragment != std::string_view::npos) {
        rest = rest.substr(0, fragment);
    }

    const std::size_t action_end = rest.find_first_of("/?");
    const std::string_view action = rest.substr(0, action_end);
    if (action.empty()) {
        return std::unexpected(Error::MissingAction);
    }
    TapwaveUri out;
    out.action_name.reserve(action.size());
    for (char c : action) {
        c = lower(c);
        if (!action_char(c)) {
            return std::unexpected(Error::BadAction);
        }
        out.action_name.push_back(c);
    }
    out.action = action_from(out.action_name);
    rest = action_end == std::string_view::npos ? std::string_view{} : rest.substr(action_end);

    std::string_view query;
    if (const auto question = rest.find('?'); question != std::string_view::npos) {
        query = rest.substr(question + 1);
        rest = rest.substr(0, question);
    }
    if (rest.starts_with('/')) {
        rest.remove_prefix(1);
    }
    const auto target = percent_decode(rest);
    if (!target) {
        return std::unexpected(Error::BadEncoding);
    }
    out.target = *target;

    while (!query.empty()) {
        const std::size_t amp = query.find('&');
        const std::string_view pair = query.substr(0, amp);
        query = amp == std::string_view::npos ? std::string_view{} : query.substr(amp + 1);
        if (pair.empty()) {
            continue;
        }
        const std::size_t equals = pair.find('=');
        const auto key = percent_decode(pair.substr(0, equals));
        const auto value = percent_decode(
            equals == std::string_view::npos ? std::string_view{} : pair.substr(equals + 1));
        if (!key || !value) {
            return std::unexpected(Error::BadEncoding);
        }
        out.options.push_back({*key, *value});
    }
    return out;
}

std::string format(const TapwaveUri& uri) {
    std::string out{kScheme};
    for (char c : uri.action_name) {
        out.push_back(lower(c));
    }
    out.push_back('/');
    encode_into(out, uri.target, {});
    for (std::size_t i = 0; i < uri.options.size(); ++i) {
        out.push_back(i == 0 ? '?' : '&');
        encode_into(out, uri.options[i].key, "&=");
        out.push_back('=');
        encode_into(out, uri.options[i].value, "&=");
    }
    return out;
}

}  // namespace tapwave::uri
