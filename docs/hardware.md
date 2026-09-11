# T-Lora Pager, hardware reference

Sourced from LilyGo's hardware doc and LilyGoLib, Meshtastic's `tlora-pager`
variant, and the AW9364 and XL9555 datasheets. Where sources disagree, both are
listed.

Board: LilyGo T-Lora Pager, ESP32-S3, SX1262 sub-GHz variant.

## The three constraints that shape the UI

1. **The screen is 480x222.** Ultra-wide and short. That is 2.33in, 221 PPI, IPS,
   262K colours, 450 cd/m2. Two thirds the height of a 320x240 panel with double the
   width. Vertical lists run out of room fast: at 16px line height you get about 12
   rows, and that is before a header or status bar. Design across, not down.
2. **There is no touchscreen.** LilyGo states this outright. Every screen needs an
   explicit focus model and a key map. Nothing can rely on a tap target.
3. **There are no arrow keys.** The keyboard is 4x10 letters, Enter, Space and a few
   modifiers (map below). Directional navigation has to come from the rotary encoder.
   Rotate to move focus, press to activate. That is the whole navigation primitive,
   so it has to be right.

Practical consequence: the rotary encoder is the d-pad and the OK button. The
keyboard is for text entry and shortcuts. Plan every screen as "what does rotate do
here, what does press do here", and give focus a visible treatment that reads at
221 PPI.

## Display

| Property | Value |
|---|---|
| Controller | ST7796U, SPI |
| Resolution | 480 x 222 |
| Native orientation | portrait, 222 wide x 480 tall |
| Offset | 49 on the short axis |
| Diagonal / PPI | 2.33in / 221 |
| Colours | 262K (18-bit) |
| Contrast / luminance | 1000:1 / 450 cd/m2 |
| Operating temp | -20 to 70 C |
| Touch | none |

The 49 offset is the ST7796's 320-wide memory minus the 222-wide glass, centred:
(320 - 222) / 2 = 49. It applies to whichever axis is short in the current rotation.
LilyGo's own rotation table sets offset (49, 0) in portrait and (0, 49) in landscape.
LovyanGFX wants the offsets in the panel's native orientation and rotates them
itself, so portrait values are the ones to configure.

`memory_width` must be 320, not 222 and not 240. `Panel_ST7796`'s constructor
already sets 320x480, and 49 + 222 = 271 has to fit inside it. Meshtastic leaves
both memory fields at the driver default for exactly this reason.

## Backlight: an AW9364, not a plain LED

This one is a trap and it is worth reading twice.

GPIO42 does not drive the backlight LEDs. It drives an **AW9364**, a 1-wire dimming
LED driver with a 4-bit DAC giving **16 current steps** from 20 mA down to 1.25 mA.
Awinic's datasheet is explicit that this is a constant-current ladder chosen
*instead of* PWM dimming, and the EN pin has a deglitch filter on it.

The protocol, from LilyGo's own `AW9364LedDriver`:

- Hold the pin LOW: output off.
- Drive it HIGH from off: the chip turns on at **level 16, maximum**.
- Every LOW to HIGH pulse steps the current **down one level**, wrapping 1 back to 16.
- Pulses are back-to-back `digitalWrite` calls, no delay. They just have to stay far
  shorter than the roughly 2.5 ms that reads as shutdown.
- Pulses to get from level `a` to level `b`: `(a - b) mod 16`.

So the number of pulses is the whole interface. There are exactly 16 brightness
values and a gamma curve over a PWM duty cycle means nothing to this chip: a 20 kHz
PWM is 20,000 down-steps a second, wrapping constantly.

Meshtastic drives pin 42 with `lgfx::Light_PWM` at 44.1 kHz anyway and gets a
picture, so it evidently does something. It is not the designed interface and it is
not what we do. Fades are done by stepping levels on a timer, which is also how
LilyGo's `BrightnessController` does it.

The keyboard backlight on GPIO46 is a separate, ordinary PWM pin.

## Input

**Rotary encoder**, GPIO40 (A), GPIO41 (B), GPIO7 (centre press). The navigation
device. LilyGo runs it from a dedicated FreeRTOS task rather than polling.

**Keyboard**, TI TCA8418 matrix controller at I2C 0x34, interrupt on GPIO6,
backlight on GPIO46 (PWM). The matrix is **4 rows x 10 columns**. LilyGo's map:

```
row 0   q  w  e  r  t  y  u  i  o  p
row 1   a  s  d  f  g  h  j  k  l  \n
row 2   -  z  x  c  v  b  n  m  -  -
row 3   space
```

Symbols are a second map over the same matrix, reached with the modifier rather than
a dedicated symbol key (`has_symbol_key` is false, and LilyGo's manual describes it
as Space + key):

```
row 0   1  2  3  4  5  6  7  8  9  0
row 1   *  /  +  -  =  :  '  "  @  -
row 2   -  _  $  ;  ?  !  ,  .  -  -
row 3   space
```

Modifier raw key codes, straight from LilyGo's config: symbol `0x1E`, alt `0x14`,
caps `0x1C`, backspace `0x1D`, the second character key `0x19`.

Note what is absent: no arrows, no Esc, no Tab, no function row. Back and cancel
have to be assigned. Backspace as "back" is the obvious candidate since it is the
only key that already means undo.

**Buttons**: BOOT on GPIO0 and RST, both physical. BOOT is a normal user button once
the board is running, which is what the self test hangs off.

## Chips, buses and addresses

| Part | Function | Bus | Address / pins |
|---|---|---|---|
| ESP32-S3 | MCU | | 16MB flash QSPI, 8MB PSRAM |
| ST7796U | display | SPI | CS 38, DC 37, BL 42, RST tied to board reset |
| SX1262 | LoRa | SPI | CS 36, IRQ 14, BUSY 48, RST 47 |
| ST25R3916 | NFC | SPI | CS 39, INT 5 |
| microSD | storage | SPI | CS 21 |
| XL9555 | GPIO expander | I2C | 0x20 |
| TCA8418 | keyboard | I2C | 0x34, INT 6 |
| ES8311 | audio codec | I2C + I2S | 0x18 |
| BHI260AP | IMU | I2C | 0x28, INT 8 |
| PCF85063A | RTC | I2C | 0x51, INT 1 |
| BQ27220 | fuel gauge | I2C | 0x55 |
| DRV2605 | haptic | I2C | 0x5A |
| BQ25896 | charger | I2C | 0x6B |
| NS4150B | 3W class D amp | | gated by the expander |
| MIA-M10Q | GNSS | UART | RX 4, TX 12, PPS 13, 38400 baud |
| AW9364 | backlight driver | 1-wire | GPIO42 |

Shared SPI bus: SCK 35, MOSI 34, MISO 33, separate CS per device. I2C: SDA 3,
SCL 2. I2S: BCK 11, WS 18, MCLK 10, DOUT 45, DIN 17.

External 12-pin socket exposes GPIO9, GPIO43, GPIO44.

### Disagreements between sources

- **NFC part.** LilyGo's hardware doc and Meshtastic both say ST25R3916, and the
  RFAL driver talks to it as one. One community writeup says ST25R3911B. It is on
  the SPI bus with its own CS on GPIO39, not on I2C.
- **IMU bus.** LilyGo says the BHI260AP is I2C and their own code calls
  `sensor.begin(Wire)` with a comment giving 0x28, so I2C is the one to trust. A
  community writeup says SPI.

### The XL9555 numbering trap

LilyGo's documentation numbers the expander's upper port `GPIO10, GPIO11, GPIO12,
GPIO14`. Those are port-1 pin labels (P1.0, P1.1, P1.2, P1.4), not linear indices.
Linearly, port 1 starts at 8. Meshtastic's variant uses linear numbering and so does
`board_pins.h`:

| Rail | LilyGo label | Linear (what we use) |
|---|---|---|
| Haptic enable | 0 | 0 |
| Amp enable | 1 | 1 |
| Keyboard reset | 2 | 2 |
| LoRa power | 3 | 3 |
| GNSS power | 4 | 4 |
| NFC power | 5 | 5 |
| GNSS reset | 7 | 7 |
| Keyboard power | P1.0 "10" | 8 |
| External socket | P1.1 "11" | 9 |
| SD detect | P1.2 "12" | 10 |
| SD pull enable | not documented | 11 |
| SD power | P1.4 "14" | 12 |

If you ever "correct" `board_pins.h` to match LilyGo's doc, you will break the
keyboard, the SD card and the expansion socket at once. Leave it linear.

## Power

- Battery 1500 mAh, 3.7 V, 5.55 Wh. BQ27220 gauge is configured for a 1500 mAh
  design capacity, which matters: a wrong design capacity gives a plausible but
  wrong percentage.
- BQ25896 charger, 128 to 2048 mA in 64 mA steps.
- USB-C for power and programming, native USB CDC, no serial driver needed.
- Everything behind the expander is off at reset. See the rails table above.

## Radio

SX1262 on the shared SPI bus. DIO2 is the RF switch control, DIO3 drives the TCXO at
3.0 V, DIO0 is not connected. INW mesh parameters are 910.5250244 MHz, BW 62.5, SF7,
CR5, which is not the USA preset.

The board ships in other radio flavours (SX1280 2.4G, LR1121 dual-band, CC1101,
Si4432) on the same footprint and pins. Ours is SX1262. Antenna is foldable, and it
goes on before you transmit.

## Timings and speeds worth copying

- I2C runs at 400 kHz normally. LilyGo raises it to 1 MHz around BHI260AP init and
  drops it back. The AW9364 driver also wants 1 MHz if it is ever driven through an
  expander pin, which ours is not.
- LilyGo opens the SD card at 4 MHz on the shared bus.
- Meshtastic runs the panel at 75 MHz write / 16 MHz read. This firmware uses
  80 MHz write, 16 MHz read.
- `bus_shared = true` and `use_lock = true` both matter on this board, since LoRa,
  display and SD are one bus.

## Sources

- LilyGo hardware spec: https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/docs/hardware/lilygo-t-lora-pager.md
- LilyGoLib driver code (keymap, AW9364 usage, rotation table): https://github.com/Xinyuan-LilyGO/LilyGoLib
- AW9364 driver implementation: https://github.com/lewisxhe/SensorLib `src/actuator/AW9364LedDriver.hpp`
- Meshtastic variant: https://github.com/meshtastic/firmware `variants/esp32s3/tlora-pager`
- AW9364 datasheet: https://datasheet.lcsc.com/lcsc/1912111437_AWINIC-Shanghai-Awinic-Tech-AW9364DNR_C401007.pdf
