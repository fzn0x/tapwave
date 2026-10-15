#include "app/scan_service.hpp"

#include <Arduino.h>
#include <esp_log.h>

#include "core/ndef.hpp"

namespace tapwave::app {

namespace {

constexpr const char* kTag = "scan";

}  // namespace

void ScanService::start(const config::Config& config, EventQueue<Event>& events) {
    pins_ = config.pins;
    driver_ = config.nfc.driver;
    set_timing(config.nfc.poll_interval_ms, config.nfc.removal_polls);
    events_ = &events;
    // Core 1, next to the loop task; Wi-Fi and Bluetooth run on core 0.
    xTaskCreatePinnedToCore(&ScanService::task, "nfc", 6144, this, 3, nullptr, 1);
}

void ScanService::set_timing(int poll_interval_ms, int removal_polls) {
    poll_interval_ms_ = poll_interval_ms;
    removal_polls_ = removal_polls;
}

ReaderStatus ScanService::status() const {
    std::scoped_lock lock(mutex_);
    return status_;
}

void ScanService::update(const std::function<void(ReaderStatus&)>& change) {
    {
        std::scoped_lock lock(mutex_);
        change(status_);
    }
    events_->push(ReaderChanged{});
}

void ScanService::task(void* self) {
    static_cast<ScanService*>(self)->run();
}

void ScanService::detect_reader() {
    last_probe_ = millis();
    auto detection = nfc::detect(nfc::Registry::instance(), {pins_}, driver_);
    for (const auto& attempt : detection.attempts) {
        ESP_LOGI(kTag, "%s: %s", attempt.driver.c_str(), attempt.result.c_str());
    }
    reader_ = std::move(detection.reader);
    failures_ = 0;
    update([&](ReaderStatus& s) {
        s.found = reader_ != nullptr;
        s.info = reader_ ? reader_->info() : nfc::ReaderInfo{};
        s.attempts = std::move(detection.attempts);
        s.problem = reader_ ? std::string{}
                            : std::string(
                                  "No reader answered. Check the wiring and the pins in "
                                  "Settings, Hardware.");
        s.tag_present = false;
    });
}

Scan ScanService::make_scan(const nfc::TagRead& read) const {
    std::vector<ndef::Record> records;
    std::optional<std::string> problem = read.ndef_issue;
    if (read.ndef && !read.ndef->empty()) {
        if (auto parsed = ndef::parse(*read.ndef)) {
            records = std::move(*parsed);
        } else {
            problem = std::string(ndef::describe(parsed.error()));
        }
    }
    Scan scan = classify(read.target.uid.to_string(), std::string(nfc::to_string(read.family)),
                         std::move(records));
    scan.ndef_error = std::move(problem);
    return scan;
}

void ScanService::poll() {
    const auto present = reader_->detect();
    if (!present) {
        if (++failures_ >= kFailuresBeforeLost) {
            ESP_LOGE(kTag, "the reader stopped answering: %s",
                     std::string(nfc::describe(present.error())).c_str());
            reader_.reset();
            current_.reset();
            last_probe_ = millis();
            update([](ReaderStatus& s) {
                s.found = false;
                s.tag_present = false;
                s.problem = "The reader stopped answering. Check its power and wiring.";
            });
        }
        return;
    }
    failures_ = 0;

    if (*present) {
        misses_ = 0;
        if (current_ && **present == *current_) {
            return;  // the same tag is still there
        }
        current_ = **present;
        auto read = reader_->read();
        if (!read && read.error() == nfc::Error::NoTag) {
            current_.reset();  // it left while we read it
            return;
        }
        Scan scan;
        if (read) {
            scan = make_scan(*read);
        } else {
            scan = classify(current_->to_string(), "Unknown", {});
            scan.ndef_error = "reading the tag failed: " + std::string(nfc::describe(read.error()));
        }
        ESP_LOGI(kTag, "tag %s (%s) id %s", scan.uid.c_str(),
                 std::string(to_string(scan.kind)).c_str(), scan.id().c_str());
        update([](ReaderStatus& s) { s.tag_present = true; });
        events_->push(TagScanned{std::move(scan)});
        return;
    }

    if (current_ && ++misses_ >= removal_polls_) {
        ESP_LOGI(kTag, "tag %s removed", current_->to_string().c_str());
        events_->push(TagRemoved{current_->to_string()});
        current_.reset();
        update([](ReaderStatus& s) { s.tag_present = false; });
    }
}

void ScanService::run() {
    detect_reader();
    while (true) {
        if (redetect_.exchange(false)) {
            reader_.reset();
            current_.reset();
            detect_reader();
        }
        if (!reader_) {
            if (millis() - last_probe_ >= kReprobeMs) {
                detect_reader();
            }
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        poll();
        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms_.load()));
    }
}

}  // namespace tapwave::app
