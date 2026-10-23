#include <Arduino.h>
#include "target.h"

InwM9Board board;

// One SPI host for the radio, the SD card and the panel (bus_shared), as on the
// pager and the T-Deck.
SPIClass inw_spi(FSPI);
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, inw_spi);
static WRAPPER_CLASS s_driver(radio, board);
RadioLibWrapper& radio_driver = s_driver;
const char* radio_chip = "none";

InwRTCClock rtc_clock;
InwSensors sensors;

// The LR1110's antenna switch is two of its own DIO lines, set per mode. This table is
// the one both MeshCore's and Meshtastic's M9 builds use (m9-reference/).
static const uint32_t rfswitch_dios[Module::RFSWITCH_MAX_PINS] = {
  RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC
};
static const Module::RfSwitchMode_t rfswitch_table[] = {
  // mode                  DIO5  DIO6
  {LR11x0::MODE_STBY, {LOW, LOW}},  {LR11x0::MODE_RX, {HIGH, LOW}},
  {LR11x0::MODE_TX, {HIGH, HIGH}},  {LR11x0::MODE_TX_HP, {LOW, HIGH}},
  {LR11x0::MODE_TX_HF, {LOW, LOW}}, {LR11x0::MODE_GNSS, {LOW, LOW}},
  {LR11x0::MODE_WIFI, {LOW, LOW}},  END_OF_MODE_TABLE,
};

bool radio_init() {
  if (radio_chip[0] != 'n') return true;
  rtc_clock.begin();
  inw_spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);
  // As MeshCore's M9 target.cpp: 3.3 V TCXO on DIO3, the private sync word, a 16-symbol
  // preamble, then CRC on and an explicit header.
  const int status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE,
                                 LORA_TX_POWER, 16, LR11X0_DIO3_TCXO_VOLTAGE);
  if (status != RADIOLIB_ERR_NONE) {
    Serial.printf("[radio] LR1110 begin failed: %d\n", status);
    return false;
  }
  radio.setCRC(2);
  radio.explicitHeader();
  radio.setRfSwitchTable(rfswitch_dios, rfswitch_table);
  radio.setRxBoostedGainMode(true);
  radio_chip = "LR1110";
  return true;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
