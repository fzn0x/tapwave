#include "net/updater.hpp"

#include <Update.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>

#include <array>
#include <memory>

#include "core/bytes.hpp"
#include "core/semver.hpp"

namespace tapwave::net {

namespace {

constexpr const char* kTag = "update";
constexpr std::size_t kManifestMax = 16 * 1024;

// An HTTP GET that follows redirects (GitHub release assets redirect to a CDN).
class Download {
public:
    explicit Download(const std::string& url) {
        esp_http_client_config_t config{};
        config.url = url.c_str();
        config.crt_bundle_attach = esp_crt_bundle_attach;
        config.timeout_ms = 20000;
        config.buffer_size = 4096;
        config.buffer_size_tx = 1024;
        config.user_agent = "Tapwave";
        client_ = esp_http_client_init(&config);
    }

    ~Download() {
        if (client_) {
            esp_http_client_close(client_);
            esp_http_client_cleanup(client_);
        }
    }

    // Opens the URL and returns the content length (or -1 when unknown).
    std::expected<std::int64_t, std::string> open() {
        if (!client_) {
            return std::unexpected(std::string("could not start an HTTP client"));
        }
        for (int redirects = 0; redirects < 6; ++redirects) {
            if (esp_http_client_open(client_, 0) != ESP_OK) {
                return std::unexpected(std::string("could not connect to the update server"));
            }
            const std::int64_t length = esp_http_client_fetch_headers(client_);
            const int status = esp_http_client_get_status_code(client_);
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                esp_http_client_set_redirection(client_);
                std::array<char, 256> drain;
                while (esp_http_client_read(client_, drain.data(), drain.size()) > 0) {
                }
                continue;
            }
            if (status != 200) {
                return std::unexpected("the update server answered HTTP " + std::to_string(status));
            }
            return length;
        }
        return std::unexpected(std::string("too many redirects"));
    }

    int read(char* buffer, std::size_t size) {
        return esp_http_client_read(client_, buffer, static_cast<int>(size));
    }

private:
    esp_http_client_handle_t client_ = nullptr;
};

}  // namespace

std::string_view to_string(UpdatePhase phase) {
    switch (phase) {
        case UpdatePhase::Idle:
            return "idle";
        case UpdatePhase::Checking:
            return "checking";
        case UpdatePhase::Downloading:
            return "downloading";
        case UpdatePhase::Writing:
            return "writing";
        case UpdatePhase::Restarting:
            return "restarting";
        case UpdatePhase::Failed:
            return "failed";
    }
    return "idle";
}

void Updater::begin(std::string installed_version, std::string environment) {
    std::scoped_lock lock(mutex_);
    status_.installed = std::move(installed_version);
    environment_ = std::move(environment);
}

UpdateStatus Updater::status() const {
    std::scoped_lock lock(mutex_);
    return status_;
}

bool Updater::busy() const {
    std::scoped_lock lock(mutex_);
    return busy_;
}

void Updater::set(const std::function<void(UpdateStatus&)>& change) {
    {
        std::scoped_lock lock(mutex_);
        change(status_);
    }
    if (on_change) {
        on_change();
    }
}

std::expected<void, std::string> Updater::write(UpdateTarget target, std::size_t size,
                                                const Reader& read, const std::string* sha256) {
    const bool firmware = target == UpdateTarget::Firmware;
    if (!firmware && before_filesystem_write) {
        before_filesystem_write();
    }
    if (!Update.begin(size, firmware ? U_FLASH : U_SPIFFS)) {
        return std::unexpected("the image does not fit: " + std::string(Update.errorString()));
    }
    set([&](UpdateStatus& s) {
        s.phase = UpdatePhase::Writing;
        s.target = firmware ? "firmware" : "web interface";
        s.percent = 0;
        s.error.clear();
    });

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    std::array<char, 2048> buffer;
    std::size_t written = 0;
    int last_percent = 0;
    while (written < size) {
        const int n = read(buffer.data(), std::min(buffer.size(), size - written));
        if (n <= 0) {
            Update.abort();
            mbedtls_sha256_free(&sha);
            return std::unexpected(std::string("the download stopped before the end"));
        }
        mbedtls_sha256_update(&sha, reinterpret_cast<const unsigned char*>(buffer.data()),
                              static_cast<std::size_t>(n));
        if (Update.write(reinterpret_cast<std::uint8_t*>(buffer.data()),
                         static_cast<std::size_t>(n)) != static_cast<std::size_t>(n)) {
            const std::string reason = Update.errorString();
            Update.abort();
            mbedtls_sha256_free(&sha);
            return std::unexpected("writing the image failed: " + reason);
        }
        written += static_cast<std::size_t>(n);
        const int percent = static_cast<int>(written * 100 / size);
        if (percent >= last_percent + 5 || percent == 100) {
            last_percent = percent;
            set([&](UpdateStatus& s) { s.percent = percent; });
        }
    }
    std::array<unsigned char, 32> digest;
    mbedtls_sha256_finish(&sha, digest.data());
    mbedtls_sha256_free(&sha);
    if (sha256 && to_hex(digest, {}, false) != *sha256) {
        Update.abort();
        return std::unexpected(std::string("the image does not match its SHA-256 checksum"));
    }
    if (!Update.end(true)) {
        return std::unexpected("the image was rejected: " + std::string(Update.errorString()));
    }
    return {};
}

std::expected<void, std::string> Updater::upload(UpdateTarget target, std::size_t size,
                                                 const Reader& read) {
    {
        std::scoped_lock lock(mutex_);
        if (busy_) {
            return std::unexpected(std::string("another update is running"));
        }
        busy_ = true;
    }
    auto result = write(target, size, read, nullptr);
    set([&](UpdateStatus& s) {
        s.phase = result ? UpdatePhase::Restarting : UpdatePhase::Failed;
        s.error = result ? std::string{} : result.error();
    });
    std::scoped_lock lock(mutex_);
    busy_ = false;
    return result;
}

std::expected<void, std::string> Updater::download(UpdateTarget target, const ota::Image& image) {
    Download download(image.url);
    set([&](UpdateStatus& s) {
        s.phase = UpdatePhase::Downloading;
        s.target = target == UpdateTarget::Firmware ? "firmware" : "web interface";
        s.percent = 0;
    });
    const auto length = download.open();
    if (!length) {
        return std::unexpected(length.error());
    }
    if (*length >= 0 && static_cast<std::uint64_t>(*length) != image.size) {
        return std::unexpected(std::string("the download size does not match the release"));
    }
    return write(
        target, image.size,
        [&](char* buffer, std::size_t size) { return download.read(buffer, size); }, &image.sha256);
}

void Updater::run(std::string manifest_url, bool install) {
    set([](UpdateStatus& s) {
        s.phase = UpdatePhase::Checking;
        s.error.clear();
        s.percent.reset();
    });

    auto fail = [this](std::string reason) {
        ESP_LOGW(kTag, "%s", reason.c_str());
        set([&](UpdateStatus& s) {
            s.phase = UpdatePhase::Failed;
            s.error = std::move(reason);
            s.percent.reset();
        });
    };

    std::string manifest;
    {
        Download download(manifest_url);
        const auto length = download.open();
        if (!length) {
            fail(length.error());
            return;
        }
        std::array<char, 1024> buffer;
        while (manifest.size() < kManifestMax) {
            const int n = download.read(buffer.data(), buffer.size());
            if (n <= 0) {
                break;
            }
            manifest.append(buffer.data(), static_cast<std::size_t>(n));
        }
    }
    const auto release = ota::parse_manifest(manifest, environment_);
    if (!release) {
        fail(std::string(ota::describe(release.error())));
        return;
    }
    const bool newer = is_newer(release->version, status().installed);
    set([&](UpdateStatus& s) {
        s.latest = release->version;
        s.release_url = release->release_url;
        s.available = newer;
        s.phase = UpdatePhase::Idle;
    });
    ESP_LOGI(kTag, "latest release %s, %s", release->version.c_str(),
             newer ? "newer than this firmware" : "already installed");
    if (!install) {
        return;
    }
    if (!newer) {
        fail("version " + release->version + " is already installed");
        return;
    }
    // The web UI first: if it fails, the running firmware is untouched and still works.
    if (release->filesystem) {
        if (auto written = download(UpdateTarget::Filesystem, *release->filesystem); !written) {
            fail(written.error());
            return;
        }
    }
    if (auto written = download(UpdateTarget::Firmware, release->firmware); !written) {
        fail(written.error());
        return;
    }
    set([](UpdateStatus& s) {
        s.phase = UpdatePhase::Restarting;
        s.percent = 100;
    });
}

void Updater::task(void* arg) {
    std::unique_ptr<Job> job(static_cast<Job*>(arg));
    job->self->run(job->url, job->install);
    {
        std::scoped_lock lock(job->self->mutex_);
        job->self->busy_ = false;
    }
    if (job->self->on_change) {
        job->self->on_change();
    }
    vTaskDelete(nullptr);
}

bool Updater::start(std::string manifest_url, bool install) {
    {
        std::scoped_lock lock(mutex_);
        if (busy_) {
            return false;
        }
        busy_ = true;
    }
    auto* job = new Job{this, std::move(manifest_url), install};
    if (xTaskCreate(&Updater::task, "update", 8192, job, 2, nullptr) != pdPASS) {
        delete job;
        std::scoped_lock lock(mutex_);
        busy_ = false;
        return false;
    }
    return true;
}

}  // namespace tapwave::net
