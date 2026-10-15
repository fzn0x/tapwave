# Hardware

## Boards

| Board | Flash | Environment | Notes |
| --- | --- | --- | --- |
| ESP32-WROVER (with PSRAM) | 4, 8 or 16 MB | `esp32-4mb`, `esp32-8mb`, `esp32-16mb` | The main target. The music box (milestone 3) will need the PSRAM. |
| ESP32-WROOM (no PSRAM) | 4 MB or more | same as above | Works as a tag reader. |
| ESP32-S3 (for example N16R8) | 16 MB | `esp32s3` | Builds today; becomes a tested target in milestone 4. No Bluetooth Classic, so no Bluetooth speakers. |

Pick the environment by flash size, not by module name. If you are not sure, `esp32-4mb` works on
every ESP32 board with at least 4 MB.

## Readers

| Reader | Bus | Notes |
| --- | --- | --- |
| PN532 (red or blue modules) | I2C or SPI | Set the two switches on the module for the bus you use. Runs from 3.3 V. |
| RC522 (MFRC522 and FM17522 clones) | SPI | **3.3 V only.** 5 V damages it. Cheap and common, shorter range than the PN532. |
| PN5180 | SPI | Milestone 4. |

The box finds the reader on its own at boot, in this order: PN532 on I2C, RC522 on SPI, PN532 on
SPI. If nothing answers it keeps looking every 30 seconds, so you can fix a loose wire without a
restart. Settings, Card reader shows what it tried.

## Cards

| Card | Read | Write (milestone 2) | Notes |
| --- | --- | --- | --- |
| NTAG213 | Yes | Yes | 144 bytes, enough for a Home Assistant tag or a short `tapwave://` link |
| NTAG215 | Yes | Yes | 496 bytes. A good default, also sold as small coins and stickers |
| NTAG216 | Yes | Yes | 872 bytes |
| MIFARE Ultralight, EV1, C | Yes | Yes | 48 bytes or more |
| MIFARE Classic 1K, 4K | Yes, NDEF with the public NFC Forum keys | No | Many hotel and transit cards. The UID always works |
| Phones, bank cards, DESFire | UID only | No | Phones and bank cards show a random UID on each tap |

Any card works as a UID-only card in Home Assistant. Cards with NDEF data can also carry a Home
Assistant tag ID or a `tapwave://` instruction.

## Wiring

Default pins are below. Every pin can be changed in the web UI under Settings, Wiring (under
Advanced), with no reflash. `-` means not connected.

### PN532 on I2C

Set the module's switches to I2C (on the common red module: switch 1 ON, switch 2 OFF).

| PN532 | ESP32 | ESP32-S3 |
| --- | --- | --- |
| VCC | 3.3 V | 3.3 V |
| GND | GND | GND |
| SDA | GPIO 21 | GPIO 8 |
| SCL | GPIO 22 | GPIO 9 |
| RSTO, IRQ | - (optional) | - (optional) |

### PN532 on SPI

Set the switches to SPI (on the red module: switch 1 OFF, switch 2 ON).

| PN532 | ESP32 | ESP32-S3 |
| --- | --- | --- |
| VCC | 3.3 V | 3.3 V |
| GND | GND | GND |
| SCK | GPIO 18 | GPIO 12 |
| MISO | GPIO 19 | GPIO 13 |
| MOSI | GPIO 23 | GPIO 11 |
| SS | GPIO 5 | GPIO 10 |

### RC522 on SPI

| RC522 | ESP32 | ESP32-S3 |
| --- | --- | --- |
| 3.3V | 3.3 V | 3.3 V |
| GND | GND | GND |
| SCK | GPIO 18 | GPIO 12 |
| MISO | GPIO 19 | GPIO 13 |
| MOSI | GPIO 23 | GPIO 11 |
| SDA (this is the chip select) | GPIO 5 | GPIO 10 |
| RST | 3.3 V, or a GPIO set as "Reader reset" | same |
| IRQ | - | - |

The RC522 stays powered down while RST is low, so RST must go to 3.3 V or to a pin the box controls.

### Status LED (optional)

A WS2812 (NeoPixel) LED or strip. Connect DIN to any free output pin and set it as "Status LED
data". Power a single LED from 3.3 V; for a strip use 5 V with a common ground.

| Light | Meaning |
| --- | --- |
| Blue, breathing | Starting |
| Amber, slow blink | Waiting for Wi-Fi details (setup) |
| Blue, quick blink | Connecting to Wi-Fi |
| Dim green | Ready |
| Red, slow blink | No reader found |
| Purple, breathing | Installing an update |

It also flashes when a card is read.

### Buzzer (optional)

A **passive** piezo buzzer between a free output pin and GND, set as "Buzzer". The box plays a
short tune for each card. A loud buzzer is better driven through a small transistor.

## Pins to avoid

The web UI checks pins before saving, but it helps to know why:

| Pins | Why |
| --- | --- |
| GPIO 6 to 11 | Connected to the flash chip. Rejected. |
| GPIO 16, 17 on WROVER | Used by the PSRAM. Rejected. |
| GPIO 34 to 39 | Input only. Rejected as outputs, fine for inputs such as the battery voltage. |
| GPIO 0, 2, 5, 12, 15 | Strapping pins that decide how the chip boots. Allowed with a warning. GPIO 5 is the default reader select and works on most boards; if your board fails to boot with the reader connected, move it. |

Two functions cannot share a pin, except signals on the same bus (several SPI devices share SCK,
MISO and MOSI).
