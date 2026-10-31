#include "homelink.h"
#include <WiFi.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "netwifi.h"
#include "logstore.h"

extern LogStore logs;

namespace homelink {
namespace {

// ---- companion protocol (examples/companion_radio/MyMesh.cpp) ----------------------------------
constexpr uint8_t CMD_APP_START         = 1;
constexpr uint8_t CMD_GET_DEVICE_TIME   = 5;
constexpr uint8_t CMD_SEND_RAW_PACKET   = 65;
constexpr uint8_t RESP_OK               = 0;
constexpr uint8_t RESP_ERR              = 1;
constexpr uint8_t RESP_SELF_INFO        = 5;
constexpr uint8_t RESP_CURR_TIME        = 9;
constexpr uint8_t PUSH_LOG_RX_DATA      = 0x88;
constexpr uint8_t ERR_UNSUPPORTED_CMD   = 1;
constexpr int     MAX_FRAME             = 176;
constexpr uint16_t DEFAULT_PORT         = 5000;
constexpr uint8_t RAW_PRIORITY          = 1;   // what the companion queues it at

constexpr uint32_t CONNECT_TIMEOUT_MS   = 3000;
constexpr uint32_t HANDSHAKE_TIMEOUT_MS = 5000;
constexpr uint32_t PING_EVERY_MS        = 20000;
constexpr uint32_t DEAD_AFTER_MS        = 50000;   // nothing at all heard from the companion
constexpr uint32_t UNSUPPORTED_RETRY_MS = 10 * 60 * 1000;
constexpr uint8_t  UNSUPPORTED_AFTER    = 3;      // "unknown command" replies in a row to real sends
constexpr uint8_t  PEND_MAX             = 16;
constexpr uint8_t  RAW_IN_FLIGHT_MAX    = 8;

// ---- what the main loop and the task share --------------------------------------------------
struct TxItem { uint32_t seq; uint8_t len; uint8_t buf[MAX_TX_RAW]; };
struct RxItem { int8_t snr4; int8_t rssi; uint8_t len; uint8_t buf[MAX_RX_RAW]; };

struct Target { bool valid = false; char host[64] = {0}; uint16_t port = 0; uint32_t gen = 0; };

portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t s_txQ = nullptr, s_rxQ = nullptr;
Target        s_target;                       // written by tick(), read by the task (under s_mux)
volatile State s_state = State::Off;
Remote        s_remote;                       // written by the task (under s_mux)
Stats         s_stats;
char          s_event[64] = {0};              // last thing worth a log line, from the task
volatile uint32_t s_eventGen = 0;
uint32_t      s_seq = 0;

struct Result { uint32_t seq; int8_t r; };
Result s_results[32];

// ---- settings ---------------------------------------------------------------------------------
struct HostEntry { char ssid[33]; char host[64]; uint16_t port; };
constexpr uint8_t HOSTS_MAX = 8;
HostEntry s_hosts[HOSTS_MAX] = {};
bool      s_on = false;

void loadSettings() {
  Preferences p;
  if (!p.begin("inw-link", true)) return;
  s_on = p.getBool("on", false);
  if (p.getBytesLength("hosts") == sizeof(s_hosts)) p.getBytes("hosts", s_hosts, sizeof(s_hosts));
  p.end();
}

void storeSettings() {
  Preferences p;
  if (!p.begin("inw-link", false)) return;
  p.putBool("on", s_on);
  p.putBytes("hosts", s_hosts, sizeof(s_hosts));
  p.end();
}

void event(const char* fmt, ...) {
  char b[64];
  va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
  portENTER_CRITICAL(&s_mux);
  memcpy(s_event, b, sizeof(s_event));
  s_eventGen++;
  portEXIT_CRITICAL(&s_mux);
}

void setResult(uint32_t seq, int8_t r) {
  portENTER_CRITICAL(&s_mux);
  Result& x = s_results[seq % 32];
  if (x.seq == seq) x.r = r;
  portEXIT_CRITICAL(&s_mux);
}

// ---- the task -----------------------------------------------------------------------------------
enum Kind : uint8_t { K_APP, K_RAW, K_PING };
struct Pend { Kind kind; uint32_t seq; uint32_t at; };

class Link {
public:
  void run() {
    for (;;) {
      Target t;
      portENTER_CRITICAL(&s_mux); t = s_target; portEXIT_CRITICAL(&s_mux);

      if (!t.valid) {
        if (_c.connected() || _open) close(false);
        s_state = s_on ? State::Idle : State::Off;
        _gen = t.gen;
        drainTx();
        vTaskDelay(pdMS_TO_TICKS(200));
        continue;
      }
      if (t.gen != _gen) {                       // another network, host or mode
        if (_open) close(false);
        _gen = t.gen;
        _backoff = 0;
        _nextTry = 0;
      }

      if (!_open) {
        drainTx();
        if (s_state == State::Unsupported && (int32_t)(millis() - _nextTry) < 0) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }
        if (_nextTry && (int32_t)(millis() - _nextTry) < 0) { s_state = State::Retrying; vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        open(t);
        continue;
      }

      if (!_c.connected()) { close(true); continue; }

      readFrames();
      if (!_open) continue;                     // a frame closed it (unsupported, bad handshake)

      const uint32_t now = millis();
      if (s_state == State::Handshake && now - _openedAt > HANDSHAKE_TIMEOUT_MS) {
        event("home link: %s didn't answer as a companion", t.host);
        close(true);
        continue;
      }
      if (now - _lastHeard > DEAD_AFTER_MS) {
        event("home link: no reply for %lus, reconnecting", (unsigned long)(DEAD_AFTER_MS / 1000));
        close(true);
        continue;
      }

      if (s_state == State::Up) {
        writeQueued();
        if (now - _lastPing > PING_EVERY_MS && _pendN < PEND_MAX) {
          uint8_t f[1] = {CMD_GET_DEVICE_TIME};
          if (writeFrame(f, 1)) push(K_PING, 0);
          _lastPing = now;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(s_state == State::Up ? 2 : 10));
    }
  }

private:
  WiFiClient _c;
  bool       _open = false;
  uint32_t   _gen = 0, _nextTry = 0, _backoff = 0, _openedAt = 0, _lastHeard = 0, _lastPing = 0;
  Pend       _pend[PEND_MAX];
  uint8_t    _pendHead = 0, _pendN = 0, _rawInFlight = 0;
  uint8_t    _unknownInRow = 0;
  // frame parser
  uint8_t    _hdr[3];
  uint8_t    _hdrN = 0;
  uint16_t   _need = 0, _got = 0;
  uint8_t    _frame[MAX_FRAME];
  bool       _skip = false;

  void open(const Target& t) {
    s_state = State::Connecting;
    if (!_c.connect(t.host, t.port, CONNECT_TIMEOUT_MS)) {
      event("home link: can't reach %s:%u", t.host, t.port);
      fail();
      return;
    }
    _c.setNoDelay(true);
    _open = true;
    _hdrN = 0; _need = 0; _got = 0; _skip = false;
    _pendHead = _pendN = _rawInFlight = 0;
    _unknownInRow = 0;
    _openedAt = _lastHeard = _lastPing = millis();
    s_state = State::Handshake;
    portENTER_CRITICAL(&s_mux); s_stats.connects++; s_remote = Remote(); portEXIT_CRITICAL(&s_mux);

    uint8_t f[8 + 12] = {CMD_APP_START, 3, 0, 0, 0, 0, 0, 0};
    memcpy(f + 8, "squatch-link", 12);
    if (writeFrame(f, sizeof(f))) push(K_APP, 0);
    else close(true);
  }

  void close(bool retry) {
    const bool wasUp = s_state == State::Up;
    _c.stop();
    _open = false;
    // Raw packets still waiting on an answer: the dispatcher already timed them,
    // MeshCore's own retries take it from here.
    for (uint8_t i = 0; i < _pendN; i++) {
      const Pend& p = _pend[(_pendHead + i) % PEND_MAX];
      if (p.kind == K_RAW) setResult(p.seq, -1);
    }
    _pendN = 0; _rawInFlight = 0;
    if (wasUp) { portENTER_CRITICAL(&s_mux); s_stats.drops++; portEXIT_CRITICAL(&s_mux); }
    if (s_state != State::Unsupported) {
      s_state = State::Retrying;
      if (retry) fail(); else _nextTry = 0;
    }
  }

  void fail() {
    static const uint16_t STEPS[] = {2, 5, 10, 30, 60};
    _nextTry = millis() + STEPS[min<uint32_t>(_backoff, 4)] * 1000UL;
    if (_backoff < 4) _backoff++;
    s_state = State::Retrying;
  }

  void drainTx() {
    TxItem it;
    while (xQueueReceive(s_txQ, &it, 0) == pdTRUE) setResult(it.seq, -1);
  }

  void push(Kind k, uint32_t seq) {
    if (_pendN >= PEND_MAX) return;
    _pend[(_pendHead + _pendN) % PEND_MAX] = {k, seq, (uint32_t)millis()};
    _pendN++;
    if (k == K_RAW) _rawInFlight++;
  }

  bool pop(Pend& p) {
    if (!_pendN) return false;
    p = _pend[_pendHead];
    _pendHead = (_pendHead + 1) % PEND_MAX;
    _pendN--;
    if (p.kind == K_RAW && _rawInFlight) _rawInFlight--;
    return true;
  }

  bool writeFrame(const uint8_t* d, size_t n) {
    uint8_t b[3 + MAX_FRAME];
    if (n > MAX_FRAME) return false;
    b[0] = '<'; b[1] = n & 0xFF; b[2] = n >> 8;
    memcpy(b + 3, d, n);
    return _c.write(b, 3 + n) == 3 + n;
  }

  void writeQueued() {
    TxItem it;
    while (_rawInFlight < RAW_IN_FLIGHT_MAX && _pendN < PEND_MAX && xQueueReceive(s_txQ, &it, 0) == pdTRUE) {
      uint8_t f[2 + MAX_TX_RAW];
      f[0] = CMD_SEND_RAW_PACKET;
      f[1] = RAW_PRIORITY;
      memcpy(f + 2, it.buf, it.len);
      if (!writeFrame(f, 2 + it.len)) { setResult(it.seq, -1); close(true); return; }
      push(K_RAW, it.seq);
    }
  }

  void readFrames() {
    int budget = 2048;                          // don't sit here forever on a busy link
    while (budget > 0 && _c.available() > 0) {
      if (_hdrN < 3) {
        int b = _c.read();
        if (b < 0) break;
        budget--;
        if (_hdrN == 0 && b != '>') continue;   // resync on the frame marker
        _hdr[_hdrN++] = (uint8_t)b;
        if (_hdrN == 3) {
          _need = _hdr[1] | (_hdr[2] << 8);
          _got = 0;
          _skip = _need > MAX_FRAME;
          if (_need == 0) _hdrN = 0;
        }
        continue;
      }
      uint8_t tmp[64];
      const int want = min<int>(_need - _got, sizeof(tmp));
      const int n = _c.read(tmp, want);
      if (n <= 0) break;
      budget -= n;
      if (!_skip) memcpy(_frame + _got, tmp, n);
      _got += n;
      if (_got >= _need) {
        _hdrN = 0;
        _lastHeard = millis();
        if (!_skip) onFrame(_frame, _need);
        if (!_open) return;
      }
    }
  }

  void onFrame(const uint8_t* f, int n) {
    const uint8_t code = f[0];
    if (code >= 0x80) {                         // pushed, not an answer
      // A full-size frame may be a cut-off packet: MeshCore skips packets that
      // don't fit, openHop sends the first 173 bytes. Either way it can't be trusted.
      if (code == PUSH_LOG_RX_DATA && n >= MAX_FRAME) {
        portENTER_CRITICAL(&s_mux); s_stats.rxTruncated++; portEXIT_CRITICAL(&s_mux);
        return;
      }
      if (code == PUSH_LOG_RX_DATA && n >= 3 + 2 && s_state == State::Up) {
        RxItem it;
        it.snr4 = (int8_t)f[1];
        it.rssi = (int8_t)f[2];
        it.len = (uint8_t)min(n - 3, MAX_RX_RAW);
        memcpy(it.buf, f + 3, it.len);
        if (xQueueSend(s_rxQ, &it, 0) == pdTRUE) { portENTER_CRITICAL(&s_mux); s_stats.rx++; portEXIT_CRITICAL(&s_mux); }
      }
      return;                                    // adverts, the companion's own messages: not ours
    }
    // Answers come back in the order asked, but not everything below 0x80 is an
    // answer: openHop sends RESP_CODE_CURR_TIME on its own as a heartbeat when a
    // client has been quiet. Only take the front request off the list when the code
    // is one it can be answered with.
    if (!_pendN) return;
    const Kind front = _pend[_pendHead].kind;
    const bool answers =
        front == K_APP   ? (code == RESP_SELF_INFO || code == RESP_ERR) :
        front == K_PING  ? (code == RESP_CURR_TIME) :
                           (code == RESP_OK || code == RESP_ERR);       // K_RAW
    if (!answers) return;
    Pend p;
    pop(p);
    switch (p.kind) {
      case K_APP:
        // No firmware or version check: anything that answers app start in the
        // companion protocol is taken at its word. A full SELF_INFO gives us its name
        // and radio settings; a short one or an error just means we don't know them,
        // and airtime falls back to our own radio's settings.
        if (code == RESP_SELF_INFO) onSelfInfo(f, n);
        s_state = State::Up;
        _backoff = 0;
        {
          portENTER_CRITICAL(&s_mux); const Remote r = s_remote; portEXIT_CRITICAL(&s_mux);
          if (r.sf) event("home link up: %.20s %.3f/%.1f/sf%u", r.name, r.freq, r.bw, r.sf);
          else event("home link up (companion didn't say its radio settings)");
        }
        break;
      case K_RAW: {
        const bool ok = code == RESP_OK;
        // The only way the link gives up on a companion: it answers real sends, several
        // in a row, with "unknown command". Then it really can't take raw packets.
        if (!ok && n >= 2 && f[1] == ERR_UNSUPPORTED_CMD) {
          if (++_unknownInRow >= UNSUPPORTED_AFTER) {
            setResult(p.seq, -1);
            event("home link: companion says it can't send raw packets");
            s_state = State::Unsupported;
            _nextTry = millis() + UNSUPPORTED_RETRY_MS;
            close(false);
            return;
          }
        } else if (ok) {
          _unknownInRow = 0;
        }
        setResult(p.seq, ok ? 1 : -1);
        portENTER_CRITICAL(&s_mux);
        if (ok) s_stats.txOk++; else s_stats.txErr++;
        portEXIT_CRITICAL(&s_mux);
        break;
      }
      case K_PING:
        portENTER_CRITICAL(&s_mux); s_stats.rttMs = millis() - p.at; portEXIT_CRITICAL(&s_mux);
        break;
    }
  }

  // RESP_CODE_SELF_INFO: code type txpow maxpow pub[32] lat[4] lon[4] multi_acks
  // adv_loc telem manual_add freq[4] bw[4] sf cr name...
  void onSelfInfo(const uint8_t* f, int n) {
    Remote r;
    if (n >= 8) memcpy(r.pub, f + 4, sizeof(r.pub));
    if (n >= 58) {
      uint32_t freq, bw;
      memcpy(&freq, f + 48, 4);
      memcpy(&bw, f + 52, 4);
      r.freq = freq / 1000.0f;
      r.bw = bw / 1000.0f;
      r.sf = f[56];
      r.cr = f[57];
      const int nl = min(n - 58, (int)sizeof(r.name) - 1);
      memcpy(r.name, f + 58, nl);
      r.name[nl] = 0;
    }
    portENTER_CRITICAL(&s_mux); s_remote = r; portEXIT_CRITICAL(&s_mux);
  }
};

Link s_link;
void taskMain(void*) { s_link.run(); }

// ---- tick-side helpers --------------------------------------------------------------------------
bool parseHostPort(const char* in, char* host, size_t cap, uint16_t& port) {
  while (*in == ' ') in++;
  if (!*in) return false;
  strlcpy(host, in, cap);
  char* colon = strrchr(host, ':');
  port = DEFAULT_PORT;
  if (colon) {
    const long p = strtol(colon + 1, nullptr, 10);
    if (p <= 0 || p > 65535) return false;
    port = (uint16_t)p;
    *colon = 0;
  }
  for (char* e = host + strlen(host); e > host && e[-1] == ' '; ) *--e = 0;
  return host[0] != 0;
}

}  // namespace

// ---- public ---------------------------------------------------------------------------------------
void begin() {
  loadSettings();
  s_txQ = xQueueCreate(8, sizeof(TxItem));
  s_rxQ = xQueueCreate(16, sizeof(RxItem));
  // Core 0 with the Wi-Fi stack; the UI and the mesh run on core 1.
  xTaskCreatePinnedToCore(taskMain, "homelink", 6144, nullptr, 2, nullptr, 0);
}

void tick() {
  static uint32_t last = 0, seenEvent = 0;
  if (s_eventGen != seenEvent) {
    char b[64];
    portENTER_CRITICAL(&s_mux); memcpy(b, s_event, sizeof(b)); seenEvent = s_eventGen; portEXIT_CRITICAL(&s_mux);
    logs.add(LOG_INFO, "%s", b);
  }
  if (millis() - last < 500) return;
  last = millis();

  Target want;
  if (s_on && wifi::connected())
    want.valid = hostFor(wifi::ssid(), want.host, sizeof(want.host), want.port);

  portENTER_CRITICAL(&s_mux);
  const bool same = want.valid == s_target.valid && want.port == s_target.port && !strcmp(want.host, s_target.host);
  if (!same) {
    want.gen = s_target.gen + 1;
    s_target = want;
  }
  portEXIT_CRITICAL(&s_mux);
}

bool enabled() { return s_on; }
void setEnabled(bool on) {
  if (on == s_on) return;
  s_on = on;
  storeSettings();
  // tick() picks the new target up within half a second either way.
}

State state() { return s_state; }
bool  up() { return s_state == State::Up; }
const Remote& remote() { return s_remote; }
const Stats&  stats() { return s_stats; }

const char* statusText() {
  static char b[72];
  Target t;
  portENTER_CRITICAL(&s_mux); t = s_target; const Remote r = s_remote; portEXIT_CRITICAL(&s_mux);
  switch (s_state) {
    case State::Off:         return "off";
    case State::Idle:        return wifi::connected() ? "no companion set for this network" : "waiting for wi-fi";
    case State::Connecting:  snprintf(b, sizeof(b), "connecting to %.40s:%u", t.host, t.port); return b;
    case State::Handshake:   snprintf(b, sizeof(b), "talking to %.40s", t.host); return b;
    case State::Up:
      if (r.sf) snprintf(b, sizeof(b), "up: %.20s %.3f/%.1f/sf%u", r.name, r.freq, r.bw, r.sf);
      else snprintf(b, sizeof(b), "up: %.40s", t.host);
      return b;
    case State::Unsupported: return "companion refused raw packets, trying again in 10 min";
    case State::Retrying:    snprintf(b, sizeof(b), "can't reach %.40s, retrying", t.host); return b;
  }
  return "";
}

bool hostFor(const char* ssid, char* host, size_t cap, uint16_t& port) {
  if (!ssid || !*ssid) return false;
  for (const auto& h : s_hosts)
    if (h.ssid[0] && !strcmp(h.ssid, ssid) && h.host[0]) {
      strlcpy(host, h.host, cap);
      port = h.port ? h.port : DEFAULT_PORT;
      return true;
    }
  return false;
}

void setHost(const char* ssid, const char* hostPort) {
  if (!ssid || !*ssid) return;
  char host[64]; uint16_t port = 0;
  const bool set = hostPort && parseHostPort(hostPort, host, sizeof(host), port);
  HostEntry* slot = nullptr;
  for (auto& h : s_hosts) if (h.ssid[0] && !strcmp(h.ssid, ssid)) { slot = &h; break; }
  if (!set) {
    if (slot) memset(slot, 0, sizeof(*slot));
  } else {
    if (!slot) for (auto& h : s_hosts) if (!h.ssid[0]) { slot = &h; break; }
    if (!slot) slot = &s_hosts[0];                // full: reuse the first
    strlcpy(slot->ssid, ssid, sizeof(slot->ssid));
    strlcpy(slot->host, host, sizeof(slot->host));
    slot->port = port;
  }
  storeSettings();
}

String hostText(const char* ssid) {
  char host[64]; uint16_t port;
  if (!hostFor(ssid, host, sizeof(host), port)) return String();
  return String(host) + ":" + String(port);
}

bool send(const uint8_t* raw, int len, uint32_t& seq) {
  if (s_state != State::Up || len <= 0 || len > MAX_TX_RAW || !s_txQ) return false;
  TxItem it;
  it.seq = ++s_seq;
  it.len = (uint8_t)len;
  memcpy(it.buf, raw, len);
  portENTER_CRITICAL(&s_mux); s_results[it.seq % 32] = {it.seq, 0}; portEXIT_CRITICAL(&s_mux);
  if (xQueueSend(s_txQ, &it, 0) != pdTRUE) return false;
  seq = it.seq;
  return true;
}

int8_t result(uint32_t seq) {
  portENTER_CRITICAL(&s_mux);
  const Result r = s_results[seq % 32];
  portEXIT_CRITICAL(&s_mux);
  if (r.seq == seq) return r.r;
  return (int32_t)(r.seq - seq) > 0 ? 1 : 0;   // overwritten by a later one: long settled
}

bool recv(uint8_t* raw, int& len, float& snr, float& rssi) {
  if (!s_rxQ) return false;
  RxItem it;
  if (xQueueReceive(s_rxQ, &it, 0) != pdTRUE) return false;
  memcpy(raw, it.buf, it.len);
  len = it.len;
  snr = it.snr4 / 4.0f;
  rssi = it.rssi;
  return true;
}

}  // namespace homelink
