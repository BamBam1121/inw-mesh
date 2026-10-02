// T-Deck sound: a MAX98357A class-D amp straight on I2S (BCK 7, WS 5, DOUT 6).
// No codec to configure and no MCLK. It keeps the name and interface of the
// pager's ES8311 driver so the jingle player works unchanged; volume, which the
// codec did in hardware, is done here by scaling the samples.
//
// I2S is installed per sound and removed after (start/stop), as on the pager:
// Wadamesh found a resident driver held ~2 KB of internal DMA RAM that other
// things run short of.

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include "board_pins.h"

class Es8311 {
public:
  static constexpr uint32_t   SAMPLE_RATE = 16000;
  // How much sound waits in the amp's queue. Whoever feeds it can be held up for this
  // long (the flash being written, Wi-Fi starting) before the tune has a hole. It is
  // 8 KB of DMA memory, held only while a sound plays.
  static constexpr int QUEUE_BUFS = 8, QUEUE_LEN = 512;      // 256 ms (it was 64: start-up left holes in the tune)
  static constexpr uint32_t QUEUE_MS = (uint32_t)QUEUE_BUFS * QUEUE_LEN * 1000 / SAMPLE_RATE;
  static constexpr i2s_port_t PORT = I2S_NUM_0;

  bool begin(TwoWire& = Wire) { _ok = true; return true; }
  bool ok() const { return _ok; }

  bool start() {
    if (!_ok) return false;
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;      // the amp is mono
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.dma_buf_count = QUEUE_BUFS;
    cfg.dma_buf_len = QUEUE_LEN;
    cfg.tx_desc_auto_clear = true;
    if (i2s_driver_install(PORT, &cfg, 0, nullptr) != ESP_OK) return false;
    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = PIN_I2S_BCK;
    pins.ws_io_num = PIN_I2S_WS;
    pins.data_out_num = PIN_I2S_DOUT;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(PORT, &pins) != ESP_OK) { i2s_driver_uninstall(PORT); return false; }
    return true;
  }

  void stop() {
    i2s_zero_dma_buffer(PORT);
    i2s_driver_uninstall(PORT);
  }

  void setMute(bool m) { _muted = m; }

  // 0..100, on a curve that sounds even: the amp itself is fixed-gain and loud.
  void setVolumePercent(uint8_t pct) {
    if (pct > 100) pct = 100;
    _gain = (uint16_t)((uint32_t)pct * pct * 256 / 10000);   // 0..256
  }

  uint8_t reg(uint8_t) { return 0; }   // no codec registers here

  void write(const int16_t* mono, size_t n) {
    int16_t buf[128];
    size_t done = 0;
    while (done < n) {
      const size_t k = min(n - done, (size_t)128);
      for (size_t i = 0; i < k; i++) buf[i] = _muted ? 0 : (int16_t)((int32_t)mono[done + i] * _gain / 256);
      size_t bw = 0;
      i2s_write(PORT, buf, k * sizeof(int16_t), &bw, pdMS_TO_TICKS(200));
      done += k;
    }
  }

private:
  bool     _ok = false;
  bool     _muted = true;
  uint16_t _gain = 92;   // ~60% until the app sets it
};
