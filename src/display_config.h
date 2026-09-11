// ST7796 on the shared SPI bus. The backlight is deliberately left to
// backlight.h: PIN_TFT_BL drives a pulse-counting AW9364, which LovyanGFX's
// PWM light class would drive wrong.

#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "board_pins.h"

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7796 _panel;
    lgfx::Bus_SPI      _bus;

public:
    LGFX() {
        { // shared SPI bus
            auto c = _bus.config();
            c.spi_host    = SPI2_HOST;
            c.spi_mode    = 0;
            c.freq_write  = 80000000;   // Meshtastic runs this ST7796 ~75MHz; 80 is a clean divider
            c.freq_read   = 16000000;
            c.dma_channel = SPI_DMA_CH_AUTO;   // stream sprite pushes via DMA; without
                                               // this the CPU blocks per frame and the
                                               // full-screen push tears across refreshes
            c.pin_sclk    = PIN_SPI_SCK;
            c.pin_mosi    = PIN_SPI_MOSI;
            c.pin_miso    = PIN_SPI_MISO;
            c.pin_dc      = PIN_TFT_DC;
            _bus.config(c);
            _panel.setBus(&_bus);
        }
        { // ST7796 panel
            auto c = _panel.config();
            c.pin_cs         = PIN_TFT_CS;
            c.pin_rst        = PIN_TFT_RST;
            c.pin_busy       = -1;
            // The ST7796's own memory is 320x480 and offset_x 49 + 222 = 271 has
            // to land inside it. 240 here silently clips the right of the panel.
            c.memory_width   = 320;
            c.memory_height  = 480;
            c.panel_width    = TFT_PANEL_W;   // 222
            c.panel_height   = TFT_PANEL_H;   // 480
            c.offset_x       = TFT_OFFSET_X;  // 49
            c.offset_y       = TFT_OFFSET_Y;  // 0
            c.offset_rotation = 0;
            c.readable       = true;
            c.invert         = true;          // IPS panels usually need inversion
            c.rgb_order      = false;
            c.bus_shared     = true;          // LoRa + SD share this bus
            _panel.config(c);
        }
        setPanel(&_panel);
    }
};
