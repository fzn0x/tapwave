// PN532 over I2C (address 0x24). Set the module's switches to I2C (SEL0 on, SEL1 off).
#include <Arduino.h>

#include <algorithm>

#include "drivers/buses.hpp"
#include "drivers/nfc/common/pn532.hpp"
#include "drivers/nfc/common/pn532_frame.hpp"
#include "drivers/nfc/common/registry.hpp"
#include "drivers/nfc/common/type2_tag_reader.hpp"

namespace tapwave::nfc {

namespace {

constexpr std::uint8_t kAddress = 0x24;
constexpr std::uint8_t kReady = 0x01;

class Pn532I2cTransport final : public Pn532Transport {
public:
    Pn532I2cTransport(TwoWire& wire, int irq_pin, int reset_pin)
        : wire_(wire), irq_pin_(irq_pin), reset_pin_(reset_pin) {
        if (irq_pin_ >= 0) {
            pinMode(irq_pin_, INPUT_PULLUP);
        }
        if (reset_pin_ >= 0) {
            pinMode(reset_pin_, OUTPUT);
            digitalWrite(reset_pin_, HIGH);
        }
    }

    std::string_view bus() const override { return "i2c"; }

    void wake() override {
        if (reset_pin_ >= 0) {
            digitalWrite(reset_pin_, LOW);
            delay(10);
            digitalWrite(reset_pin_, HIGH);
            delay(50);
        }
        delay(5);
    }

    std::expected<void, Error> write(ByteView frame) override {
        wire_.beginTransmission(kAddress);
        wire_.write(frame.data(), frame.size());
        if (wire_.endTransmission() != 0) {
            return std::unexpected(Error::Transport);  // nothing acknowledged the address
        }
        return {};
    }

    std::expected<Bytes, Error> read(std::uint32_t timeout_ms, std::size_t max_length) override {
        const std::uint32_t start = millis();
        // Every I2C read starts with a status byte; 0x01 means a frame follows.
        while (true) {
            if (irq_pin_ >= 0 ? digitalRead(irq_pin_) == LOW : status() == kReady) {
                break;
            }
            if (millis() - start > timeout_ms) {
                return std::unexpected(Error::Timeout);
            }
            delay(1);
        }
        const std::size_t want = 1 + max_length;
        if (wire_.requestFrom(kAddress, want) != want) {
            return std::unexpected(Error::Transport);
        }
        Bytes data(want);
        wire_.readBytes(data.data(), want);
        if (data[0] != kReady) {
            return std::unexpected(Error::Protocol);
        }
        data.erase(data.begin());
        if (const auto length = pn532::frame_length(data); length && *length <= data.size()) {
            data.resize(*length);
        }
        return data;
    }

    void sleep_ms(std::uint32_t ms) override { delay(ms); }

private:
    std::uint8_t status() {
        if (wire_.requestFrom(kAddress, static_cast<std::size_t>(1)) != 1) {
            return 0;
        }
        return static_cast<std::uint8_t>(wire_.read());
    }

    TwoWire& wire_;
    int irq_pin_;
    int reset_pin_;
};

ProbeResult probe(const ProbeContext& context) {
    const auto& pins = context.pins;
    if (pins.i2c_sda < 0 || pins.i2c_scl < 0) {
        return std::unexpected(std::string("i2c_sda and i2c_scl are not set"));
    }
    TwoWire* wire = buses::i2c(pins.i2c_sda, pins.i2c_scl);
    if (!wire) {
        return std::unexpected(std::string("the I2C bus did not start on those pins"));
    }
    wire->beginTransmission(kAddress);
    if (wire->endTransmission() != 0) {
        return std::unexpected(std::string("nothing answered at I2C address 0x24"));
    }
    auto chip = std::make_unique<Pn532>(
        std::make_unique<Pn532I2cTransport>(*wire, pins.nfc_irq, pins.nfc_reset), "pn532-i2c");
    if (const auto ready = chip->begin(); !ready) {
        return std::unexpected("a device at 0x24 did not answer like a PN532: " +
                               std::string(describe(ready.error())));
    }
    return std::make_unique<Type2TagReader>(std::move(chip));
}

}  // namespace

TAPWAVE_REGISTER_NFC_DRIVER("pn532-i2c", 10, "PN532 on I2C", probe);

}  // namespace tapwave::nfc
