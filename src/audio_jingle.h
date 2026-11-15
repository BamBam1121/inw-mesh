// Notification tones. Each jingle runs on its own short-lived task so the UI
// loop never waits on I2S, and the amp is only powered while it plays.

#pragma once
#include <Arduino.h>
#include <math.h>
#include "es8311_codec.h"

// WAVE_CHIME: a music box / celesta (the note with its octave and twelfth over it).
// WAVE_JINGLE: sleigh bells - a metal bell's out-of-tune overtones and a hiss, shaken
// fourteen times a second, each shake ringing out before the next.
enum : uint8_t { WAVE_SINE = 0, WAVE_SQUARE, WAVE_TRIANGLE, WAVE_CHIME, WAVE_JINGLE };

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

  // The task runs on core 1, beside the screen loop, not on core 0 with the radio stacks:
  // Wi-Fi starting up there held it off for 433 ms (measured), a hole in the start-up tune
  // just as the lock screen came up. On core 1 the same start-up feeds it within 32 ms.
  // Ignored while one is already playing: two chimes at once is just noise.
  void play(const Jingle* j) {
    if (!j || !_codec || !_codec->ok() || _busy || !_vol) return;
    _busy = true;
    _j = j;
    if (xTaskCreatePinnedToCore(task, "jingle", 4096, this, 5, nullptr, 1) != pdPASS) { _busy = false; _fail = 1; }
  }

  // Why the last sound didn't play (1: no task, 2: I2S wouldn't start), once,
  // for the log: a sound that fails is otherwise just silence.
  uint8_t takeFailure() { const uint8_t f = _fail; _fail = 0; return f; }
  void tick() {}
  bool playing() const { return _busy; }

private:
  static void task(void* arg) {
    JinglePlayer* p = (JinglePlayer*)arg;
    if (p->_amp) p->_amp(true);
    if (p->_codec->start()) {
      p->_codec->setVolumePercent(p->_vol);
      p->_codec->setMute(false);
      p->tone({0, 60});                  // the amp needs a moment after power-up, or the first note is clipped
      p->_late = 0; p->_fed = millis();
      for (uint8_t i = 0; i < p->_j->count; i++) p->tone(p->_j->steps[i]);
      // The speaker holds Es8311::QUEUE_MS of sound; fed later than that, it ran dry and
      // the tune had a hole in it. Say so, with how late: it is the only trace of one.
      const uint32_t late = p->_late;
      p->tone({0, (uint16_t)(40 + Es8311::QUEUE_MS)});   // let the last note play out before power-down
      p->_codec->stop();
      if (late > Es8311::QUEUE_MS) Serial.printf("[jingle] %s had a gap: fed %lu ms late (the speaker holds %u ms)\n", p->_j->name, (unsigned long)late, (unsigned)Es8311::QUEUE_MS);
      else Serial.printf("[jingle] %s played through (latest feed %lu ms, the speaker holds %u ms)\n", p->_j->name, (unsigned long)late, (unsigned)Es8311::QUEUE_MS);
    } else p->_fail = 2;
    if (p->_amp) p->_amp(false);
    p->_busy = false;
    vTaskDelete(nullptr);
  }

  float sample(float phase) const {
    switch (_j->wave) {
      case WAVE_SQUARE:   return phase < PI ? 0.45f : -0.45f;   // squares are loud; tame them
      case WAVE_TRIANGLE: return phase < PI ? (2.0f * phase / PI - 1.0f) : (3.0f - 2.0f * phase / PI);
      case WAVE_CHIME:    return 0.62f * sinf(phase) + 0.26f * sinf(2 * phase) + 0.12f * sinf(3 * phase);
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
          float v;
          if (_j->wave == WAVE_JINGLE) {                // its overtones don't line up, so each keeps its own phase
            _phase2 += 2.0f * PI * f * 2.76f / sr; if (_phase2 > 2.0f * PI) _phase2 -= 2.0f * PI;
            _phase3 += 2.0f * PI * f * 5.40f / sr; if (_phase3 > 2.0f * PI) _phase3 -= 2.0f * PI;
            const float hit = fmodf((float)done / sr, 1.0f / 14.0f);       // since the last shake
            const float ring = expf(-hit * 45.0f);
            _noise = _noise * 1664525u + 1013904223u;
            const float hiss = (float)(int32_t)(_noise >> 8 << 8) / 2147483648.0f;
            v = (0.45f * sinf(_phase) + 0.3f * sinf(_phase2) + 0.15f * sinf(_phase3)) * (0.35f + 0.65f * ring) + hiss * 0.28f * ring;
          } else v = sample(_phase);
          if (_j->bell) {
            const float t = (float)done / sr;
            amp = 22000.0f * min(1.0f, t * 400.0f) * expf(-t * 6.0f);   // 2.5 ms attack, ring out
            if (done > total - fade) amp *= (float)(total - done) / fade;
          } else {
            amp = 22000.0f;
            if (done < fade) amp *= (float)done / fade;
            else if (done > total - fade) amp *= (float)(total - done) / fade;
          }
          buf[n] = (int16_t)(v * amp);
        } else {
          buf[n] = 0;
        }
      }
      _codec->write(buf, n);
      const uint32_t now = millis();     // how long since the speaker was last given sound
      if (now - _fed > _late) _late = now - _fed;
      _fed = now;
    }
  }

  Es8311* _codec = nullptr;
  void (*_amp)(bool) = nullptr;
  volatile uint8_t _fail = 0;
  const Jingle* _j = nullptr;
  volatile bool _busy = false;
  uint8_t _vol = 60;
  float _phase = 0, _phase2 = 0, _phase3 = 0;
  uint32_t _noise = 12345;
  uint32_t _fed = 0, _late = 0;
};
