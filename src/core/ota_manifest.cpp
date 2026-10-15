#include "core/ota_manifest.hpp"

#include <ArduinoJson.h>

#include <algorithm>

namespace tapwave::ota {

namespace {

std::optional<Image> read_image(JsonVariantConst v) {
    if (!v.is<JsonObjectConst>()) {
        return std::nullopt;
    }
    Image image;
    const char* url = v["url"];
    const char* sha = v["sha256"];
    if (!url || !sha || !v["size"].is<std::uint32_t>()) {
        return std::nullopt;
    }
    image.url = url;
    image.size = v["size"].as<std::uint32_t>();
    image.sha256 = sha;
    std::ranges::transform(image.sha256, image.sha256.begin(), [](char c) {
        return (c >= 'A' && c <= 'F') ? static_cast<char>(c - 'A' + 'a') : c;
    });
    const bool url_ok = image.url.starts_with("https://") || image.url.starts_with("http://");
    const bool sha_ok = image.sha256.size() == 64 && std::ranges::all_of(image.sha256, [](char c) {
                            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                        });
    if (!url_ok || !sha_ok || image.size == 0) {
        return std::nullopt;
    }
    return image;
}

}  // namespace

std::string_view describe(ManifestError error) {
    switch (error) {
        case ManifestError::Json:
            return "the release manifest is not valid JSON";
        case ManifestError::MissingVersion:
            return "the release manifest has no version";
        case ManifestError::NoBuild:
            return "the release has no firmware for this board";
        case ManifestError::BadImage:
            return "the release manifest lists an incomplete firmware image";
    }
    return "unknown manifest error";
}

std::expected<Release, ManifestError> parse_manifest(std::string_view json, std::string_view env) {
    JsonDocument doc;
    if (deserializeJson(doc, json.data(), json.size()) || !doc.is<JsonObjectConst>()) {
        return std::unexpected(ManifestError::Json);
    }
    const char* version = doc["version"];
    if (!version || !*version) {
        return std::unexpected(ManifestError::MissingVersion);
    }
    Release release;
    release.version = version;
    if (const char* url = doc["release_url"]) {
        release.release_url = url;
    }
    const JsonVariantConst build = doc["builds"][env];
    if (!build.is<JsonObjectConst>()) {
        return std::unexpected(ManifestError::NoBuild);
    }
    const auto firmware = read_image(build["firmware"]);
    if (!firmware) {
        return std::unexpected(ManifestError::BadImage);
    }
    release.firmware = *firmware;
    if (!build["filesystem"].isNull()) {
        const auto filesystem = read_image(build["filesystem"]);
        if (!filesystem) {
            return std::unexpected(ManifestError::BadImage);
        }
        release.filesystem = *filesystem;
    }
    return release;
}

}  // namespace tapwave::ota
