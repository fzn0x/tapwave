# Contributing to Tapwave

Thanks for helping. Tapwave is meant to be easy to work on: most of the code runs and is tested on
a normal computer, and the web UI runs against a pretend box, so you do not need hardware for most
changes.

## Ways to help

- **Try it on your hardware** and report what happened, good or bad. Reports from real boards and
  readers are the most useful contribution right now. Use the "Hardware report" issue form.
- **Fix a bug or a typo.** Small pull requests are welcome without an issue first.
- **Add a reader.** A new NFC reader is one new file; see
  [Adding a reader](docs/wiki/Adding-a-Reader.md).
- **Translate the web UI.** All strings live in one table, see [Translations](#translations).
- **Improve the docs.** The wiki pages are Markdown files in [`docs/wiki/`](docs/wiki), mirrored
  to the GitHub wiki, so a docs change is a normal pull request.

For a larger change, such as a new feature, a new dependency or anything that touches the
configuration format, the MQTT topics or the `tapwave://` scheme, please open an issue first so we
can agree on the approach. Those are promises to installed devices and are hard to change later.

## Set up

You need:

- [Python](https://www.python.org) 3.11 or newer
- [PlatformIO Core](https://platformio.org) 6.2: `pip install "platformio==6.2.0"`
- [Node.js](https://nodejs.org) 24 or newer, only for the web UI tests (no packages to install)
- [clang-format](https://clang.llvm.org/docs/ClangFormat.html) 23: `pip install "clang-format==23.1.3"`

Then:

```sh
git clone https://github.com/fzn0x/tapwave.git
cd tapwave
pio test -e native          # the first run downloads the toolchains
```

On Windows, run PlatformIO from PowerShell or `cmd`. ESP-IDF's tools refuse to run inside an MSYS2
shell (Git Bash works if `MSYSTEM` is unset).

## Where things are

[ARCHITECTURE.md](ARCHITECTURE.md) is the map. In short:

| Folder | What lives there | Tested on a computer |
| --- | --- | --- |
| `src/core/` | NDEF, UIDs, the scan model, the URI scheme, configuration, MQTT payloads, Improv | Yes |
| `src/drivers/nfc/common/` | Reader protocols (PN532 frames, RC522 registers, Type 2 tags) | Yes, against simulated tags |
| `src/drivers/` | Hardware drivers: buses, readers, LED, buzzer | Built in CI |
| `src/net/` | Wi-Fi, Improv, HTTP server, MQTT, updates | Built in CI |
| `src/app/` | Wires everything together, the REST API | Built in CI |
| `ui/` | The web UI: HTML, CSS and JavaScript modules, no framework | Yes, with `node --test` |
| `scripts/` | Build helpers, the mock box, size report | Yes, with `unittest` |
| `test/` | C++ unit tests, one folder per module | |

The one rule that keeps this working: **`src/core/` and `src/drivers/nfc/common/` never include
Arduino or ESP-IDF headers.** The native test build fails if they do.

## Run the tests

```sh
pio test -e native                             # C++ unit tests
pio test -e native-asan                        # the same with AddressSanitizer and UBSan (Linux, macOS)
cd ui && npm test                              # web UI
python -m unittest discover -s scripts/tests   # build scripts and the mock box
pio run -e esp32-4mb                           # firmware build for the tightest board
```

CI runs all of these on every pull request, builds all four board environments and posts the
firmware size against the OTA slot in the job summary. The 4 MB boards have little room left, so
watch that number if your change adds code.

## Working on the web UI without a board

```sh
python scripts/mock_device.py                       # http://localhost:8080
python scripts/mock_device.py --scenario setup      # a box in Wi-Fi setup
python scripts/mock_device.py --scenario problems   # no reader, Wi-Fi down, broker refusing
python scripts/mock_device.py --admin-password x    # the sign-in screen
```

The mock box serves `ui/` straight from disk, so a browser refresh shows your change. It speaks the
same REST API and WebSocket as the firmware, with the same validation messages.

Before you open a pull request for a UI change, please:

- check it at phone width (390 px) and on a wide screen, in light and dark mode;
- keep the voice of [PRODUCT.md](PRODUCT.md): calm, plain words for a parent, and every problem
  says what to do next;
- follow [DESIGN.md](DESIGN.md), in particular: orange only on things that can be pressed;
- put every new string in `ui/lib/i18n.js`, never in `app.js`;
- keep the gzipped UI under its budget (`python scripts/build_ui.py <out dir>` prints the total);
- add no dependencies and nothing loaded from the internet. The box is often not online.

## Code style

- C++23, formatted with the repository's `.clang-format` (Google style, 4 spaces, 100 columns).
  Run `clang-format -i` on the files you touched; CI checks it.
- Types are `PascalCase`, functions and variables `snake_case`, constants `kPascalCase`, members
  end in `_`. Everything sits in the `tapwave` namespace.
- Errors are values: return `std::expected` or `std::optional` instead of throwing.
- Hardware waits have timeouts. A reader that stops answering must never hang the box.
- Comments explain why, not what.
- Nothing that belongs in configuration is hard-coded: pins, hosts, intervals and names are
  settings with defaults in `src/core/config.cpp`. No credentials or personal endpoints in source.
- New logic in `core/` or `drivers/nfc/common/` comes with a doctest in `test/`.

## Changing the configuration format

The configuration is stored on the device and must survive updates. If you add, rename or remove a
setting:

1. Bump `kSchemaVersion` in `src/core/config.hpp`.
2. Add a migration from the previous version to `migrations()` in `src/core/config.cpp`.
3. Add a test for the migration in `test/test_config/`.
4. Update the table in [ARCHITECTURE.md section 10](ARCHITECTURE.md#10-configuration).

Adding a setting with a default does not need a migration; defaults fill missing keys.

## Translations

1. Copy the `en` table in `ui/lib/i18n.js` to a new language code, for example `de`.
2. Translate the values. Keep every `{placeholder}` exactly as it is.
3. Run `cd ui && npm test`. It fails if a key or a placeholder is missing.

## Commits and pull requests

We use [Conventional Commits](https://www.conventionalcommits.org):

```
feat(nfc): detect the PN532 on its second I2C address

The red PN532 boards from some sellers answer on 0x48 after a reset.
```

- Types: `feat`, `fix`, `docs`, `test`, `build`, `ci`, `refactor`, `perf`, `chore`.
- Scopes follow the folders: `core`, `nfc`, `net`, `app`, `io`, `ui`, `scripts`, `architecture`.
- The summary is lower case and imperative. The body says why the change is needed.
- Keep commits small and focused; several small commits are easier to review than one large one.

In the pull request, say what you tested and on which hardware, if any. Update the docs and the
wiki pages your change affects in the same pull request.

## Writing style

Docs, comments, UI strings and commit messages use plain words and no em dashes. If a sentence
needs one, use a comma, a colon, parentheses or two sentences.

## License

By contributing, you agree that your contributions are licensed under the [MIT License](LICENSE).
