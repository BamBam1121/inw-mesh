// T-Lora Pager pin map, cross-checked against Meshtastic's tlora-pager variant.

#pragma once

// ---- Shared SPI bus (LoRa + display + microSD all live here) ----------------
#define PIN_SPI_SCK    35
#define PIN_SPI_MOSI   34
#define PIN_SPI_MISO   33

// ---- I2C bus (keyboard, RTC, IMU, charger, fuel gauge, codec, GPIO expander) -
// 400 kHz normally. LilyGo raises it to 1 MHz around BHI260AP init only.
#define PIN_I2C_SDA    3
#define PIN_I2C_SCL    2

// ---- LoRa radio (SX1262, shared SPI bus) --------------------------------------
#define PIN_LORA_CS    36
#define PIN_LORA_DIO1  14   // IRQ
#define PIN_LORA_BUSY  48   // (Meshtastic labels this LORA_DIO2)
#define PIN_LORA_RST   47
#define LORA_TCXO_V    3.0f // DIO3 drives the TCXO at 3.0 V

// ---- Display: ST7796, 2.33" IPS 480x222 (on the shared SPI bus) -------------
#define PIN_TFT_CS     38
#define PIN_TFT_DC     37   // RS
#define PIN_TFT_RST    (-1) // tied to board reset
#define PIN_TFT_BL     42   // NOT a PWM LED pin: drives an AW9364, see backlight.h
#define TFT_PANEL_W    222  // native width  (portrait); we rotate to landscape
#define TFT_PANEL_H    480  // native height
#define TFT_OFFSET_X   49
#define TFT_OFFSET_Y   0
#define TFT_ROTATION   3

// ---- microSD (shared SPI bus) -----------------------------------------------
#define PIN_SD_CS      21

// ---- Rotary encoder + button ------------------------------------------------
#define PIN_ROTARY_A   40
#define PIN_ROTARY_B   41
#define PIN_ROTARY_PRESS 7
#define PIN_BUTTON     0

// ---- QWERTY keyboard (TCA8418 over I2C) -------------------------------------
// 4x10 matrix, no arrow keys and no Esc. The rotary encoder is the only directional
// input on the board, so it carries navigation. Keymap is in docs/hardware.md.
#define PIN_KB_BL      46   // ordinary PWM, unlike the display backlight
#define PIN_KB_INT     6
#define KB_MATRIX_ROWS 4
#define KB_MATRIX_COLS 10

// ---- GPS (u-blox MIA-M10Q, UART) --------------------------------------------
#define PIN_GPS_RX     4
#define PIN_GPS_TX     12
#define PIN_GPS_PPS    13
#define GPS_BAUD       38400

// ---- Audio codec ES8311 (I2S) -----------------------------------------------
#define PIN_I2S_BCK    11
#define PIN_I2S_WS     18
#define PIN_I2S_DOUT   45
#define PIN_I2S_DIN    17
#define PIN_I2S_MCLK   10

// ---- NFC ST25R3916 (SPI, on the shared bus, not I2C) ------------------------
#define PIN_NFC_INT    5
#define PIN_NFC_CS     39

// ---- Interrupts from I2C parts ----------------------------------------------
#define PIN_RTC_INT    1
#define PIN_IMU_INT    8

// ---- External 12-pin socket -------------------------------------------------
#define PIN_EXT_IO0    9
#define PIN_EXT_IO1    43
#define PIN_EXT_IO2    44

// ---- I2C device addresses ---------------------------------------------------
#define ADDR_ES8311_CODEC  0x18
#define ADDR_XL9555_EXP    0x20
#define ADDR_BHI260AP_IMU  0x28   // 0x29 if a board revision straps SA0 high
#define ADDR_TCA8418_KB    0x34
#define ADDR_RTC_PCF85063  0x51
#define ADDR_BQ27220_GAUGE 0x55
#define ADDR_DRV2605_HAPT  0x5A
#define ADDR_BQ25896_CHG   0x6B

// ---- XL9555 GPIO expander: power rails + resets are behind this chip ---------
// You MUST enable the relevant rail over I2C before the peripheral responds.
// These are EXPANDER pin numbers (0-15 on the XL9555), NOT ESP32 GPIOs.
//
// Linear numbering, matching Meshtastic's variant. LilyGo's documentation labels
// the upper port P1.x and prints them as "10, 11, 12, 14"; those are the same pins
// as 8, 9, 10, 12 here. Do not "correct" these to LilyGo's numbers or you break the
// keyboard, SD and expansion socket together. See docs/hardware.md.
#define EXP_DRV_EN     0   // haptic driver enable
#define EXP_AMP_EN     1   // audio amp enable
#define EXP_KB_RST     2   // keyboard reset
#define EXP_LORA_EN    3   // radio power, switched on in setup() before radio_init()
#define EXP_GPS_EN     4
#define EXP_NFC_EN     5
#define EXP_GPS_RST    7
#define EXP_KB_EN      8
#define EXP_GPIO_EN    9    // external 12-pin socket
#define EXP_SD_DET     10
#define EXP_SD_PULLEN  11
#define EXP_SD_EN      12
