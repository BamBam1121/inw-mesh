// LilyGo T-Deck / T-Deck Plus pin map.
//
// Nobody here owns one: every value below is cross-checked against boards that
// run on real T-Decks - MeshCore's lilygo_tdeck variant and Wadamesh's T-Deck
// build (its reference device), with LilyGo's published pin list. Where they
// disagree, the one proven on hardware wins and the reason is noted.

#pragma once

#define BOARD_NAME     "T-Deck"

// ---- What this board has. Shared code checks these, never the board name. ----
#define SCREEN_W               320
#define SCREEN_H               240
#define BOARD_HAS_SIDE_BUTTON  0    // GPIO0 is the trackball click; the power switch is mechanical
#define BOARD_HAS_POWER_OFF    0    // nothing can switch it off from software
#define BOARD_HAS_HAPTIC       0
#define BOARD_HAS_RTC          0    // no battery-backed clock: time comes from GPS, Wi-Fi or the phone
#define BOARD_HAS_CHARGER_IC   0    // no charge controller or fuel gauge on I2C: battery is an ADC reading
#define BOARD_HAS_EXT_HEADER   0    // no 12-pin header
#define BOARD_HAS_KB_BACKLIGHT 1    // set through the keyboard's own controller
#define BOARD_HAS_TOUCH        1    // GT911 capacitive touch: the main input on this board
#define BOARD_ROW_H            28   // menu rows sized for a finger (5 mm), not a wheel
#define BOARD_HOME_DASHBOARD   1    // home is src/tdeck/dashboard.cpp, not the wheel carousel

// Wi-Fi updates come from firmware/t-deck/ and must carry a signature naming
// "t-deck" (ota.cpp): pager firmware can never be installed on a T-Deck this way.
#define OTA_SUBDIR             "t-deck/"
#define OTA_BOARD              "t-deck"
// Crashes and errors go back to the developer over Wi-Fi (src/tdeck/bugreport.h).
#define BOARD_HAS_REPORTS      1
#define REPORT_BOARD           "t-deck"

// ---- Power: one switch for the keyboard, radio, display and SD --------------------
#define PIN_POWER_ON   10

// ---- Shared SPI bus: LoRa, display and microSD ----------------------------------
#define PIN_SPI_SCK    40
#define PIN_SPI_MOSI   41
#define PIN_SPI_MISO   38

// ---- I2C: keyboard controller (0x55) and GT911 touch (0x5D or 0x14) -------------
#define PIN_I2C_SDA    18
#define PIN_I2C_SCL    8

// ---- LoRa radio (SX1262) --------------------------------------------------------
#define PIN_LORA_CS    9
#define PIN_LORA_DIO1  45
#define PIN_LORA_BUSY  13
#define PIN_LORA_RST   17

// ---- Display: ST7789, 2.8" IPS, 240x320 portrait, run landscape -----------------
#define PIN_TFT_CS     12
#define PIN_TFT_DC     11
#define PIN_TFT_RST    (-1)
#define PIN_TFT_BL     42   // plain PWM here (Wadamesh dims it at 20 kHz), unlike the pager's AW9364

// ---- microSD (shared SPI bus) ---------------------------------------------------
#define PIN_SD_CS      39

// ---- Trackball: four pulse lines and a click ------------------------------------
// Named for the screen as the UI draws it (landscape, keyboard at the bottom).
// Wadamesh measured on a T-Deck that the raw lines are inverted on both axes
// against that picture: its "up" line (15) moves the cursor down, and so on.
#define PIN_TB_DOWN    15
#define PIN_TB_UP      3
#define PIN_TB_RIGHT   2
#define PIN_TB_LEFT    1
#define PIN_TB_CLICK   0    // also the BOOT button

// Shared code starts "the wheel" and reads "the side button" by these names.
#define PIN_ROTARY_A     PIN_TB_UP
#define PIN_ROTARY_B     PIN_TB_DOWN
#define PIN_ROTARY_PRESS PIN_TB_CLICK
#define PIN_BUTTON       PIN_TB_CLICK

// ---- Keyboard: a separate ESP32-C3 answering on I2C -----------------------------
#define ADDR_TDECK_KB  0x55
#define PIN_KB_INT     46
#define PIN_KB_BL      (-1) // no GPIO: the backlight is a command to the keyboard controller

// ---- Touch (not used yet: the trackball and keys drive the UI) -------------------
#define ADDR_GT911_A   0x5D
#define ADDR_GT911_B   0x14
#define PIN_TOUCH_INT  16

// ---- GPS (T-Deck Plus only), UART ------------------------------------------------
#define PIN_GPS_RX     44   // ESP32 receives here
#define PIN_GPS_TX     43
#define PIN_GPS_PPS    (-1)
#define GPS_BAUD       38400
#define GPS_BAUD_ALT   9600    // some T-Deck Plus batches have a GPS module at 9600; gps.h tries both

// ---- Speaker: MAX98357A class-D amp on I2S, no codec, no MCLK --------------------
#define PIN_I2S_BCK    7
#define PIN_I2S_WS     5
#define PIN_I2S_DOUT   6
#define PIN_I2S_MCLK   (-1)

// ---- Battery: half the cell voltage on GPIO4 -------------------------------------
#define PIN_BAT_ADC    4

// ---- Where MeshCore's T-Deck build keeps its store ----------------------------------
// Its board file (boards/t-deck.json) uses the core's default_16MB.csv: a SPIFFS
// partition at 0xC90000, 0x360000 long, inside ours (partitions_inw.csv). The first
// start reads the identity, contacts and channels out before formatting (fsmigrate.h).
#define MESHCORE_FS_OFFSET 0xC90000
#define MESHCORE_FS_SIZE   0x360000

// ---- Parts the pager has and this board doesn't ----------------------------------
// Shared code still names them; these values are never driven on a T-Deck.
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
