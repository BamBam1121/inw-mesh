// ThinkNode M9 display: ST7789 2.4" IPS, 240x320, on the shared SPI bus (radio, SD).
// Brightness is left to backlight.h (PWM on PIN_TFT_BL, active LOW).
//
// Rotation 1 is the landscape the M9 is held in, keyboard at the bottom: MADCTL MV|MX,
// the same as the T-Deck. Wadamesh found that on a real M9 (Adafruit rotation 1; 3 was
// upside down). MeshCore also turns inversion on (INVON), as the T-Deck needs.

#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "board_pins.h"

#define TFT_ROTATION 1

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;

public:
    LGFX() {
        { // shared SPI bus
            auto c = _bus.config();
            c.spi_host    = SPI2_HOST;
            c.spi_mode    = 0;
            c.freq_write  = 40000000;   // the ST7789's rated speed; MeshCore's M9 build runs it at 80 MHz,
                                        // the T-Deck here does too - try that once someone has one in hand
            c.freq_read   = 16000000;
            c.dma_channel = SPI_DMA_CH_AUTO;
            c.pin_sclk    = PIN_SPI_SCK;
            c.pin_mosi    = PIN_SPI_MOSI;
            c.pin_miso    = PIN_SPI_MISO;
            c.pin_dc      = PIN_TFT_DC;
            _bus.config(c);
            _panel.setBus(&_bus);
        }
        { // ST7789 panel
            auto c = _panel.config();
            c.pin_cs          = PIN_TFT_CS;
            c.pin_rst         = PIN_TFT_RST;
            c.pin_busy        = -1;
            c.memory_width    = 240;
            c.memory_height   = 320;
            c.panel_width     = 240;
            c.panel_height    = 320;
            c.offset_x        = 0;
            c.offset_y        = 0;
            c.offset_rotation = 0;
            c.readable        = true;
            c.invert          = true;
            c.rgb_order       = false;
            c.bus_shared      = true;   // LoRa + SD share this bus
            _panel.config(c);
        }
        setPanel(&_panel);
    }
    void writeFreq(uint32_t hz) { auto c = _bus.config(); c.freq_write = hz; _bus.config(c); }
    uint32_t writeFreq() const { return _bus.config().freq_write; }
};
