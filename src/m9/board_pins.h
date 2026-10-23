// Elecrow ThinkNode M9 pin map.
//
// Nobody here has one: every value below is taken from the two firmwares that run
// on real M9s - MeshCore's variants/thinknode_m9 and Meshtastic's
// variants/esp32s3/ELECROW-ThinkNode-M9 - checked against Elecrow's wiki pin table.
// Copies of those files are in m9-reference/. Where they disagree it is noted.
//
// Two hardware versions are out there. They are told apart by the keyboard
// controller's I2C address (0x6C on v1.0, 0x6D on v1.1), and they differ in the GPS:
// v1.0 has an L76K at 9600 baud whose enable is active LOW, v1.1 an AG3352 at
// 115200 whose enable is active HIGH.

#pragma once

#define BOARD_NAME     "ThinkNode M9"

// ---- What this board has. Shared code checks these, never the board name. ----
#define SCREEN_W               320
#define SCREEN_H               240
#define BOARD_HAS_SIDE_BUTTON  0    // no free button: the keyboard's Home key locks and wakes (main.cpp)
#define BOARD_HAS_POWER_OFF    0
#define BOARD_HAS_HAPTIC       0
#define BOARD_HAS_RTC          0    // a PCF8563 is fitted (0x51 on SDA 7 / SCL 6); not driven yet
#define BOARD_HAS_CHARGER_IC   0    // an LGS4056 charger with no I2C: the battery is an ADC reading
#define BOARD_HAS_EXT_HEADER   0
#define BOARD_HAS_KB_BACKLIGHT 1    // a plain GPIO (46), not a command
#define BOARD_HAS_TOUCH        0    // no touch panel: the keyboard drives everything
#define BOARD_ROW_H            28   // the T-Deck's rows: same screen, same size of text
#ifndef BOARD_ASYNC_PUSH
#define BOARD_ASYNC_PUSH       1    // the T-Deck's overlapped frame send, same bus arrangement
#endif
#define BOARD_FADE_BY_CLOCK    1
#define BOARD_HAS_TRACKBALL    1    // the four arrow keys stand in for the T-Deck's trackball (rotary.h)
#ifndef BOARD_FRAME_MS
#define BOARD_FRAME_MS         33
#endif
#define BOARD_HOME_DASHBOARD   1    // the T-Deck's home screen (src/m9/dashboard.cpp)
#define BOARD_SMOOTH_SHAPES    1
// Keys only, no trackball click: Enter presses on the home and lock screens too, and
// the keyboard's Home key does what the T-Deck's long trackball press does (main.cpp).
#define BOARD_KEYS_ONLY        1

// Wi-Fi updates come from firmware/thinknode-m9/ and must carry a signature naming
// "thinknode-m9" (ota.cpp): pager or T-Deck firmware can never be installed on it this way.
#define OTA_SUBDIR             "thinknode-m9/"
#define OTA_BOARD              "thinknode-m9"
#define BOARD_HAS_REPORTS      1
#define REPORT_BOARD           "thinknode-m9"
// The GPS has a real enable pin here (unlike the T-Deck), so it is switched off,
// not put to sleep by command.
#define BOARD_GPS_SLEEP_BY_COMMAND 0
#define BOARD_SLOW_CPU_WHEN_DARK   1
#define BOARD_RADIO_ONLY_WHEN_DARK 1
// The ADC reads the cell itself, behind the charger (Meshtastic: BATTERY_PIN 13,
// multiplier 2), so it means something on USB too.
#define BOARD_BATT_UNSEEN_ON_USB   0
#define BOARD_BATTERY_FROM_VOLTAGE 1
#define BAT_DIVIDER                2.0f   // Meshtastic's ADC_MULTIPLIER for the M9
// No flat-battery hold (main.cpp) until a real M9's battery reading is known to be right:
// a reading that came out low would keep it from starting, with no trackball to override.
#define BOARD_FLAT_HOLD            0

// ---- Power: one switch (active LOW) for the screen, GPS and other peripherals ----
#define PIN_POWER_ON   18
#define POWER_ON_LEVEL LOW

// ---- Shared SPI bus: LoRa, display and microSD ----------------------------------
#define PIN_SPI_SCK    40
#define PIN_SPI_MOSI   47
#define PIN_SPI_MISO   38

// ---- I2C: the keyboard controller (STC8H) --------------------------------------
// Meshtastic puts it on the main bus (SDA 20 / SCL 21); the RTC and sensors are on a
// second bus (SDA 7 / SCL 6) that nothing here uses yet.
#define PIN_I2C_SDA    20
#define PIN_I2C_SCL    21
#define PIN_I2C2_SDA   7
#define PIN_I2C2_SCL   6

// ---- LoRa radio (LR1110) --------------------------------------------------------
#define PIN_LORA_CS    39
#define PIN_LORA_DIO1  42    // the IRQ line
#define PIN_LORA_BUSY  41
#define PIN_LORA_RST   45

// ---- Display: ST7789, 2.4", 240x320, run landscape ---------------------------------
#define PIN_TFT_CS     16
#define PIN_TFT_DC     15
#define PIN_TFT_RST    14
#define PIN_TFT_BL     17    // PWM, active LOW (both firmwares: TFT_BACKLIGHT_ON LOW)
#define TFT_BL_ACTIVE_LOW 1

// ---- microSD (shared SPI bus) ---------------------------------------------------
#define PIN_SD_CS      48

// ---- Keyboard: an STC8H answering on I2C ----------------------------------------
#define ADDR_M9_KB_V10 0x6C
#define ADDR_M9_KB_V11 0x6D
#define PIN_KB_INT     12    // rests LOW, goes HIGH when a key is pressed
#define PIN_KB_BL      46

// The T-Deck's trackball names, for shared code that reads "the wheel" or "the side
// button". There is no trackball; shared code still reads PIN_BUTTON at start-up (held:
// self test) and to wake from the flat-battery sleep, so it is GPIO0, the ESP32-S3's
// BOOT strap: pulled up, so it reads "not pressed" whether or not the M9 brings a
// button out to it.
#define PIN_TB_CLICK     0
#define PIN_ROTARY_A     (-1)
#define PIN_ROTARY_B     (-1)
#define PIN_ROTARY_PRESS 0
#define PIN_BUTTON       0

// ---- Buzzer: a transistor on GPIO9 (no speaker, no I2S) -------------------------
#define PIN_M9_BUZZER     9

// ---- GPS, UART ---------------------------------------------------------------------
// MeshCore's M9 build receives on 3; Meshtastic's on 2. gps.h tries the other way round
// if nothing is heard (GPS_PINS_UNSURE).
#define PIN_GPS_RX     3    // ESP32 receives here
#define PIN_GPS_TX     2
#define GPS_PINS_UNSURE 1
#define PIN_GPS_PPS    4
#define PIN_GPS_EN     11   // active LOW on v1.0, HIGH on v1.1 (io_expander.h picks)
#define PIN_GPS_RST    5
#define PIN_GPS_WAKE   10   // v1.1's AG3352: held HIGH so it never sleeps under us
#define GPS_BAUD       115200   // v1.1
#define GPS_BAUD_ALT   9600     // v1.0; gps.h tries both

// ---- Battery: half the cell voltage on GPIO13; GPIO1 LOW when USB power is in ------
#define PIN_BAT_ADC    13
#define PIN_EXT_PWR    1

// ---- Where MeshCore's M9 build keeps its store --------------------------------------
// Its board uses default_16MB.csv like the T-Deck: SPIFFS at 0xC90000, 0x360000 long,
// read out before formatting on the first start (fsmigrate.h).
#define MESHCORE_FS_OFFSET 0xC90000
#define MESHCORE_FS_SIZE   0x360000

// ---- Parts other boards have and this one doesn't ---------------------------------
#define ADDR_GT911_A   0x5D
#define ADDR_GT911_B   0x14
#define PIN_TOUCH_INT  (-1)
#define PIN_I2S_BCK    (-1)
#define PIN_I2S_WS     (-1)
#define PIN_I2S_DOUT   (-1)
#define PIN_I2S_MCLK   (-1)
#define PIN_NFC_CS     (-1)
#define PIN_NFC_INT    (-1)
#define PIN_EXT_IO0    (-1)
#define PIN_RTC_INT    (-1)
#define EXP_DRV_EN     0
#define EXP_AMP_EN     1
#define EXP_KB_RST     2
#define EXP_LORA_EN    3
#define EXP_GPS_EN     4
#define EXP_NFC_EN     5
#define EXP_GPS_RST    7
#define EXP_KB_EN      8
#define EXP_GPIO_EN    9
#define EXP_SD_DET     10
#define EXP_SD_PULLEN  11
#define EXP_SD_EN      12
