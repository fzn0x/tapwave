# Home Assistant

Tapwave talks to Home Assistant over MQTT, with MQTT discovery: once the box is connected to the
broker, Home Assistant creates the device and its entities by itself. (The ESPHome native API is a
possible later addition; MQTT is what is implemented.)

## Connect the box

1. In Home Assistant, install an MQTT broker if you do not have one: Settings, Add-ons, **Mosquitto
   broker**. Then add the **MQTT** integration.
2. Create a Home Assistant user for the box (Settings, People, Users), or use any user that can
   log in to the broker.
3. In the box's web UI, open Settings, Home Assistant. Turn on "Connect to Home Assistant", enter
   the broker address (for example `homeassistant.local`), the port (1883) and the user, then save.

The Home page shows "Connected" once the box is in. In Home Assistant the box appears under
Settings, Devices and services, MQTT, with the name you gave it.

If the connection fails, the Home page says why: the broker cannot be reached, it turned down the
user name or password, it refused the connection, or the encrypted connection failed.

## Entities

| Entity | Type | What it does |
| --- | --- | --- |
| Tag scanner | tag scanner | Fires `tag_scanned` for every card. Cards appear under Settings, Tags. |
| Last tag | sensor | The last card's tag ID. Its attributes hold the whole scan (UID, card type, URI, text, records). |
| Tag present | binary sensor | On while a card is on the reader. |
| Tag | event | Event types `scanned`, `removed` and `music`. |
| Buzzer, Status LED | switch | Turn the beeps and the light on or off. |
| Restart, Identify | button | Restart the box, or blink and beep so you can find it. |
| Firmware | update | Shows new releases and installs them. |
| Wi-Fi signal, Reader, IP address, Uptime | sensor | Diagnostics. |

## Tag automations

Each card gets a tag ID. For a card written with the Home Assistant app it is the ID on the card;
for any other card it is the card's UID, such as `04-A2-1B-3C-4D-5E-80`. The first tap of a new
card adds it under Settings, Tags, where you can name it and create an automation.

```yaml
alias: Bedtime lights card
triggers:
  - trigger: tag
    tag_id: 04-A2-1B-3C-4D-5E-80
actions:
  - action: light.turn_on
    target:
      entity_id: light.kids_room
    data:
      brightness_pct: 20
```

To react only to cards on one box when you have several, add `device_id` to the trigger.

## Music tags

A card that carries a music link (Spotify, Apple Music, or a Music Assistant URI such as
`spotify://playlist/37i9dQZF1DX4sWSpwq3LiO`) fires the Tag event with type `music` and the link in
`music_uri`. This automation plays it with Music Assistant:

```yaml
alias: Play music cards in the kids' room
triggers:
  - trigger: state
    entity_id: event.tapwave_tag
conditions:
  - condition: state
    entity_id: event.tapwave_tag
    attribute: event_type
    state: music
actions:
  - action: music_assistant.play_media
    target:
      entity_id: media_player.kids_room
    data:
      media_id: "{{ trigger.to_state.attributes.music_uri }}"
```

`event.tapwave_tag` depends on the name of your box; look it up on the device page. Music cards
also fire `tag_scanned` with their UID, so a tag automation works as well.

## Confirm a scan with a beep or the light

An automation can tell the person at the box whether it worked by publishing `ok` or `error`:

```yaml
action: mqtt.publish
data:
  topic: tapwave/a1b2c3/cmd/feedback
  payload: ok
```

`a1b2c3` is the box ID: the last six characters of the MAC address shown under "About this box" on
the Home page, without the colons. It is also the end of the default network name
(`tapwave-a1b2c3.local`). `cmd/rtttl` plays a melody in RTTTL format. See [MQTT](MQTT.md) for every topic.

## Moving from adonno/tagreader

- UIDs use the same format and Home Assistant tag URLs report the same ID, so existing tags and
  their automations keep working.
- `esphome.music_tag` becomes the Tag event with type `music` (above).
- `esphome.tag_removed` becomes the Tag event with type `removed`.
- `rfidreader_tag_ok`, `rfidreader_tag_ko` and `play_rtttl` become MQTT publishes to
  `cmd/feedback` and `cmd/rtttl`.
- Writing cards from Home Assistant (tagreader's write service) arrives in milestone 2.
