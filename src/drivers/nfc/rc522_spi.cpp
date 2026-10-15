// RC522 (MFRC522, FM17522) over SPI. Power it from 3.3 V only.
#include <Arduino.h>

#include "drivers/buses.hpp"
#include "drivers/nfc/common/rc522.hpp"
#include "drivers/nfc/common/registry.hpp"
#include "drivers/nfc/common/type2_tag_reader.hpp"

namespace tapwave::nfc {

namespace {

const SPISettings kSettings(4000000, MSBFIRST, SPI_MODE0);

constexpr std::uint8_t read_address(std::uint8_t reg) {
    return static_cast<std::uint8_t>(0x80 | (reg << 1));
}

constexpr std::uint8_t write_address(std::uint8_t reg) {
    return static_cast<std::uint8_t>((reg << 1) & 0x7E);
}

class Rc522SpiBus final : public Rc522Bus {
public:
    Rc522SpiBus(SPIClass& spi, int cs_pin, int reset_pin)
        : spi_(spi), cs_(cs_pin), reset_pin_(reset_pin) {
        pinMode(cs_, OUTPUT);
        digitalWrite(cs_, HIGH);
        if (reset_pin_ >= 0) {
            pinMode(reset_pin_, OUTPUT);
            digitalWrite(reset_pin_, HIGH);
        }
    }

    std::string_view bus() const override { return "spi"; }

    std::uint8_t read(std::uint8_t reg) override {
        begin();
        spi_.transfer(read_address(reg));
        const std::uint8_t value = spi_.transfer(0x00);
        end();
        return value;
    }

    void write(std::uint8_t reg, std::uint8_t value) override {
        begin();
        spi_.transfer(write_address(reg));
        spi_.transfer(value);
        end();
    }

    void read_fifo(std::span<std::uint8_t> out) override {
        if (out.empty()) {
            return;
        }
        const std::uint8_t address = read_address(rc522::kFifoDataReg);
        begin();
        spi_.transfer(address);
        for (std::size_t i = 0; i + 1 < out.size(); ++i) {
            out[i] = spi_.transfer(address);  // each byte sent asks for the next one
        }
        out[out.size() - 1] = spi_.transfer(0x00);
        end();
    }

    void write_fifo(ByteView data) override {
        begin();
        spi_.transfer(write_address(rc522::kFifoDataReg));
        for (std::uint8_t b : data) {
            spi_.transfer(b);
        }
        end();
    }

    void hard_reset() override {
        if (reset_pin_ < 0) {
            return;  // RST must then be tied to 3.3 V
        }
        digitalWrite(reset_pin_, LOW);
        delayMicroseconds(10);
        digitalWrite(reset_pin_, HIGH);
        delay(50);  // oscillator start-up
    }

    std::uint32_t now_ms() override { return millis(); }
    void sleep_ms(std::uint32_t ms) override { delay(ms); }

private:
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
    auto bus = std::make_unique<Rc522SpiBus>(*spi, pins.nfc_cs, pins.nfc_reset);
    auto chip = std::make_unique<Rc522>(std::move(bus), "rc522-spi");
    if (const auto ready = chip->begin(); !ready) {
        return std::unexpected(std::string("no RC522 answered on SPI"));
    }
    return std::make_unique<Type2TagReader>(std::move(chip));
}

}  // namespace

TAPWAVE_REGISTER_NFC_DRIVER("rc522-spi", 20, "RC522 on SPI", probe);

}  // namespace tapwave::nfc
