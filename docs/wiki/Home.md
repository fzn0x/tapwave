# Tapwave wiki

Tapwave is open firmware for ESP32 NFC readers. Today it is a Home Assistant tag reader with a web
UI for setup; writing cards, a music box and Bluetooth audio arrive in the next milestones (see
[the milestones](https://github.com/fzn0x/tapwave/blob/main/ARCHITECTURE.md#2-milestones)).

These pages are kept in the repository under
[`docs/wiki/`](https://github.com/fzn0x/tapwave/tree/main/docs/wiki) and copied to the GitHub wiki
on every change, so corrections are normal pull requests.

## Set up a box

1. [Getting started](Getting-Started.md): from a bare board to the first card, in four steps.
2. [Hardware](Hardware.md): boards, readers, cards and wiring.
3. [Flashing](Flashing.md): PlatformIO, prebuilt images and the recovery page.
4. [Home Assistant](Home-Assistant.md): entities, tag automations and moving from adonno/tagreader.

## Reference

- [MQTT](MQTT.md): topics, payloads and discovery.
- [REST API](REST-API.md): every endpoint with `curl` examples, and the WebSocket.
- [The tapwave:// scheme](URI-Scheme.md): how a card carries its own instruction.

## Help

- [Troubleshooting](Troubleshooting.md): no reader found, no setup network, Home Assistant does not
  see the box, and more.
- [FAQ](FAQ.md)
- Questions: [Discussions](https://github.com/fzn0x/tapwave/discussions).
  Bugs: [issues](https://github.com/fzn0x/tapwave/issues/new/choose).

## Contribute

- [Contributing guide](https://github.com/fzn0x/tapwave/blob/main/CONTRIBUTING.md)
- [Adding a reader](Adding-a-Reader.md): a new NFC reader is one new file.
- [Architecture](https://github.com/fzn0x/tapwave/blob/main/ARCHITECTURE.md): how the firmware is
  put together.
