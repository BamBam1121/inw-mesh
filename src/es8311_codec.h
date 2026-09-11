// ES8311, playback only. The ESP32 is I2S master with MCLK at 256 x fs.
// Register sequence follows Espressif's es8311 driver as used on this board by
// Wadamesh (GPL-3.0); begin() does the power/reference setup once, and
// start()/stop() bracket each sound.

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include "board_pins.h"

class Es8311 {
public:
  static constexpr uint32_t SAMPLE_RATE = 16000;
  static constexpr i2s_port_t PORT = I2S_NUM_0;

  bool begin(TwoWire& w = Wire) {
    _w = &w;
    _w->beginTransmission(ADDR_ES8311_CODEC);
    if (_w->endTransmission() != 0) return false;
    bool ok = true;
    ok &= wr(0x44, 0x08); ok &= wr(0x44, 0x08);   // noise immunity (first write can miss)
    ok &= wr(0x01, 0x30); ok &= wr(0x02, 0x00); ok &= wr(0x03, 0x10);
    ok &= wr(0x16, 0x24); ok &= wr(0x04, 0x10); ok &= wr(0x05, 0x00);
    ok &= wr(0x0B, 0x00); ok &= wr(0x0C, 0x00); ok &= wr(0x10, 0x1F);
    ok &= wr(0x11, 0x7F);
    ok &= wr(0x00, 0x80);                           // out of reset, slave
    ok &= wr(0x01, 0x3F);                           // clocks from the MCLK pin
    ok &= wr(0x13, 0x10); ok &= wr(0x1B, 0x0A); ok &= wr(0x1C, 0x6A);
    ok &= wr(0x44, 0x58);                           // internal reference
    _ok = ok;
    return ok;
  }
  bool ok() const { return _ok; }

  // Install I2S and power the DAC path. Pair with stop().
  bool start() {
    if (!_ok) return false;
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
    cfg.tx_desc_auto_clear = true;
    if (i2s_driver_install(PORT, &cfg, 0, nullptr) != ESP_OK) return false;
    i2s_pin_config_t pins = {};
    pins.mck_io_num = PIN_I2S_MCLK;
    pins.bck_io_num = PIN_I2S_BCK;
    pins.ws_io_num = PIN_I2S_WS;
    pins.data_out_num = PIN_I2S_DOUT;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(PORT, &pins) != ESP_OK) { i2s_driver_uninstall(PORT); return false; }
    wr(0x00, 0x80); wr(0x01, 0x3F);
    wr(0x09, 0x0C); wr(0x0A, 0x0C);                 // I2S, 16-bit
    wr(0x02, 0x00); wr(0x05, 0x00); wr(0x03, 0x10);
    wr(0x04, 0x20);                                 // DAC OSR for 16 kHz
    wr(0x07, 0x00); wr(0x08, 0xFF); wr(0x06, 0x03);
    wr(0x17, 0xBF); wr(0x0E, 0x02); wr(0x12, 0x00); wr(0x14, 0x1A);
    wr(0x0D, 0x01); wr(0x15, 0x40); wr(0x37, 0x08); wr(0x45, 0x00);
    return true;
  }

  void stop() {
    setMute(true);
    wr(0x32, 0x00); wr(0x17, 0x00); wr(0x0E, 0xFF); wr(0x12, 0x02); wr(0x14, 0x00);
    wr(0x0D, 0xFA); wr(0x15, 0x00); wr(0x02, 0x10); wr(0x00, 0x00); wr(0x00, 0x1F);
    wr(0x01, 0x30); wr(0x01, 0x00); wr(0x45, 0x00); wr(0x0D, 0xFC); wr(0x02, 0x00);
    i2s_zero_dma_buffer(PORT);
    i2s_driver_uninstall(PORT);
  }

  void setMute(bool m) {
    uint8_t v = rd(0x31) & 0x9F;
    wr(0x31, m ? (v | 0x60) : v);
  }

  // 0..100. The floor is the quietest level still clearly audible.
  void setVolumePercent(uint8_t pct) {
    if (pct > 100) pct = 100;
    wr(0x32, pct ? (uint8_t)(0x60 + (uint32_t)(0xFF - 0x60) * pct / 100) : 0);
  }

  void write(const int16_t* mono, size_t n) {
    size_t bw = 0;
    i2s_write(PORT, mono, n * sizeof(int16_t), &bw, pdMS_TO_TICKS(200));
  }

private:
  bool wr(uint8_t reg, uint8_t v) {
    _w->beginTransmission(ADDR_ES8311_CODEC);
    _w->write(reg); _w->write(v);
    return _w->endTransmission() == 0;
  }
  uint8_t rd(uint8_t reg) {
    _w->beginTransmission(ADDR_ES8311_CODEC);
    _w->write(reg);
    if (_w->endTransmission(false) != 0) return 0;
    if (_w->requestFrom((uint8_t)ADDR_ES8311_CODEC, (uint8_t)1) != 1) return 0;
    return _w->read();
  }
  TwoWire* _w = nullptr;
  bool _ok = false;
};
