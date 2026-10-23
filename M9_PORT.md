# Squatch Mesh on the Elecrow ThinkNode M9

Built from published sources only. Nobody on the project has an M9, so this has
**never run on one**. Everything below marked *unverified* is a best reading of
MeshCore's and Meshtastic's M9 support (copies in `m9-reference/`) and Elecrow's
wiki.

Build: `pio run -e thinknode-m9` (or `thinknode-m9-dev` for the USB test commands).

## How it maps onto the M9

| | |
|---|---|
| Screen | ST7789 320x240, the T-Deck's layouts. Rotation 3 (MeshCore's MV+MY). *If it shows upside down: `TFT_ROTATION 1` in `src/m9/display_config.h`.* |
| Keys | STC8H at 0x6C (v1.0) / 0x6D (v1.1), key code from register 0x01 on each rising edge of GPIO12. |
| Moving around | Arrow keys = the T-Deck's trackball. Enter = select (and send while typing). Del = back / backspace. **Home** = lock and screen off; Home again wakes. |
| Radio | LR1110: CS 39, IRQ 42, BUSY 41, RST 45, 3.3 V TCXO, DIO5/DIO6 antenna switch (both firmwares' table). |
| GPS | v1.0 L76K (9600, enable active LOW) / v1.1 AG3352 (115200, enable HIGH). Real power switch on GPIO11. *The two firmwares disagree which pin is RX (3 or 2): it tries one, and the other after 6 s of silence.* |
| Battery | Cell / 2 on GPIO13; GPIO1 LOW while charging. *Divider not measured.* The flat-battery hold is **off** until a real reading is seen to be right. |
| Backlight | GPIO17, PWM, active LOW. |
| Sound | Buzzer on GPIO9, played as PWM. *Never heard.* |
| Not used yet | PCF8563 clock, QMI8658 motion, QMC6309 compass (second I2C bus, SDA 7 / SCL 6), the Mute / Time / torch / FM / Preset keys, the touch-only settings. |
| Updates | Wi-Fi updates look in `firmware/thinknode-m9/` (none published yet), signed for "thinknode-m9" only. |

## Format the microSD card first

Some M9 batches shipped with a Windows worm on the microSD card (Elecrow confirmed
it, September 2026: a hidden `autorun.inf` that runs on a Windows PC). It can't run on
the M9, but Squatch Mesh writes backups and map tiles to that card and people copy
them to a PC. Before using it: take Elecrow's free replacement card, or format the
card (not on a Windows PC with auto-play on, and never open `autorun.inf` or a `.pif`
file on it). Elecrow's notice: https://www.elecrow.com/Thinknode-M9-Security-Advisory-SD-Card-Malware-Risk.html

## First test on a real M9 (in this order)

1. Flash `squatch-mesh-thinknode-m9-<version>-full.bin` at 0x0 (esptool, or the ESP web flasher). Hold BOOT while plugging in if it won't connect. **This replaces the stock firmware**; the stock MeshCore identity and contacts are read across on the first start (as on the T-Deck), but make a backup in the stock firmware first.
2. Screen: lit, the right way up, colours right (not inverted)?
3. Keys: Settings > System > Hardware check shows the board version and the last key pressed.
4. Radio: the hardware check says LR1110; send a message to a node nearby both ways.
5. GPS outdoors: the hardware check says "talking" and then a fix.
6. Battery: compare the percentage with a meter or the stock firmware's figure.

Over USB at 115200 the boot log names every step; `status` and `batt` work in the release build.
