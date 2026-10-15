# Adding a reader

A new NFC reader is one new file in
[`src/drivers/nfc/`](https://github.com/fzn0x/tapwave/tree/main/src/drivers/nfc). It registers
itself, and auto-detection, the web UI's reader list and the `nfc.driver` setting pick it up with
no other changes.

## 1. Pick the interface

[`readers.hpp`](https://github.com/fzn0x/tapwave/blob/main/src/drivers/nfc/common/readers.hpp)
has two:

- **`Iso14443aReader`** for frontends that send raw ISO 14443-A commands and leave tag memory to
  the host: PN532, RC522, PN5180 and most cheap modules. You implement `select()`,
  `mifare_read()`, `ultralight_write()`, `classic_auth()` and `transceive()`.
  `Type2TagReader` then does the rest: identifying NTAG and Ultralight cards, walking the
  capability container and TLVs, reading MIFARE Classic with the NFC Forum keys, and writing in
  milestone 2. You get all of that for free.
- **`TagReader`** for readers that handle NDEF themselves (an NCI controller such as the PN7150, or
  a serial module that reports NDEF). You implement `detect()`, `read()` and `write_ndef()`
  directly.

Most readers are the first kind.

## 2. Split the protocol from the bus

Put everything that is pure protocol (frame encoding, register sequences, answer parsing) in
`src/drivers/nfc/common/`, behind a small bus interface. That code must not include Arduino or
ESP-IDF headers, which lets it run in the host tests. The RC522 is a good model:

- [`common/rc522.hpp`](https://github.com/fzn0x/tapwave/blob/main/src/drivers/nfc/common/rc522.hpp)
  declares `Rc522Bus` (read and write a register, read and write the FIFO, reset, time) and the
  `Rc522` class that implements `Iso14443aReader` on top of it.
- [`rc522_spi.cpp`](https://github.com/fzn0x/tapwave/blob/main/src/drivers/nfc/rc522_spi.cpp) is
  the only file that touches the hardware: it implements `Rc522Bus` with Arduino's `SPI`, and
  registers the driver.

## 3. Write the probe and register it

The probe gets the configured pins, tries to talk to the chip, and returns either a working reader
or a short reason it found none. The reason is shown in the web UI under "What the box tried", so
make it useful to someone checking their wiring.

```cpp
namespace tapwave::nfc {
namespace {

ProbeResult probe(const ProbeContext& context) {
    const auto& pins = context.pins;
    if (pins.nfc_cs < 0) {
        return std::unexpected(std::string("nfc_cs is not set"));
    }
    SPIClass* spi = buses::spi(pins.spi_sck, pins.spi_miso, pins.spi_mosi);
    if (!spi) {
        return std::unexpected(std::string("spi_sck, spi_miso and spi_mosi are not set"));
    }
    auto chip = std::make_unique<MyChip>(std::make_unique<MyChipSpiBus>(*spi, pins.nfc_cs));
    if (!chip->begin()) {
        return std::unexpected(std::string("no MyChip answered on SPI"));
    }
    return std::make_unique<Type2TagReader>(std::move(chip));
}

}  // namespace

TAPWAVE_REGISTER_NFC_DRIVER("mychip-spi", 40, "MyChip on SPI", probe);

}  // namespace tapwave::nfc
```

The arguments are the driver ID (also the value of the `nfc.driver` setting that pins this
driver), the priority (lower is probed first; the built-in drivers use 10, 20 and 30), a
description for the web UI, and the probe.

Probing must be harmless for other chips that might be on the same pins, and it must time out
quickly: every bounded wait counts against the boot time when the reader is missing. Use the
shared buses from `drivers/buses.hpp` instead of creating your own `Wire` or `SPIClass`.

## 4. Test it on the host

Add `test/test_mychip/test_main.cpp` with a fake bus that simulates the chip, the way
[`test/test_rc522`](https://github.com/fzn0x/tapwave/tree/main/test/test_rc522) simulates RC522
registers. The simulated cards in
[`test/support/fake_tags.hpp`](https://github.com/fzn0x/tapwave/blob/main/test/support/fake_tags.hpp)
(NTAG, Ultralight and MIFARE Classic with real memory layouts) answer the commands your chip
forwards, so you can test a whole read without hardware.

```sh
pio test -e native -f test_mychip
```

## 5. Document it

- Add the reader to the tables in [Hardware](Hardware.md) and in
  [ARCHITECTURE.md section 6](https://github.com/fzn0x/tapwave/blob/main/ARCHITECTURE.md#6-nfc-drivers).
- Say in the pull request which modules you tested with. A [hardware
  report](https://github.com/fzn0x/tapwave/issues/new?template=hardware_report.yml) from someone
  else with the same module is very welcome.
