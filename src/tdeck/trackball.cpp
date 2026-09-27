// Trackball pulse counting for rotary.h (T-Deck).
#include <Arduino.h>
#include "board_pins.h"

namespace tdeck_tb {

static volatile int32_t s_steps = 0;
static volatile uint32_t s_n[4] = {0, 0, 0, 0};   // up, down, left, right: for the hardware check
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR fwd()  { portENTER_CRITICAL_ISR(&s_mux); s_steps = s_steps + 1; portEXIT_CRITICAL_ISR(&s_mux); }
static void IRAM_ATTR back() { portENTER_CRITICAL_ISR(&s_mux); s_steps = s_steps - 1; portEXIT_CRITICAL_ISR(&s_mux); }
static void IRAM_ATTR up()    { s_n[0] = s_n[0] + 1; back(); }
static void IRAM_ATTR down()  { s_n[1] = s_n[1] + 1; fwd(); }
static void IRAM_ATTR left()  { s_n[2] = s_n[2] + 1; back(); }
static void IRAM_ATTR right() { s_n[3] = s_n[3] + 1; fwd(); }

void attach() {
  for (int p : { PIN_TB_UP, PIN_TB_DOWN, PIN_TB_LEFT, PIN_TB_RIGHT }) pinMode(p, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_TB_DOWN),  down,  FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_TB_RIGHT), right, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_TB_UP),    up,    FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_TB_LEFT),  left,  FALLING);
}

int32_t take() {
  portENTER_CRITICAL(&s_mux);
  const int32_t s = s_steps;
  s_steps = 0;
  portEXIT_CRITICAL(&s_mux);
  return s;
}

void counts(uint32_t out[4]) { for (int i = 0; i < 4; i++) out[i] = s_n[i]; }

}  // namespace tdeck_tb
