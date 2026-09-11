// Notification tones. Each jingle runs on its own short-lived task so the UI
// loop never waits on I2S, and the amp is only powered while it plays.

#pragma once
#include <Arduino.h>
#include <math.h>
#include "es8311_codec.h"

struct ToneStep { uint16_t freq; uint16_t ms; };   // freq 0 = rest

struct Jingle {
  const char*     name;
  const ToneStep* steps;
  uint8_t         count;
};

namespace jingles {
  static const ToneStep MSG_STEPS[]   = {{880, 90}, {1318, 110}};              // two-note chime
  static const ToneStep DM_STEPS[]    = {{1047, 90}, {1568, 120}};             // rising fifth
  static const ToneStep ALERT_STEPS[] = {{1318, 70}, {1760, 70}, {2349, 130}}; // @mention arpeggio
  static const ToneStep BOOT_STEPS[]  = {{523, 90}, {659, 90}, {784, 140}};    // C-E-G

  static const Jingle MSG   = {"msg",   MSG_STEPS,   2};
  static const Jingle DM    = {"dm",    DM_STEPS,    2};
  static const Jingle ALERT = {"alert", ALERT_STEPS, 3};
  static const Jingle BOOT  = {"boot",  BOOT_STEPS,  3};
}

class JinglePlayer {
public:
  bool begin(Es8311* codec) { _codec = codec; return codec && codec->ok(); }
  void setAmp(void (*fn)(bool on)) { _amp = fn; }
  void setVolume(uint8_t pct) { _vol = pct; }

  // Ignored while one is already playing: two chimes at once is just noise.
  void play(const Jingle* j) {
    if (!j || !_codec || !_codec->ok() || _busy || !_vol) return;
    _busy = true;
    _j = j;
    if (xTaskCreatePinnedToCore(task, "jingle", 4096, this, 2, nullptr, 0) != pdPASS) _busy = false;
  }

  void tick() {}
  bool playing() const { return _busy; }

private:
  static void task(void* arg) {
    JinglePlayer* p = (JinglePlayer*)arg;
    if (p->_amp) p->_amp(true);
    if (p->_codec->start()) {
      p->_codec->setVolumePercent(p->_vol);
      p->_codec->setMute(false);
      for (uint8_t i = 0; i < p->_j->count; i++) p->tone(p->_j->steps[i].freq, p->_j->steps[i].ms);
      p->tone(0, 40);                    // let the last tone drain before power-down
      p->_codec->stop();
    }
    if (p->_amp) p->_amp(false);
    p->_busy = false;
    vTaskDelete(nullptr);
  }

  void tone(uint16_t freq, uint16_t ms) {
    const int total = (int)(Es8311::SAMPLE_RATE * ms / 1000);
    const int fade = max(1, total / 8);
    const float step = 2.0f * PI * freq / Es8311::SAMPLE_RATE;
    int16_t buf[128];
    int done = 0;
    float phase = 0;
    while (done < total) {
      int n = 0;
      for (; n < 128 && done < total; n++, done++) {
        float a = freq ? 22000.0f : 0.0f;
        if (done < fade) a *= (float)done / fade;
        else if (done > total - fade) a *= (float)(total - done) / fade;
        buf[n] = (int16_t)(sinf(phase) * a);
        phase += step;
        if (phase > 2.0f * PI) phase -= 2.0f * PI;
      }
      _codec->write(buf, n);
    }
  }

  Es8311* _codec = nullptr;
  void (*_amp)(bool) = nullptr;
  const Jingle* _j = nullptr;
  volatile bool _busy = false;
  uint8_t _vol = 60;
};
