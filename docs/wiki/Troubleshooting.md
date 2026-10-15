# Troubleshooting

Start with the Home page of the box: every row that needs attention says what is wrong and what to
do. The serial console (`pio device monitor`, 115200 baud) shows the same and more.

## "No reader found"

Open Settings, Card reader. "What the box tried" lists every reader type and what it answered.

- **Check the wiring** against [Hardware](Hardware.md), and that the pins in Settings, Wiring
  match the pins you used.
- **PN532: check the two switches.** They choose I2C or SPI and are easy to leave in the wrong
  position. Change them only with the power off.
- **RC522: check the power and RST.** It must run from 3.3 V, and its RST pin must be connected to
  3.3 V or to a pin set as "Reader reset".
- **Try "Look for the reader again"** after fixing a wire. The box also looks on its own every 30
  seconds.
- **Long or thin wires** can make I2C unreliable. Keep them short, or use SPI.

## A card is not read, or only its ID

- **"Plain card":** the card has no NDEF data, or it is a type the box only reads the UID from
  (phones, bank cards, DESFire). It still works in Home Assistant by its UID.
- **"The data on this card could not be read":** the box got the UID but not the data. Hold the
  card still and flat over the reader, and away from metal. A MIFARE Classic card with its own keys
  can only be used by its UID.
- **Phones** show a different random UID on each tap and cannot be used as cards.

## I cannot find the setup network

- The setup network `Tapwave-XXXX` only exists while the box has no working Wi-Fi. If the box is
  already connected, open it at its address instead.
- If you set a password for the setup network, enter it when you join.
- The LED, if you have one, blinks amber while the box waits for Wi-Fi details.
- Use Improv instead: [improv-wifi.com](https://www.improv-wifi.com) over USB, or Home Assistant's
  Improv via BLE.

## The setup page does not open

After joining `Tapwave-XXXX`, open `http://192.168.4.1` in the browser. Some phones switch back to
mobile data on a network without internet; turn mobile data off during setup.

## I cannot reach the box at tapwave-xxxxxx.local

- Some Android phones and some networks do not resolve `.local` names. Use the box's IP address,
  shown at the end of setup, in your router's device list, or on the serial console.
- Phones and computers on a guest network often cannot see devices on the main network.

## The box keeps losing Wi-Fi

- The ESP32 only uses 2.4 GHz. Make sure your router offers a 2.4 GHz network.
- Check the signal on the Home page. If it says "weak signal", move the box or the router.
- After a minute offline the box opens its setup network next to the normal one, so you can fix
  the Wi-Fi details from a phone.

## Home Assistant does not see the box

The Home Assistant row on the Home page says what the broker answered:

| The box says | Do this |
| --- | --- |
| The broker cannot be reached | Check the broker address and port. `homeassistant.local` does not work on every network; try the IP address. |
| The broker turned down the user name or password | Check the user in Settings, Home Assistant. With the Mosquitto add-on, any Home Assistant user works. |
| The broker refused the connection | The broker rejected the client. Check its logs, for example for an access control list. |
| The encrypted connection failed | The broker's certificate must come from a public authority. Turn TLS off to test. |

If the box says "Connected" but nothing appears in Home Assistant, check that "Add this box to
Home Assistant automatically" is on and that the discovery prefix matches Home Assistant's
(`homeassistant` by default).

## An update failed

The Update page shows why. The box keeps running the old version when a download or a check fails.
If a new version cannot get online after it starts, the box goes back to the previous version on
the next reset. You can always flash over USB again; see [Flashing](Flashing.md).

## The page says "Tapwave recovery"

The web UI is missing or damaged, but the firmware is fine. Upload `littlefs.bin` for your board on
that page, or flash it with `pio run -e <environment> -t uploadfs`.

## I forgot the admin password

There is no reset without the password yet. Erase the board over USB and flash it again: this
removes every setting, including Wi-Fi.

```sh
pio run -e esp32-4mb -t erase
pio run -e esp32-4mb -t upload
pio run -e esp32-4mb -t uploadfs
```

## Still stuck

Ask in [Discussions](https://github.com/fzn0x/tapwave/discussions), or
[open an issue](https://github.com/fzn0x/tapwave/issues/new/choose) with the firmware version, the
board, the reader and the serial log.
