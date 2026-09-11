// Notification tones. Each jingle runs on its own short-lived task so the UI
// loop never waits on I2S, and the amp is only powered while it plays.

#pragma once
#include <Arduino.h>
#include <math.h>
#include "es8311_codec.h"

enum : uint8_t { WAVE_SINE = 0, WAVE_SQUARE, WAVE_TRIANGLE };

// freq 0 is a rest. slideTo, when set, glides the pitch there over the step.
struct ToneStep {
  uint16_t freq, ms, slideTo;
  constexpr ToneStep(uint16_t f, uint16_t m, uint16_t s = 0) : freq(f), ms(m), slideTo(s) {}
};

struct Jingle {
  const char*     name;
  const ToneStep* steps;
  uint8_t         count;
  uint8_t         wave;
  bool            bell;     // piano/bell envelope: sharp attack, exponential decay
};

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
      for (uint8_t i = 0; i < p->_j->count; i++) p->tone(p->_j->steps[i]);
      p->tone({0, 40});                  // let the last note drain before power-down
      p->_codec->stop();
    }
    if (p->_amp) p->_amp(false);
    p->_busy = false;
    vTaskDelete(nullptr);
  }

  float sample(float phase) const {
    switch (_j->wave) {
      case WAVE_SQUARE:   return phase < PI ? 0.45f : -0.45f;   // squares are loud; tame them
      case WAVE_TRIANGLE: return phase < PI ? (2.0f * phase / PI - 1.0f) : (3.0f - 2.0f * phase / PI);
      default:            return sinf(phase);
    }
  }

  void tone(const ToneStep& s) {
    const int total = (int)(Es8311::SAMPLE_RATE * s.ms / 1000);
    const int fade = max(1, total / 8);
    const float sr = Es8311::SAMPLE_RATE;
    int16_t buf[128];
    int done = 0;
    while (done < total) {
      int n = 0;
      for (; n < 128 && done < total; n++, done++) {
        float amp = 0;
        if (s.freq) {
          const float f = s.slideTo ? s.freq + (s.slideTo - s.freq) * (float)done / total : s.freq;
          _phase += 2.0f * PI * f / sr;
          if (_phase > 2.0f * PI) _phase -= 2.0f * PI;
          if (_j->bell) {
            const float t = (float)done / sr;
            amp = 22000.0f * min(1.0f, t * 400.0f) * expf(-t * 6.0f);   // 2.5 ms attack, ring out
            if (done > total - fade) amp *= (float)(total - done) / fade;
          } else {
            amp = 22000.0f;
            if (done < fade) amp *= (float)done / fade;
            else if (done > total - fade) amp *= (float)(total - done) / fade;
          }
          buf[n] = (int16_t)(sample(_phase) * amp);
        } else {
          buf[n] = 0;
        }
      }
      _codec->write(buf, n);
    }
  }

  Es8311* _codec = nullptr;
  void (*_amp)(bool) = nullptr;
  const Jingle* _j = nullptr;
  volatile bool _busy = false;
  uint8_t _vol = 60;
  float _phase = 0;
};
