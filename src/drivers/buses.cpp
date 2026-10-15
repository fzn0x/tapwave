#include "drivers/buses.hpp"

#include <esp_log.h>

#include <cstdint>

namespace tapwave::buses {

namespace {

constexpr const char* kTag = "buses";
constexpr std::uint32_t kI2cFrequency = 100000;  // the PN532 is reliable at 100 kHz

struct I2cState {
    bool started = false;
    int sda = -1;
    int scl = -1;
};

struct SpiState {
    bool started = false;
    int sck = -1;
    int miso = -1;
    int mosi = -1;
};

I2cState i2c_state;
SpiState spi_state;

}  // namespace

TwoWire* i2c(int sda, int scl) {
    if (sda < 0 || scl < 0) {
        return nullptr;
    }
    if (i2c_state.started) {
        if (i2c_state.sda != sda || i2c_state.scl != scl) {
            ESP_LOGW(kTag, "I2C already runs on SDA %d SCL %d", i2c_state.sda, i2c_state.scl);
            return nullptr;
        }
        return &Wire;
    }
    if (!Wire.begin(sda, scl, kI2cFrequency)) {
        ESP_LOGE(kTag, "I2C did not start on SDA %d SCL %d", sda, scl);
        return nullptr;
    }
    Wire.setTimeOut(50);
    i2c_state = {true, sda, scl};
    return &Wire;
}

SPIClass* spi(int sck, int miso, int mosi) {
    if (sck < 0 || miso < 0 || mosi < 0) {
        return nullptr;
    }
    if (spi_state.started) {
        if (spi_state.sck != sck || spi_state.miso != miso || spi_state.mosi != mosi) {
            ESP_LOGW(kTag, "SPI already runs on SCK %d MISO %d MOSI %d", spi_state.sck,
                     spi_state.miso, spi_state.mosi);
            return nullptr;
        }
        return &SPI;
    }
    SPI.begin(sck, miso, mosi, -1);
    spi_state = {true, sck, miso, mosi};
    return &SPI;
}

}  // namespace tapwave::buses
