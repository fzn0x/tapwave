#include "core/scan.hpp"

#include <algorithm>
#include <array>

namespace tapwave {

namespace {

char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool starts_with_ignoring_case(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           std::ranges::equal(text.substr(0, prefix.size()), prefix,
                              [](char a, char b) { return lower(a) == lower(b); });
}

constexpr auto kHomeAssistantTagPrefixes = std::to_array<std::string_view>({
    "https://www.home-assistant.io/tag/",
    "https://home-assistant.io/tag/",
    "http://www.home-assistant.io/tag/",
    "http://home-assistant.io/tag/",
});

// Links that adonno/tagreader treats as music, plus Music Assistant provider schemes.
constexpr auto kMusicPrefixes = std::to_array<std::string_view>({
    "https://open.spotify.com/",
    "spotify:",
    "https://music.apple.com/",
    "sonos-2://",
    "apple_music://",
    "audiobookshelf://",
    "deezer://",
    "filesystem_local://",
    "filesystem_smb://",
    "jellyfin://",
    "library://",
    "opensubsonic://",
    "plex://",
    "qobuz://",
    "radiobrowser://",
    "soundcloud://",
    "tidal://",
    "tunein://",
    "ytmusic://",
});

// Music Assistant URIs look like "<provider>://<media type>/<item id>".
constexpr auto kMusicAssistantMediaTypes = std::to_array<std::string_view>({
    "track",
    "album",
    "artist",
    "playlist",
    "radio",
    "audiobook",
    "podcast",
    "folder",
});

bool looks_like_music_assistant_uri(std::string_view uri) {
    const auto separator = uri.find("://");
    if (separator == std::string_view::npos || separator == 0) {
        return false;
    }
    const std::string_view scheme = uri.substr(0, separator);
    for (char c : scheme) {
        const char l = lower(c);
        if (!((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '_' || l == '-')) {
            return false;
        }
    }
    if (starts_with_ignoring_case(scheme, "http") || starts_with_ignoring_case(scheme, "tapwave")) {
        return false;
    }
    const std::string_view rest = uri.substr(separator + 3);
    const auto slash = rest.find('/');
    if (slash == std::string_view::npos || slash + 1 >= rest.size()) {
        return false;
    }
    const std::string_view media_type = rest.substr(0, slash);
    return std::ranges::any_of(kMusicAssistantMediaTypes,
                               [&](std::string_view t) { return media_type == t; });
}

}  // namespace

std::string_view to_string(ScanKind kind) {
    switch (kind) {
        case ScanKind::HaTag:
            return "ha_tag";
        case ScanKind::Tapwave:
            return "tapwave";
        case ScanKind::Music:
            return "music";
        case ScanKind::Url:
            return "url";
        case ScanKind::Text:
            return "text";
        case ScanKind::Uid:
            return "uid";
    }
    return "uid";
}

std::optional<std::string> home_assistant_tag_id(std::string_view uri) {
    for (std::string_view prefix : kHomeAssistantTagPrefixes) {
        if (!starts_with_ignoring_case(uri, prefix)) {
            continue;
        }
        std::string_view id = uri.substr(prefix.size());
        while (!id.empty() && (id.back() == ' ' || id.back() == '\0' || id.back() == '\n' ||
                               id.back() == '\r' || id.back() == '\t')) {
            id.remove_suffix(1);
        }
        if (id.empty()) {
            return std::nullopt;
        }
        return std::string(id);
    }
    return std::nullopt;
}

bool is_music_uri(std::string_view uri) {
    return std::ranges::any_of(
               kMusicPrefixes,
               [&](std::string_view p) { return starts_with_ignoring_case(uri, p); }) ||
           looks_like_music_assistant_uri(uri);
}

Scan classify(std::string uid, std::string tag_type, std::vector<ndef::Record> records) {
    Scan scan;
    scan.uid = std::move(uid);
    scan.tag_type = std::move(tag_type);
    scan.records = std::move(records);

    for (const ndef::Record& record : scan.records) {
        if (const auto u = ndef::uri(record)) {
            if (!scan.uri) {
                scan.uri = *u;
            }
            if (!scan.ha_tag_id) {
                if (auto id = home_assistant_tag_id(*u)) {
                    scan.ha_tag_id = std::move(id);
                    continue;
                }
            }
            if (!scan.tapwave_uri && uri::has_tapwave_scheme(*u)) {
                scan.tapwave_uri = *u;
                if (auto parsed = uri::parse(*u)) {
                    scan.action = std::move(*parsed);
                } else {
                    scan.action_error = parsed.error();
                }
                continue;
            }
            if (!scan.music_uri && is_music_uri(*u)) {
                scan.music_uri = *u;
            }
        } else if (const auto t = ndef::text(record); t && !scan.text) {
            scan.text = t->text;
        }
    }

    if (scan.ha_tag_id) {
        scan.kind = ScanKind::HaTag;
    } else if (scan.tapwave_uri) {
        scan.kind = ScanKind::Tapwave;
    } else if (scan.music_uri) {
        scan.kind = ScanKind::Music;
    } else if (scan.uri) {
        scan.kind = ScanKind::Url;
    } else if (scan.text) {
        scan.kind = ScanKind::Text;
    } else {
        scan.kind = ScanKind::Uid;
    }
    return scan;
}

}  // namespace tapwave
