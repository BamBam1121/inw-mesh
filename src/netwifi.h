// Wi-Fi: saved networks, NTP, and fetching map tiles while connected.
//
// The fetch task only downloads into PSRAM. The loop task writes tiles to SD,
// because the card shares its SPI bus with the panel and radio and nothing
// else may touch it.

#pragma once
#include <Arduino.h>

namespace wifi {
  constexpr uint8_t SAVED_MAX = 5;

  void begin();                 // at boot: start if enabled
  void tick();                  // from loop
  void setEnabled(bool on);
  bool enabled();
  bool connected();
  const char* statusText();     // "home-net 192.168.1.40 -61 dBm" / "searching" / "off"
  const char* ssid();

  // Saved networks
  uint8_t savedCount();
  const char* savedSsid(uint8_t i);
  void save(const char* ssid, const char* pass);   // also connects
  void forget(uint8_t i);

  // Scanning (asynchronous)
  void startScan();
  bool scanDone();
  int  scanCount();
  const char* scanSsid(int i);
  int  scanRssi(int i);
  bool scanOpen(int i);

  // Tile fetching. request() is cheap and de-duplicated; pollTile() hands the
  // main loop one finished download at a time to write out.
  void requestTile(uint8_t z, int32_t x, int32_t y);
  struct TileDone { uint8_t z; int32_t x, y; bool png; uint8_t* data; size_t len; };
  bool pollTile(TileDone& out);      // caller must free(out.data)
  uint16_t tilesFetched();
  uint16_t tilesFailed();
  int lastHttpCode();
}
