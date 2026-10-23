# Squatch Mesh on the Elecrow ThinkNode M9

Built from published sources only. Nobody on the project has an M9, so this has
**never run on one**. It follows MeshCore's and Meshtastic's M9 support (copies in
`m9-reference/`), Elecrow's wiki, and where they disagree, Wadamesh's M9 port
(github.com/ALLFATHER-BV/wadamesh, `variants/thinknode_m9/`), whose notes were checked
on a real pre-production M9 against the schematic. Read only for the facts; none of
its (GPL) code is copied. Anything marked *unverified* is still a best reading.

Build: `pio run -e thinknode-m9` (or `thinknode-m9-dev` for the USB test commands).

## How it maps onto the M9

| | |
|---|---|
| Screen | ST7789 320x240, the T-Deck's layouts. Rotation 1, as on the T-Deck (Wadamesh: 3 was upside down on a real one). |
| Keys | An ESP32-S2 key scanner on its own I2C bus (20/21) at 0x6C (v1.0) / 0x6D (v1.1): key code from register 0x01, polled every 15 ms; key light is register 0x02. GPIO20 is also the S3's USB D+, released at start or the keyboard never answers. |
| Moving around | Arrow keys = the T-Deck's trackball. Enter = select (and send while typing). Del and Back = back / backspace. **Home** = lock and screen off; Home again wakes. |
| Radio | LR1110: CS 39, IRQ 42, BUSY 41, RST 45, 3.3 V TCXO, DIO5/DIO6 antenna switch (both firmwares' table). |
| GPS | Receives on 3, sends on 2, 115200 (9600 also tried). Enable on GPIO11, active LOW on v1.0 (*HIGH on v1.1, per Meshtastic*). Reset on GPIO5 is held LOW (HIGH holds the GPS in reset). |
| Battery | Cell / 2 on GPIO13 (ADC2: readings Wi-Fi blocks are dropped). *GPIO1 LOW on USB is unverified* (the USB socket goes to a bridge chip, so the S3 can't see the cable). The flat-battery hold is **off** until a real reading is seen to be right. |
| Backlight | GPIO17, PWM, active LOW. |
| Sound | Buzzer on GPIO9, played as PWM. *Never heard.* |
| USB | A USB-serial bridge, not the S3's own USB: the console and the installer's commands are on UART0. No user button on any GPIO (only the power slider and reset). |
| Not used yet | PCF8563 clock, QMI8658 motion, QMC6309 compass (second I2C bus, SDA 7 / SCL 6), the message / map / triangle / Ctrl keys, the touch-only settings. |
| Updates | Wi-Fi updates look in `firmware/thinknode-m9/`, signed for "thinknode-m9" only. An `m9-v<FW_VERSION>` tag builds and publishes one (.github/workflows/m9.yml, then build.yml on main); every M9 build goes to both channels. |

## Format the microSD card first

Some M9 batches shipped with a Windows worm on the microSD card (Elecrow confirmed
it, September 2026: a hidden `autorun.inf` that runs on a Windows PC). It can't run on
the M9, but Squatch Mesh writes backups and map tiles to that card and people copy
them to a PC. Before using it: take Elecrow's free replacement card, or format the
card (not on a Windows PC with auto-play on, and never open `autorun.inf` or a `.pif`
file on it). Elecrow's notice: https://www.elecrow.com/Thinknode-M9-Security-Advisory-SD-Card-Malware-Risk.html

The firmware also cleans it up itself (`src/sdguard.*`): on every start it looks at the
card's top folder and deletes an `autorun.inf` that starts a program, the programs it
names (.exe, .pif, .scr, .com, .bat, .cmd, .vbs, .js, .wsf, .lnk) and any `.pif` file,
taking the worm's hidden/read-only marks off first. Nothing else is touched and the card
is never formatted. If it found anything, Squatch punches and stomps the virus on screen
("I got ya!") and the log says what went. *Never run against the real worm*: the
dev build's `virusplant` leaves a harmless stand-in, `virussweep` deletes it as a start
would, `virusfight` only plays the animation.

## First test on a real M9 (in this order)

1. Flash `squatch-mesh-thinknode-m9-<version>-full.bin` at 0x0 (esptool, or the ESP web flasher). There is no BOOT button: the USB bridge chip puts it into flash mode by itself, as with Elecrow's own updater. **This replaces the stock firmware**; the stock MeshCore identity and contacts are read across on the first start (as on the T-Deck), but make a backup in the stock firmware first.
2. Screen: lit, the right way up, colours right (not inverted)?
3. Keys: Settings > System > Hardware check shows the board version and the last key pressed.
4. Radio: the hardware check says LR1110; send a message to a node nearby both ways.
5. GPS outdoors: the hardware check says "talking" and then a fix.
6. Battery: compare the percentage with a meter or the stock firmware's figure.

Over USB at 115200 (through the bridge chip) the boot log names every step; `status` and `batt` work in the release build.

The radio: older pre-production LR1110s (firmware 3.3) rejected a command newer RadioLib sends; the RadioLib this builds with (7.7.1) already skips it on firmware before 3.6.
