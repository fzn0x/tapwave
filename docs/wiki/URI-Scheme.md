# The tapwave:// scheme

A `tapwave://` link written on a card tells the box what to do, with no server and no setup: the
card is the instruction. The scheme is open, so other projects may read and write it.
[ARCHITECTURE.md section 9](https://github.com/fzn0x/tapwave/blob/main/ARCHITECTURE.md#9-the-tapwave-uri-scheme)
is the specification; this page is the short version.

**Today (milestone 1)** the box reads these links, shows what they mean in the web UI and reports
them to Home Assistant with the scan (`action` in the scan JSON). Acting on them arrives with
milestone 2 (Home Assistant playback and commands) and milestone 3 (playing on the box).

## Shape

```
tapwave://<action>/<target>?<option>=<value>&...
```

## Actions

| Link | Meaning | Options |
| --- | --- | --- |
| `tapwave://sd/<path>` | Play a file or folder from the SD card. A folder plays in name order. | `mode`, `track`, `volume` |
| `tapwave://stream/<url>` | Play an internet radio or audio stream. `.m3u` and `.pls` playlists work. | `volume` |
| `tapwave://ha/<media id>` | Ask Home Assistant to play something, usually through Music Assistant. | `player`, `enqueue`, `radio` |
| `tapwave://cmd/<command>` | Control playback: `play`, `pause`, `toggle`, `stop`, `next`, `previous`, `volume/40`, `volume/+10`, `sleep/20`, `sleep/off`, `shuffle/on`, `repeat/all` and more. | none |

- `mode`: `album` (in order, once; the default for folders), `single`, `audiobook` (remembers
  where you stopped, milestone 4), `shuffle` or `loop`.
- `track`: where to start in a folder, counting from 1.
- `volume`: 0 to 100, never above the kids-mode limit.
- `player`: a Home Assistant media player, such as `media_player.kids_room`.
- `enqueue`: `play`, `next`, `add` or `replace`. `radio`: `true` or `false`.

## Examples

```
tapwave://sd/Audiobooks/The%20Gruffalo?mode=audiobook
tapwave://sd/Music/Lullabies?mode=shuffle&volume=30
tapwave://stream/https://icecast.example.org/jazz.mp3
tapwave://ha/spotify://playlist/37i9dQZF1DX4sWSpwq3LiO?player=media_player.kids_room
tapwave://cmd/sleep/20
tapwave://cmd/volume/-10
```

Spaces and `%`, `?` and `#` in a path are written as `%20`, `%25`, `%3F` and `%23`. Slashes and
colons stay as they are, so a stream address needs no changes unless it has its own `?query`.

## Other links on cards

- `https://www.home-assistant.io/tag/<id>`: a Home Assistant tag. The box reports the ID.
- Spotify, Apple Music, Sonos and Music Assistant links: forwarded to Home Assistant as a `music`
  event.
- Any other `http://` or `https://` link that looks like audio plays on the box once the player
  arrives (milestone 3); everything else goes to Home Assistant.

## How long a link fits

| Card | Longest `tapwave://` link |
| --- | --- |
| MIFARE Ultralight | 40 characters |
| NTAG213 | 136 characters |
| NTAG215 | 483 characters |
| NTAG216 | 859 characters |

## Rules for other projects

- Ignore options you do not know.
- If you do not know an action, report the card and do nothing else.
- The meaning of an action or option never changes. New behaviour gets a new action or option,
  which is why the scheme has no version number.
