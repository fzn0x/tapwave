# FAQ

### Does it need Home Assistant?

No. The box works on its own: the web UI shows every card, and the REST API and MQTT work with any
home server or script. Home Assistant just gets the most out of it today, because playing music on
the box itself arrives in milestone 3.

### Does it need the internet?

No. Setup, the web UI and Home Assistant all work on the local network. The box contacts two
outside addresses, both of which you can change or turn off in Settings, Updates and time:

- the time server (`pool.ntp.org`), so scans carry the real time;
- the release list on GitHub Pages, twice a day, to tell you about updates. It sends nothing about
  you or your cards.

### Why MQTT and not the ESPHome API?

MQTT works with every broker and every home server, Home Assistant creates the entities through
discovery, and the ESP-IDF client is built into the framework. The ESPHome native API is a possible
later addition.

### Which cards should I buy?

NTAG215 stickers or cards. They are cheap, every phone can write them, and they have room for a
Home Assistant tag and a long `tapwave://` link. NTAG213 is fine for Home Assistant tags alone.

### Can I use my old TonUINO or ESPuino cards?

Yes, by their UID: tap them and use the ID in Home Assistant. A card table that maps UIDs to music
on the box arrives in milestone 2.

### My board has 4 MB of flash. Will it get every feature?

It keeps updates over the air, which need two copies of the firmware. The tag reader fits with
room to spare. When the music player and Bluetooth audio arrive, the 4 MB build may have to leave
Bluetooth audio out; that will be measured and announced, not guessed.

### Why is it MIT licensed when ESPuino and TonUINO are GPL?

So it can be used and changed in any project, including commercial ones. That is also why the
music player (milestone 3) will use decoders with permissive licenses instead of the popular GPL
audio libraries.

### Is the web UI available in my language?

English only for now, but every string is in one table, and adding a language is a small pull
request. See [the contributing guide](https://github.com/fzn0x/tapwave/blob/main/CONTRIBUTING.md#translations).

### Is it safe to put on my network?

It is meant for a home network, like most smart home devices. Set an admin password so only you
can change settings, and do not expose it to the internet. The
[security policy](https://github.com/fzn0x/tapwave/blob/main/SECURITY.md) lists what it protects
against and what it does not.
