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

// Which radio chip is actually on this board?
// LilyGo ships the T-Lora Pager with either an SX1262 or an LR1121 on the same
// pins, and this firmware drives the SX1262. When radio_init() fails, ask an
// LR11x0 for its version: GetVersion (0x0101) only reads, so it never drives the
// RF switch or the power amplifier of a chip we don't have a driver for.
// Returns "LR1110" / "LR1120" / "LR1121", or nullptr if nothing known answers.
const char* radio_chip_probe() {
  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);
  pinMode(P_LORA_RESET, OUTPUT);
  pinMode(P_LORA_BUSY, INPUT);
  inw_spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);

  digitalWrite(P_LORA_RESET, LOW);
  delay(5);
  digitalWrite(P_LORA_RESET, HIGH);
  uint32_t t0 = millis();                                  // BUSY stays high while it boots
  while (digitalRead(P_LORA_BUSY) && millis() - t0 < 500) delay(1);
  if (digitalRead(P_LORA_BUSY)) return nullptr;

  inw_spi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(P_LORA_NSS, LOW);
  inw_spi.transfer(0x01);
  inw_spi.transfer(0x01);
  digitalWrite(P_LORA_NSS, HIGH);
  inw_spi.endTransaction();

  t0 = millis();
  while (digitalRead(P_LORA_BUSY) && millis() - t0 < 100) delay(1);

  uint8_t rx[5] = {0};                     // status, hardware, device, fw major, fw minor
  inw_spi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(P_LORA_NSS, LOW);
  for (int i = 0; i < 5; i++) rx[i] = inw_spi.transfer(0x00);
  digitalWrite(P_LORA_NSS, HIGH);
  inw_spi.endTransaction();

  Serial.printf("[radio] probe: %02x %02x %02x %02x %02x\n", rx[0], rx[1], rx[2], rx[3], rx[4]);
  switch (rx[2]) {
    case 0x01: return "LR1110";
    case 0x02: return "LR1120";
    case 0x03: return "LR1121";
  }
  return nullptr;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
