# Third-party code and assets

## Linked libraries (fetched by PlatformIO)

| Library | Licence | Used for |
|---|---|---|
| [MeshCore](https://github.com/meshcore-dev/MeshCore) | MIT | mesh stack; the companion node in `examples/companion_radio` is the engine |
| [RadioLib](https://github.com/jgromes/RadioLib) | MIT | SX1262 driver |
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | FreeBSD | display, sprites, JPEG/PNG decoding |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | MIT | MeshCore JSON import |
| [Crypto](https://github.com/rweather/arduinolibs) | MIT | used by MeshCore |
| [RTClib](https://github.com/adafruit/RTClib) | MIT | used by MeshCore |
| [CayenneLPP](https://github.com/ElectronicCats/CayenneLPP) | MIT | telemetry encoding |
| [base64](https://github.com/Densaugeo/base64_arduino) | MIT | used by MeshCore |
| [Melopero RV3028](https://github.com/melopero/Melopero_RV-3028_Arduino_Library) | MIT | used by MeshCore |

NFC (in the `t-lora-pager` build and the release binaries; licence text in `licenses/ST-SLA0052.txt`):

| Library | Licence | Used for |
|---|---|---|
| [ST25R3916-NFC-RFAL](https://github.com/lewisxhe/ST25R3916-NFC-RFAL) | ST SLA0052 | ST25R3916 driver, RFAL and NDEF |

## Included in this repository

- **Colour emoji** (`src/emoji_data.h`): Google's [Noto Color Emoji](https://github.com/googlefonts/noto-emoji)
  (Apache-2.0 / SIL OFL 1.1), as baked to 16x16 RGB565 by
  [Wadamesh](https://github.com/ALLFATHER-BV/wadamesh) (GPL-3.0). Regenerate with
  `tools/convert_emoji.py`.
- **ES8311 register sequence** (`src/es8311_codec.h`): follows Espressif's es8311
  driver as adapted for this board in Wadamesh (GPL-3.0).
- **Board details** cross-checked against Meshtastic's `tlora-pager` variant
  (GPL-3.0) and LilyGo's LilyGoLib.

Wadamesh also inspired several screen layouts (messenger, settings grid, map) and
its SD data folder can be imported on first boot.
