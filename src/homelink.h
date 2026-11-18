// Home link: while the pager is on a saved Wi-Fi network that has a companion
// host set for it, raw MeshCore packets go to and come from that companion over
// TCP (CMD_SEND_RAW_PACKET out, PUSH_CODE_LOG_RX_DATA in) instead of the
// pager's own LoRa radio. The pager keeps its own identity,
// contacts and history: the companion at home is only a radio on a wire.
//
// Works with any companion that speaks the MeshCore companion protocol over TCP
// and takes CMD_SEND_RAW_PACKET, whatever its firmware (MeshCore companions,
// openHop Repeater's companion listeners, others). There is no firmware or
// version check.
//
// A FreeRTOS task owns the socket. The main loop only touches two queues and a
// few counters, so a slow connect or a stalled router never holds up the UI or
// the mesh.

#pragma once
#include <Arduino.h>

namespace homelink {

// Largest raw packet that fits one companion frame (MAX_FRAME_SIZE 176 less the
// command byte and the priority byte). Received packets are one byte smaller
// still (176 less code, snr, rssi). Anything bigger stays on LoRa.
constexpr int MAX_TX_RAW = 174;
constexpr int MAX_RX_RAW = 173;

enum class State : uint8_t {
  Off,           // turned off in settings
  Idle,          // no Wi-Fi, or this network has no companion set
  Connecting,
  Handshake,     // connected, waiting for the answer to app start
  Up,
  Unsupported,   // the companion answered real sends with "unknown command"
  Retrying,      // last attempt failed, waiting to try again
};

struct Remote {
  char     name[33] = {0};
  uint8_t  pub[4] = {0};        // first bytes of the companion's own key, for display
  float    freq = 0, bw = 0;    // MHz, kHz
  uint8_t  sf = 0, cr = 0;
};

struct Stats {
  uint32_t txOk = 0, txErr = 0, rx = 0, rxTruncated = 0, connects = 0, drops = 0;
  uint32_t rttMs = 0;           // last keepalive round trip
};

void begin();                   // once, after wifi::begin()
void tick();                    // from loop(): follows Wi-Fi and the settings

bool  enabled();
void  setEnabled(bool on);
State state();
bool  up();
const char* statusText();       // "up: home-companion 869.618/62.5/sf8" / "connecting 192.168.1.20:5000" ...
const Remote& remote();
const Stats&  stats();

// Per-network companion host, by SSID. "host" or "host:port" (default port 5000).
// An empty string removes it. Kept apart from the saved networks blob on purpose:
// netwifi drops that blob if its size changes.
bool  hostFor(const char* ssid, char* host, size_t cap, uint16_t& port);
void  setHost(const char* ssid, const char* hostPort);
String hostText(const char* ssid);   // "" or "host:port", for the menu

// Radio side, main loop only.
// send(): false if the link is down or its queue is full. seq identifies the
// packet for result(): 0 pending, 1 accepted by the companion, -1 refused.
bool   send(const uint8_t* raw, int len, uint32_t& seq);
int8_t result(uint32_t seq);
// recv(): one packet the companion heard, with its SNR/RSSI. false when none.
bool   recv(uint8_t* raw, int& len, float& snr, float& rssi);

}  // namespace homelink
