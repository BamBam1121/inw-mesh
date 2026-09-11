#include <Arduino.h>
#include "target.h"

InwPagerBoard board;

// Same host as the panel (bus_shared); a second SPI host on these pins hangs the
// board. The SD card uses this object too, so all three agree on the pins.
SPIClass inw_spi(FSPI);
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, inw_spi);
WRAPPER_CLASS radio_driver(radio, board);

InwRTCClock rtc_clock;
InwSensors sensors;

bool radio_init() {
  rtc_clock.begin();
  inw_spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);
  // std_init applies the LORA_* and SX126X_* build flags (the 3.0V TCXO on DIO3
  // and DIO2 as the RF switch, per Meshtastic's tlora-pager config). MyMesh then
  // re-applies the saved freq/bw/sf/cr/tx from prefs.
  return radio.std_init(&inw_spi);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
