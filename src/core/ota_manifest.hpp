// The release manifest the device reads to update itself (ARCHITECTURE.md section 14).
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace tapwave::ota {

struct Image {
    std::string url;
    std::uint32_t size = 0;
    std::string sha256;  // 64 lowercase hex digits
};

struct Release {
    std::string version;
    std::string release_url;
    Image firmware;
    std::optional<Image> filesystem;
};

enum class ManifestError : std::uint8_t {
    Json,            // not a JSON object
    MissingVersion,  // no "version"
    NoBuild,         // nothing for this board environment
    BadImage,        // an image without an http(s) URL, a size or a SHA-256
};

std::string_view describe(ManifestError error);

// Picks the build for `env` (the PlatformIO environment, for example "esp32-4mb").
std::expected<Release, ManifestError> parse_manifest(std::string_view json, std::string_view env);

}  // namespace tapwave::ota
