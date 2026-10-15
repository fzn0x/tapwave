# Security policy

## Reporting a vulnerability

Please do not open a public issue for a security problem. Report it privately through GitHub:
open the repository's **Security** tab and choose **Report a vulnerability**. If you cannot use
GitHub, email fncolon@pm.me.

Include what you found, the firmware version (shown on the Update page and in `/api/status`), the
board, and the steps to reproduce it. You should get a reply within a week. Once a fix is released
you will be credited in the release notes, unless you prefer not to be.

## Supported versions

Fixes go into the latest release. Boxes update from the web UI, from Home Assistant or from the
release manifest, so there are no long-lived release branches.

## How Tapwave is meant to be deployed

Knowing the intended setup helps judge whether something is a vulnerability:

- **The web UI and API are for the local network.** They are served over plain HTTP, like most
  home devices. Do not forward the box's port to the internet.
- **The admin password is optional.** Without it, anyone on the local network can change settings.
  With it, every endpoint except `GET /api/status` and sign-in needs a session cookie or HTTP Basic
  authentication. Requests that change state must send a JSON or binary content type, which other
  websites cannot send without a CORS preflight that the box never approves.
- **Secrets are never returned by the API.** Wi-Fi, MQTT and admin passwords are stored in the
  ESP32's NVS and read back as `********`. NVS is not encrypted, so anyone with physical access to
  the board can read them.
- **The setup network is open** unless an access point password is set, and it only exists while
  the box has no working Wi-Fi.
- **Updates from the release manifest** are downloaded over HTTPS, checked against the ESP-IDF
  certificate bundle and a SHA-256 in the manifest, and rolled back automatically if the new
  firmware never gets online. Uploaded files are not signed.

Reports that break one of these promises are very welcome. Reports that need physical access to the
board, or an attacker who already has the admin password, are usually out of scope, but tell us if
you think the impact is larger than it looks.
