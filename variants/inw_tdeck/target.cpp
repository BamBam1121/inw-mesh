#include <Arduino.h>
#include "target.h"

InwTDeckBoard board;

// One SPI host for the radio, the SD card and the panel (bus_shared), on the
// same pins, as on the pager.
SPIClass inw_spi(FSPI);
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, inw_spi);
static WRAPPER_CLASS s_driver(radio, board);
RadioLibWrapper& radio_driver = s_driver;
const char* radio_chip = "none";

InwRTCClock rtc_clock;
InwSensors sensors;

bool radio_init() {
  if (radio_chip[0] != 'n') return true;
  rtc_clock.begin();
  inw_spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);
  // std_init applies the build flags: 1.8 V TCXO on DIO3 and DIO2 as the RF
  // switch. That last one must be on for the T-Deck: MeshCore's own variant has
  // it off, and Wadamesh found (their issue #6) that leaves the transmit path
  // unswitched - about 16 dB lost, a few milliwatts reaching the antenna.
  if (!radio.std_init(&inw_spi)) return false;
  radio_chip = "SX1262";
  return true;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
