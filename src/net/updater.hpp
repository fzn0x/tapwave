// Firmware and web UI updates (ARCHITECTURE.md section 14): uploads through the API, and
// downloads from the release manifest for the Home Assistant update entity and the API.
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "core/ota_manifest.hpp"

namespace tapwave::net {

enum class UpdateTarget : std::uint8_t { Firmware, Filesystem };

enum class UpdatePhase : std::uint8_t { Idle, Checking, Downloading, Writing, Restarting, Failed };

std::string_view to_string(UpdatePhase phase);

struct UpdateStatus {
    UpdatePhase phase = UpdatePhase::Idle;
    std::string installed;
    std::string latest;  // empty until a manifest was read
    std::string release_url;
    std::string target;  // "firmware" or "web interface" while one is being written
    std::optional<int> percent;
    std::string error;
    bool available = false;  // latest is newer than installed
};

class Updater {
public:
    // Reads up to `size` bytes into `buffer`; returns the count, or a negative number on error.
    using Reader = std::function<int(char* buffer, std::size_t size)>;

    void begin(std::string installed_version, std::string environment);

    // Writes an uploaded image. Runs on the caller's task (the HTTP server).
    std::expected<void, std::string> upload(UpdateTarget target, std::size_t size,
                                            const Reader& read);

    // Fetches the manifest, and with `install` downloads and writes the release. Runs on its
    // own task; returns false if an update is already running.
    bool start(std::string manifest_url, bool install);

    UpdateStatus status() const;
    bool busy() const;

    // Called on any task whenever the status changes.
    std::function<void()> on_change;
    // Called before the web UI filesystem is overwritten, so it can be unmounted.
    std::function<void()> before_filesystem_write;

private:
    static void task(void* arg);
    void run(std::string manifest_url, bool install);
    std::expected<void, std::string> download(UpdateTarget target, const ota::Image& image);
    std::expected<void, std::string> write(UpdateTarget target, std::size_t size,
                                           const Reader& read, const std::string* sha256);
    void set(const std::function<void(UpdateStatus&)>& change);

    mutable std::mutex mutex_;
    UpdateStatus status_;
    std::string environment_;
    bool busy_ = false;

    struct Job {
        Updater* self;
        std::string url;
        bool install;
    };
};

}  // namespace tapwave::net
