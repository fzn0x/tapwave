// PN532 over SPI. Set the module's switches to SPI (SEL0 off, SEL1 on). The PN532 shifts bits
// least significant first.
#include <Arduino.h>

#include "drivers/buses.hpp"
#include "drivers/nfc/common/pn532.hpp"
#include "drivers/nfc/common/pn532_frame.hpp"
#include "drivers/nfc/common/registry.hpp"
#include "drivers/nfc/common/type2_tag_reader.hpp"

namespace tapwave::nfc {

namespace {

constexpr std::uint8_t kDataWrite = 0x01;
constexpr std::uint8_t kStatusRead = 0x02;
constexpr std::uint8_t kDataRead = 0x03;
constexpr std::uint8_t kReady = 0x01;

const SPISettings kSettings(1000000, LSBFIRST, SPI_MODE0);

class Pn532SpiTransport final : public Pn532Transport {
public:
    Pn532SpiTransport(SPIClass& spi, int cs_pin, int irq_pin, int reset_pin)
        : spi_(spi), cs_(cs_pin), irq_pin_(irq_pin), reset_pin_(reset_pin) {
        pinMode(cs_, OUTPUT);
        digitalWrite(cs_, HIGH);
        if (irq_pin_ >= 0) {
            pinMode(irq_pin_, INPUT_PULLUP);
        }
        if (reset_pin_ >= 0) {
            pinMode(reset_pin_, OUTPUT);
            digitalWrite(reset_pin_, HIGH);
        }
    }

    std::string_view bus() const override { return "spi"; }

    void wake() override {
        if (reset_pin_ >= 0) {
            digitalWrite(reset_pin_, LOW);
            delay(10);
            digitalWrite(reset_pin_, HIGH);
            delay(50);
        }
        // A falling chip select wakes the PN532 from power-down.
        digitalWrite(cs_, LOW);
        delay(2);
        digitalWrite(cs_, HIGH);
        delay(5);
    }

    std::expected<void, Error> write(ByteView frame) override {
        begin();
        delay(1);
        spi_.transfer(kDataWrite);
        for (std::uint8_t b : frame) {
            spi_.transfer(b);
        }
        end();
        return {};
    }

    std::expected<Bytes, Error> read(std::uint32_t timeout_ms, std::size_t max_length) override {
        const std::uint32_t start = millis();
        while (!ready()) {
            if (millis() - start > timeout_ms) {
                return std::unexpected(Error::Timeout);
            }
            delay(1);
        }
        begin();
        spi_.transfer(kDataRead);
        Bytes data;
        data.reserve(max_length);
        // Read the header, then exactly the rest of the frame.
        while (data.size() < max_length) {
            data.push_back(spi_.transfer(0x00));
            if (data.size() >= 5) {
                if (const auto length = pn532::frame_length(data); length) {
                    while (data.size() < *length && data.size() < max_length) {
                        data.push_back(spi_.transfer(0x00));
                    }
                    break;
                }
            }
        }
        end();
        return data;
    }

    void sleep_ms(std::uint32_t ms) override { delay(ms); }

private:
    bool ready() {
        if (irq_pin_ >= 0) {
            return digitalRead(irq_pin_) == LOW;
        }
        begin();
        spi_.transfer(kStatusRead);
        const std::uint8_t status = spi_.transfer(0x00);
        end();
        return (status & kReady) != 0;
    }

    void begin() {
        spi_.beginTransaction(kSettings);
        digitalWrite(cs_, LOW);
    }

    void end() {
        digitalWrite(cs_, HIGH);
        spi_.endTransaction();
    }

    SPIClass& spi_;
    int cs_;
    int irq_pin_;
    int reset_pin_;
};

ProbeResult probe(const ProbeContext& context) {
    const auto& pins = context.pins;
    if (pins.nfc_cs < 0) {
        return std::unexpected(std::string("nfc_cs is not set"));
    }
    SPIClass* spi = buses::spi(pins.spi_sck, pins.spi_miso, pins.spi_mosi);
    if (!spi) {
        return std::unexpected(std::string("spi_sck, spi_miso and spi_mosi are not set"));
    }
    auto chip = std::make_unique<Pn532>(
        std::make_unique<Pn532SpiTransport>(*spi, pins.nfc_cs, pins.nfc_irq, pins.nfc_reset),
        "pn532-spi");
    if (const auto ready = chip->begin(); !ready) {
        return std::unexpected("no PN532 answered on SPI: " + std::string(describe(ready.error())));
    }
    return std::make_unique<Type2TagReader>(std::move(chip));
}

}  // namespace

// After the RC522: an RC522 probe only reads a register, which a PN532 ignores.
TAPWAVE_REGISTER_NFC_DRIVER("pn532-spi", 30, "PN532 on SPI", probe);

}  // namespace tapwave::nfc
