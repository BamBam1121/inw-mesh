// Trackball pulse counting for rotary.h (T-Deck).
#include <Arduino.h>
#include "board_pins.h"

namespace tdeck_tb {

static volatile int32_t s_dx = 0, s_dy = 0;         // right and down are positive
static volatile uint32_t s_n[4] = {0, 0, 0, 0};   // up, down, left, right: for the hardware check
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR step(volatile int32_t& axis, int by) { portENTER_CRITICAL_ISR(&s_mux); axis = axis + by; portEXIT_CRITICAL_ISR(&s_mux); }
static void IRAM_ATTR up()    { s_n[0] = s_n[0] + 1; step(s_dy, -1); }
static void IRAM_ATTR down()  { s_n[1] = s_n[1] + 1; step(s_dy, 1); }
static void IRAM_ATTR left()  { s_n[2] = s_n[2] + 1; step(s_dx, -1); }
static void IRAM_ATTR right() { s_n[3] = s_n[3] + 1; step(s_dx, 1); }

void attach() {
  for (int p : { PIN_TB_UP, PIN_TB_DOWN, PIN_TB_LEFT, PIN_TB_RIGHT }) pinMode(p, INPUT_PULLUP);
  // Each step of the ball flips its line: both edges are steps. Counting only the
  // falling one took every other step, so the ball had to be rolled twice as far.
  attachInterrupt(digitalPinToInterrupt(PIN_TB_DOWN),  down,  CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_TB_RIGHT), right, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_TB_UP),    up,    CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_TB_LEFT),  left,  CHANGE);
}

void take(int32_t& dx, int32_t& dy) {
  portENTER_CRITICAL(&s_mux);
  dx = s_dx; dy = s_dy;
  s_dx = 0; s_dy = 0;
  portEXIT_CRITICAL(&s_mux);
}

void counts(uint32_t out[4]) { for (int i = 0; i < 4; i++) out[i] = s_n[i]; }

}  // namespace tdeck_tb
