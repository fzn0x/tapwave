// NFC driver registration and auto-detection (ARCHITECTURE.md section 6).
//
// Each driver file registers itself:
//
//   TAPWAVE_REGISTER_NFC_DRIVER("mychip-spi", 40, "MyChip on SPI", probe_mychip);
//
// and detection probes the registered drivers in priority order, lowest first.
#pragma once

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/config.hpp"
#include "drivers/nfc/common/readers.hpp"

namespace tapwave::nfc {

struct ProbeContext {
    const config::Pins& pins;
};

// A probe either returns a working reader or says why it found none ("no answer on I2C
// address 0x24", "nfc_cs pin not set").
using ProbeResult = std::expected<std::unique_ptr<TagReader>, std::string>;
using ProbeFn = ProbeResult (*)(const ProbeContext& context);

struct DriverEntry {
    std::string_view id;  // "pn532-i2c"; also the value of nfc.driver that pins this driver
    int priority = 0;
    std::string_view description;
    ProbeFn probe = nullptr;
};

class Registry {
public:
    static Registry& instance();

    void add(const DriverEntry& entry);
    std::vector<DriverEntry> entries() const;  // sorted by priority
    void clear() { entries_.clear(); }         // for tests

private:
    std::vector<DriverEntry> entries_;
};

struct Registration {
    explicit Registration(const DriverEntry& entry) { Registry::instance().add(entry); }
};

#define TAPWAVE_NFC_CONCAT_(a, b) a##b
#define TAPWAVE_NFC_CONCAT(a, b) TAPWAVE_NFC_CONCAT_(a, b)
#define TAPWAVE_REGISTER_NFC_DRIVER(id, priority, description, probe)                     \
    static const ::tapwave::nfc::Registration TAPWAVE_NFC_CONCAT(nfc_driver_, __LINE__) { \
        ::tapwave::nfc::DriverEntry {                                                     \
            id, priority, description, probe                                              \
        }                                                                                 \
    }

struct ProbeAttempt {
    std::string driver;
    std::string result;  // "found PN532 1.6 (i2c)" or the reason the probe gave up

    friend bool operator==(const ProbeAttempt&, const ProbeAttempt&) = default;
};

struct Detection {
    std::unique_ptr<TagReader> reader;  // nullptr when nothing answered
    std::vector<ProbeAttempt> attempts;
};

// Probes the drivers in priority order and keeps the first that answers. With a driver ID
// instead of "auto", probes only that driver.
Detection detect(const Registry& registry, const ProbeContext& context, std::string_view driver);

}  // namespace tapwave::nfc
