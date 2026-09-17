#include <Arduino.h>
#include "target.h"
#include <new>

InwPagerBoard board;

// Same host as the panel (bus_shared); a second SPI host on these pins hangs the
// board. The SD card uses this object too, so all three agree on the pins.
SPIClass inw_spi(FSPI);
// LilyGo fits either an SX1262 or an LR1121 in the same slot, on the same pins,
// and they look identical from outside. Carry both drivers and let the board
// decide at boot (radio_init below). The one that isn't used is never begun.
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, inw_spi);
CustomLR1121 radio_lr = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, inw_spi);

// radio_driver has to stay a plain object: MeshCore's own MyMesh.cpp and
// ESP32Board.cpp use it that way. So reserve storage big enough for either
// driver and build the right one into it once the chip is known.
alignas(WRAPPER_CLASS) alignas(CustomLR1121Wrapper)
static uint8_t s_driver_store[sizeof(WRAPPER_CLASS) > sizeof(CustomLR1121Wrapper)
                              ? sizeof(WRAPPER_CLASS) : sizeof(CustomLR1121Wrapper)];
RadioLibWrapper& radio_driver = *reinterpret_cast<RadioLibWrapper*>(s_driver_store);
const char* radio_chip = "none";
static PhysicalLayer* s_phy = nullptr;

InwRTCClock rtc_clock;
InwSensors sensors;

bool radio_init() {
  if (radio_chip[0] != 'n') return true;   // built once; never placement-new over a live driver
  rtc_clock.begin();
  inw_spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);

  // SX1262 first, unchanged: std_init applies the LORA_* and SX126X_* build
  // flags (3.0V TCXO on DIO3, DIO2 as the RF switch, per Meshtastic's
  // tlora-pager config). RadioLib reads the chip's own version string, so this
  // only succeeds on a real SX1262 and every pager already in the field takes
  // this path exactly as it did before. MyMesh then re-applies the saved
  // freq/bw/sf/cr/tx from prefs.
  if (radio.std_init(&inw_spi)) {
    new (s_driver_store) WRAPPER_CLASS(radio, board);
    s_phy = &radio;
    radio_chip = "SX1262";
    return true;
  }

  // Otherwise this is the LR1121 board. begin() identifies the chip too
  // (RADIOLIB_ERR_CHIP_NOT_FOUND if it is something else), so reaching here with
  // no error means the LR1121 really is what is fitted.
#ifdef LORA_CR
  const uint8_t cr = LORA_CR;
#else
  const uint8_t cr = 5;
#endif
  const int state = radio_lr.begin(LORA_FREQ, LORA_BW, LORA_SF, cr,
                                   RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE,
                                   LORA_TX_POWER, 8, 3.0f);   // 8-arg begin applies the 3.0V TCXO itself
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[radio] neither SX1262 nor LR1121 answered (lr1121 said %d)\n", state);
    return false;
  }

  // RF switch on DIO5/DIO6. A wrong table here kills transmit power silently,
  // with no error returned, so this is the T-Lora Pager's proven table rather
  // than anything derived here: it matches Wadamesh's shipping LR1121 build for
  // this board, which is the build these pagers are known to work on.
  static const uint32_t rfswitch_dio_pins[] = {
    RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC,
  };
  static const Module::RfSwitchMode_t rfswitch_table[] = {
    { LR11x0::MODE_STBY,  { LOW,  LOW  } },
    { LR11x0::MODE_RX,    { LOW,  HIGH } },
    { LR11x0::MODE_TX,    { HIGH, LOW  } },
    { LR11x0::MODE_TX_HP, { HIGH, LOW  } },
    { LR11x0::MODE_TX_HF, { LOW,  LOW  } },
    { LR11x0::MODE_GNSS,  { LOW,  LOW  } },
    { LR11x0::MODE_WIFI,  { LOW,  LOW  } },
    END_OF_MODE_TABLE,
  };
  radio_lr.setRfSwitchTable(rfswitch_dio_pins, rfswitch_table);

  // LR11x0::begin() defaults to a 2-byte CRC; the mesh uses 1 byte, the same
  // override CustomSX1262::std_init() makes, so both chips talk to each other.
  radio_lr.setCRC(1);

  new (s_driver_store) CustomLR1121Wrapper(radio_lr, board);
  s_phy = &radio_lr;
  radio_chip = "LR1121";
  return true;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(s_phy ? *s_phy : (PhysicalLayer&)radio);
  return mesh::LocalIdentity(&rng);
}
