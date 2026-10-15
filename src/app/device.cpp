#include "app/device.hpp"

#include <Esp.h>
#include <esp32-hal-psram.h>
#include <esp_mac.h>

#include <cctype>

#include "core/bytes.hpp"

#ifndef TAPWAVE_VERSION
#define TAPWAVE_VERSION "0.0.0-dev"
#endif
#ifndef TAPWAVE_ENV
#define TAPWAVE_ENV "unknown"
#endif

namespace tapwave::app {

Device Device::detect() {
    Device d;
#if CONFIG_IDF_TARGET_ESP32S3
    d.chip = Chip::Esp32S3;
#else
    d.chip = Chip::Esp32;
#endif
    d.chip_model = std::string(ESP.getChipModel()) + " rev " +
                   std::to_string(ESP.getChipRevision() / 100) + "." +
                   std::to_string(ESP.getChipRevision() % 100);
    d.has_psram = psramFound();
    d.psram_size = d.has_psram ? ESP.getPsramSize() : 0;
    d.flash_size = ESP.getFlashChipSize();

    std::uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    d.mac = to_hex(ByteView(mac, 6), ":", false);
    d.id = to_hex(ByteView(mac + 3, 3), {}, false);
    d.environment = TAPWAVE_ENV;
    d.version = TAPWAVE_VERSION;
    return d;
}

std::string Device::setup_name() const {
    std::string suffix = id.substr(id.size() - 4);
    for (char& c : suffix) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return "Tapwave-" + suffix;
}

std::string Device::model() const {
    return chip == Chip::Esp32S3 ? "Tapwave (ESP32-S3)" : "Tapwave (ESP32)";
}

}  // namespace tapwave::app
