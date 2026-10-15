# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Stack

Plain HTML, CSS and JavaScript modules in `ui/`, no framework and no build step beyond gzip
(`scripts/build_ui.py`). Served by the ESP32 from LittleFS; the gzipped UI must stay under about
96 KB to fit the 128 KB filesystem of 4 MB boards. No network access beyond the device itself: no
CDN fonts, scripts or images.

## Users

Primary: a parent setting up a kids' NFC music box, on a phone, often connected to the box's own
setup Wi-Fi. They do setup once (Wi-Fi, maybe Home Assistant) and come back for small changes: a
new card, a volume limit, an update. They are not technical and should never need to know what a
GPIO is to finish a task.

Secondary: Home Assistant users who flash Tapwave as a tag reader. They use the same UI; pins,
MQTT topics and diagnostics sit behind Advanced sections rather than in a separate product.

## Product Purpose

Tapwave is open-source ESP32 firmware that is a music box, a Home Assistant tag reader and a tag
writer in one binary. The web UI is how a person sets the device up, sees that it works (the last
card tapped, live), changes settings without reflashing, and updates it. Success: a parent goes
from a freshly flashed board to "tap a card and it shows up" without help, and later changes
settings without fear of breaking it.

## Positioning

The one device that covers the three jobs other projects split (ESPuino and TonUINO play, adonno's
tagreader reports to Home Assistant, phone apps write tags), configured entirely from a browser:
pins, reader and modes are settings, not compile-time choices.

## Operating Context

- Usually a phone in portrait, sometimes over the setup access point with no internet.
- The device is slow compared with a server: one small HTTP server, a WebSocket for live events.
- Milestone 1 ships Home (status and the last card), Cards (recent scans), Library (what is
  coming), Settings, Update and the Wi-Fi setup flow; writing cards and the tag table arrive in
  milestone 2, the SD library and player in milestone 3.
  Pages for later milestones say plainly what they will do and when, without fake controls.

## Capabilities and Constraints

- Everything the UI does goes through the public REST API and WebSocket (ARCHITECTURE.md
  section 13); the UI has no private endpoints.
- Settings changes may need a restart; the API says which, and the UI must say so too.
- An optional admin password protects the API; the UI needs a sign-in screen for it.
- Pins are validated by the firmware per chip; the UI shows its messages next to the field.

## Brand Commitments

- Name: Tapwave. Card URI scheme: `tapwave://`.
- Voice: calm and plain. Short, friendly sentences for an adult reader, no jargon on main
  screens, not childish. Errors say what happened, why if it helps, and what to do next.
- No em-dashes anywhere in copy. No marketing filler.
- English first; every string lives in one table so contributors can add languages.

## Evidence on Hand

No photos of real devices, no testimonials, no user counts. Do not invent any.

## Product Principles

1. A parent finishes every everyday task without reading documentation.
2. Technical detail is available, never in the way: Advanced sections, not separate tools.
3. The device's real state is always visible: what is connected, what is not, and why.
4. Nothing destructive happens without a clear confirmation in the page itself.
5. Honest about milestones: show what works today, name what is coming.

## Accessibility & Inclusion

WCAG 2.2 AA: contrast, keyboard use, visible focus, labels on every control, touch targets of at
least 44 px, and respect for reduced motion. Readable in light and dark mode.
