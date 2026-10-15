# Getting started

From a bare board to "tap a card and it shows up" in four steps. You need an ESP32 board, a PN532
or RC522 reader, a USB cable and a few NFC cards (NTAG215 stickers or cards are a good choice).

## 1. Wire the reader

Connect the reader to the default pins in [Hardware](Hardware.md). The short version for a PN532
in I2C mode on an ESP32: VCC to 3.3 V, GND to GND, SDA to GPIO 21, SCL to GPIO 22, and set the
module's switches to I2C.

Other pins work too. You choose them in the web UI later, so there is no need to edit any code.

## 2. Flash the firmware

With [PlatformIO](https://platformio.org) installed:

```sh
git clone https://github.com/fzn0x/tapwave.git
cd tapwave
pio run -e esp32-4mb -t upload      # the firmware
pio run -e esp32-4mb -t uploadfs    # the web UI
```

Use `esp32-8mb`, `esp32-16mb` or `esp32s3` if your board has more flash or is an S3.
[Flashing](Flashing.md) explains how to pick, and how to flash a prebuilt image with esptool
instead.

## 3. Connect it to Wi-Fi

A new box has no Wi-Fi settings, so it offers three ways to get them. Use whichever is easiest.

- **From a phone.** Join the Wi-Fi network `Tapwave-XXXX`. A setup page opens on its own (if it
  does not, open `http://192.168.4.1`). Pick your network, enter the password and wait. The last
  step shows the box's new address.
- **From Home Assistant.** With Bluetooth in Home Assistant, the **Improv via BLE** integration
  finds the box and offers to set it up.
- **Over USB.** Leave the box plugged in and open [improv-wifi.com](https://www.improv-wifi.com) in
  Chrome or Edge, then choose "Connect device to Wi-Fi".

`XXXX` is the end of the board's MAC address. It is also printed on the serial console
(`pio device monitor`).

If the box later loses its Wi-Fi for more than a minute, the setup network opens again so you can
fix the settings. It closes once the box is back online.

## 4. Open the box and tap a card

Open `http://tapwave-xxxxxx.local` (the setup page showed the exact address; some Android phones do
not resolve `.local` names, so use the IP address it showed instead). The Home page shows the reader,
Wi-Fi and update status, and waits for a card. Tap one: its name, ID and what it will do appear
straight away.

## Next steps

- **Home Assistant:** Settings, Home Assistant. See [Home Assistant](Home-Assistant.md).
- **A password for the settings:** Settings, Admin password. Without one, anyone on your network can
  change the box's settings.
- **Names:** Settings, This box. The name shows in the web UI and in Home Assistant.
- **LED and buzzer:** connect them, then set their pins in Settings, Wiring (under Advanced).
