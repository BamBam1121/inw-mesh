// Touch gestures, for boards with a touchscreen (BOARD_HAS_TOUCH). The board's
// driver feeds raw samples - finger down at (x, y), or up - and this turns them
// into what screens act on: taps, long presses, drags and swipes. The pager has
// no touchscreen, so nothing here ever runs on it.
#pragma once
#include <Arduino.h>
#include <math.h>

struct TouchEvent {
  enum Type : uint8_t {
    Down,        // a finger landed
    Drag,        // it moved: dx, dy since the last Drag (or the Down)
    Up,          // it lifted (after a drag, or a long press)
    Tap,         // down and up again without moving much
    LongPress,   // held still for LONG_MS (fires while still down)
    Swipe,       // a quick flick: dir is 'L', 'R', 'U' or 'D'
  } type;
  int16_t x, y;          // where, in screen pixels (for Drag: where the finger is now)
  int16_t x0, y0;        // where the finger first landed
  int16_t dx, dy;        // Drag: movement since the last event
  char dir;              // Swipe
  bool coast;            // Drag: the list still gliding after the finger let go
};

class Gestures {
public:
  static constexpr int TAP_SLOP = 14;        // px a tap may wander (a fingertip rolls)
  static constexpr int SWIPE_MIN = 40;       // px a swipe must cover...
  static constexpr uint32_t SWIPE_MS = 350;  // ...within this long
  static constexpr uint32_t LONG_MS = 600;
  // A flick up or down keeps a list going after the finger lifts, slowing like a
  // phone's; touching it again stops it where it is, without opening anything.
  static constexpr float FLING_MIN = 0.35f;  // px/ms at the lift to glide at all
  static constexpr float FLING_MAX = 3.0f;   // px/ms, so one flick can't fly off
  static constexpr float COAST_STOP = 0.04f; // px/ms: done
  static constexpr float COAST_TAU = 420.0f; // ms for the speed to fall to ~37%
  static constexpr uint32_t STILL_MS = 90;   // a finger that stopped this long before lifting: no glide

  // Feed one sample; returns true and fills `out` when there's an event. Call
  // again straight away while it keeps returning true (a lift can produce Up
  // and then Tap or Swipe; a glide produces a Drag each loop).
  bool feed(bool down, int16_t x, int16_t y, uint32_t now, TouchEvent& out) {
    if (_queued) { out = _q; _queued = false; return true; }
    if (down && !_down) {
      _caught = _coasting;                   // a finger on a gliding list only stops it
      _coasting = false;
      _down = true; _moved = false; _long = false;
      _x0 = _lx = x; _y0 = _ly = y; _at = _moveAt = now; _vy = 0;
      return emit(out, TouchEvent::Down, x, y, 0, 0);
    }
    if (down && _down) {
      const int ddx = x - _lx, ddy = y - _ly;
      if (!_moved && (abs(x - _x0) > TAP_SLOP || abs(y - _y0) > TAP_SLOP)) _moved = true;
      if (!_moved && !_long && !_caught && now - _at >= LONG_MS) {
        _long = true;
        return emit(out, TouchEvent::LongPress, x, y, 0, 0);
      }
      if (_moved && (ddx || ddy)) {
        // Speed from the last few moves, so the lift carries how fast it was going.
        const uint32_t dt = now - _moveAt;
        if (dt > 0) {
          const float v = (float)ddy / (float)(dt < 8 ? 8 : dt);
          _vy = dt > 100 ? v : 0.5f * v + 0.5f * _vy;
        }
        _moveAt = now;
        _lx = x; _ly = y;
        return emit(out, TouchEvent::Drag, x, y, ddx, ddy);
      }
      return false;
    }
    if (!down && _down) {
      _down = false;
      const int tx = x >= 0 ? x : _lx, ty = y >= 0 ? y : _ly;
      if (!_moved) {
        if (_long || _caught) return emit(out, TouchEvent::Up, tx, ty, 0, 0);
        return emit(out, TouchEvent::Tap, _x0, _y0, 0, 0);
      }
      emit(out, TouchEvent::Up, tx, ty, 0, 0);
      const int ax = tx - _x0, ay = ty - _y0;
      if (now - _at <= SWIPE_MS && (abs(ax) >= SWIPE_MIN || abs(ay) >= SWIPE_MIN)) {
        _q = out;
        _q.type = TouchEvent::Swipe;
        _q.dir = abs(ax) > abs(ay) ? (ax > 0 ? 'R' : 'L') : (ay > 0 ? 'D' : 'U');
        _queued = true;
      }
      // Glide on a mostly-up-or-down flick that was still moving when it lifted.
      if (_coastOk && abs(ay) > abs(ax) && now - _moveAt < STILL_MS && fabsf(_vy) >= FLING_MIN) {
        _cv = _vy > FLING_MAX ? FLING_MAX : _vy < -FLING_MAX ? -FLING_MAX : _vy;
        _cacc = 0; _cAt = now; _coasting = true;
      }
      return true;
    }
    if (!down && _coasting) {
      const uint32_t dt = now - _cAt;
      if (dt < 10) return false;
      _cAt = now;
      _cacc += _cv * (float)dt;
      _cv *= expf(-(float)dt / COAST_TAU);
      if (fabsf(_cv) < COAST_STOP) _coasting = false;
      const int step = (int)_cacc;
      if (!step) return false;
      _cacc -= step;
      _ly += step;
      emit(out, TouchEvent::Drag, _lx, _ly, 0, step);
      out.coast = true;
      return true;
    }
    return false;
  }

  bool down() const { return _down; }
  bool coasting() const { return _coasting; }
  // Forget the Swipe a lift queued behind its Up: the Up already changed the screen.
  void dropQueued() { _queued = false; }
  // Stop a glide (a new screen came up), and whether the screen under the finger
  // glides at all (a map or the lock face follow the finger, they don't coast).
  void stopCoast() { _coasting = false; }
  void allowCoast(bool ok) { _coastOk = ok; if (!ok) _coasting = false; }

private:
  bool emit(TouchEvent& e, TouchEvent::Type t, int16_t x, int16_t y, int16_t dx, int16_t dy) {
    e.type = t; e.x = x; e.y = y; e.x0 = _x0; e.y0 = _y0; e.dx = dx; e.dy = dy; e.dir = 0; e.coast = false;
    return true;
  }
  bool _down = false, _moved = false, _long = false, _queued = false;
  bool _coasting = false, _caught = false, _coastOk = true;
  int16_t _x0 = 0, _y0 = 0, _lx = 0, _ly = 0;
  uint32_t _at = 0, _moveAt = 0, _cAt = 0;
  float _vy = 0, _cv = 0, _cacc = 0;
  TouchEvent _q{};
};
