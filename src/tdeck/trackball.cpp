// Trackball pulse counting for rotary.h (T-Deck).
#include <Arduino.h>
#include "board_pins.h"

namespace tdeck_tb {

static volatile int32_t s_steps = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR fwd()  { portENTER_CRITICAL_ISR(&s_mux); s_steps = s_steps + 1; portEXIT_CRITICAL_ISR(&s_mux); }
static void IRAM_ATTR back() { portENTER_CRITICAL_ISR(&s_mux); s_steps = s_steps - 1; portEXIT_CRITICAL_ISR(&s_mux); }

void attach() {
  for (int p : { PIN_TB_UP, PIN_TB_DOWN, PIN_TB_LEFT, PIN_TB_RIGHT }) pinMode(p, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_TB_DOWN),  fwd,  FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_TB_RIGHT), fwd,  FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_TB_UP),    back, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_TB_LEFT),  back, FALLING);
}

int32_t take() {
  portENTER_CRITICAL(&s_mux);
  const int32_t s = s_steps;
  s_steps = 0;
  portEXIT_CRITICAL(&s_mux);
  return s;
}

}  // namespace tdeck_tb
