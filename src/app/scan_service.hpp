// Polls the NFC reader on its own task and posts scans and removals as events.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "app/events.hpp"
#include "core/config.hpp"
#include "core/event_bus.hpp"
#include "drivers/nfc/common/registry.hpp"

namespace tapwave::app {

struct ReaderStatus {
    bool found = false;
    nfc::ReaderInfo info;
    std::vector<nfc::ProbeAttempt> attempts;
    std::string problem;  // why there is no reader, in words
    bool tag_present = false;
};

class ScanService {
public:
    static constexpr std::uint32_t kReprobeMs = 30000;
    static constexpr int kFailuresBeforeLost = 10;

    void start(const config::Config& config, EventQueue<Event>& events);

    // Safe from any task.
    void redetect() { redetect_ = true; }
    void set_timing(int poll_interval_ms, int removal_polls);
    ReaderStatus status() const;

private:
    static void task(void* self);
    void run();
    void detect_reader();
    void poll();
    Scan make_scan(const nfc::TagRead& read) const;
    void update(const std::function<void(ReaderStatus&)>& change);

    config::Pins pins_;
    std::string driver_;
    std::atomic<int> poll_interval_ms_{150};
    std::atomic<int> removal_polls_{3};
    std::atomic<bool> redetect_{false};
    EventQueue<Event>* events_ = nullptr;

    std::unique_ptr<nfc::TagReader> reader_;
    std::optional<Uid> current_;
    int misses_ = 0;
    int failures_ = 0;
    std::uint32_t last_probe_ = 0;

    mutable std::mutex mutex_;
    ReaderStatus status_;
};

}  // namespace tapwave::app
