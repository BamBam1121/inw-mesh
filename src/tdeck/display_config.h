// T-Deck display: ST7789 2.8" IPS, 240x320, on the shared SPI bus (radio, SD).
// Brightness is left to backlight.h (PWM on PIN_TFT_BL).
//
// Rotation 1 is the landscape the T-Deck is held in, keyboard at the bottom.
// Checked against the boards that run on real T-Decks: LilyGo's examples use
// TFT_eSPI rotation 1, Wadamesh uses Adafruit rotation 3, and all three are the
// same register value (MX|MV) as LovyanGFX's rotation 1.

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
            c.freq_write  = 80000000;   // Halves the send (34 -> 19 ms a frame): with the overlapped push
                                        // (board_pins.h BOARD_ASYNC_PUSH) scrolling goes ~27 -> ~36 fps, the
                                        // pager's range. It is past the ST7789's 62.5 MHz rating, and the owner's
                                        // unit showed rainbow lines after a day on battery at 80 MHz once (2026-10-09)
                                        // - but that was traced to the async push sharing the bus without a lock,
                                        // which is now fixed (ui.cpp pushTask). Back at 80 MHz 2026-10-11 to be
                                        // carried a day on battery; if the lines return, drop to 40000000.
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
            c.invert          = true;   // IPS: Adafruit's ST7789 init turns inversion on too
            c.rgb_order       = false;
            c.bus_shared      = true;   // LoRa + SD share this bus
            _panel.config(c);
        }
        setPanel(&_panel);
    }
    // The speed pictures are sent at, changed on the spot (it takes effect with the next thing drawn).
    void writeFreq(uint32_t hz) { auto c = _bus.config(); c.freq_write = hz; _bus.config(c); }
    uint32_t writeFreq() const { return _bus.config().freq_write; }
};
