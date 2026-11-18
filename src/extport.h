// The 12-pin header on top of the pager. Its I2C pins are the board's own bus
// (SDA IO3, SCL IO2), so a sensor plugged in there is found by itself: shown
// under Tools and sent in answer to the mesh's telemetry requests. IO9 can
// light an LED or sound a buzzer for new messages.
#pragma once
#include <Arduino.h>

class CayenneLPP;

namespace ext {

void begin();                    // after the power rails are up
uint32_t sleepBudgetMs(uint32_t now); // sensor readings and alert output edges
void tick();                     // from loop()
void alert();                    // a message alert went off (quiet hours already checked)
void applyPin();                 // after ui_settings.io9Mode changes
void telemetry(CayenneLPP& lpp); // readings for a telemetry reply (environment permission)
void openPage();
void report();                   // USB "ext": what is plugged in, and the readings

}  // namespace ext
