# Flashing

A Tapwave box needs two images: the **firmware** and the **web UI** (a LittleFS filesystem image).
The first flash goes over USB. After that, updates install from the web UI or Home Assistant.

## Pick the environment

| Your board | Environment |
| --- | --- |
| ESP32 with 4 MB flash (most WROOM and WROVER boards) | `esp32-4mb` |
| ESP32 with 8 MB flash | `esp32-8mb` |
| ESP32 with 16 MB flash | `esp32-16mb` |
| ESP32-S3 with 16 MB flash (N16R8) | `esp32s3` |

esptool prints the flash size when it connects (`Detected flash size: 4MB`); `esptool flash-id`
shows it without writing anything. A 4 MB build works on bigger flash too,
it just leaves space unused.

## With PlatformIO

```sh
pio run -e esp32-4mb -t upload       # firmware
pio run -e esp32-4mb -t uploadfs     # web UI
pio device monitor -e esp32-4mb      # serial console, 115200 baud
```

PlatformIO downloads the right toolchain on the first build. To start completely fresh (this
erases Wi-Fi and every other setting), run `pio run -e esp32-4mb -t erase` first.

If the upload cannot connect, hold the board's BOOT button while it starts uploading.

## With a prebuilt image

Every CI run on `main` keeps the images for each environment for 14 days: open the
[Actions tab](https://github.com/fzn0x/tapwave/actions/workflows/ci.yml), pick a run and download
`tapwave-<environment>` (you need to be signed in to GitHub). Each archive holds:

| File | What it is |
| --- | --- |
| `firmware.factory.bin` | Bootloader, partition table and firmware in one image, written at `0x0` |
| `firmware.bin` | The firmware alone, for an update through the web UI |
| `littlefs.bin` | The web UI, written at the filesystem offset below |

Flash both with [esptool](https://docs.espressif.com/projects/esptool/) (`pip install esptool`):

```sh
esptool --chip esp32 write-flash 0x0 firmware.factory.bin 0x3D0000 littlefs.bin
```

| Environment | `--chip` | Filesystem offset |
| --- | --- | --- |
| `esp32-4mb` | `esp32` | `0x3D0000` |
| `esp32-8mb` | `esp32` | `0x670000` |
| `esp32-16mb` | `esp32` | `0x810000` |
| `esp32s3` | `esp32s3` | `0x810000` |

The offsets come from the partition tables in
[`partitions/`](https://github.com/fzn0x/tapwave/tree/main/partitions).

## If the web UI is missing

When the firmware is flashed without the web UI, or the filesystem is damaged, the box serves a
small recovery page instead. It shows the version and lets you upload `littlefs.bin`. The box keeps
working as a tag reader in the meantime, and Home Assistant still gets every scan.

## Updates after the first flash

- **Web UI:** Update, then "Check for updates" and "Install", or upload `firmware.bin` and
  `littlefs.bin` by hand.
- **Home Assistant:** the box's firmware update entity shows new releases and installs them.
- **API:** `POST /api/update/install`, see [REST API](REST-API.md).

A new firmware has to prove itself: if it crashes or never connects to Wi-Fi in its first start,
the box goes back to the previous version on the next reset.
