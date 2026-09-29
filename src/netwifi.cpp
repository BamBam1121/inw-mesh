#include "netwifi.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <esp_heap_caps.h>
#include "settings.h"
#include "logstore.h"
#include "backlight.h"
#include "app.h"           // dimmer.asleep(): back off harder while nobody's looking

extern LogStore logs;
namespace app { void setTime(uint32_t epoch); }

namespace wifi {

struct Saved { char ssid[33]; char pass[65]; };
static Saved s_saved[SAVED_MAX];
static Saved s_test;                         // a one-off join from USB (dev builds), never stored
static uint8_t s_count = 0;
static bool s_on = false, s_scanning = false;
static int s_scanResults = -1;
static uint8_t s_try = 0;
static uint32_t s_nextTry = 0;
static uint8_t s_misses = 0;                 // joins that failed in a row (none of ours in range)
static bool s_wasConnected = false;

// When to try again after a failed join. Each try scans for up to 15 s, and Wi-Fi
// scanning is about the hungriest thing the chip does: retrying every 20 s forever
// with none of the saved networks around flattened a T-Deck in 4 h (2026-09-28).
// So it backs off - 20 s, 40 s, ... up to 15 minutes while the screen is dark, and
// at most a minute while someone is looking at it (and so maybe waiting on it).
static uint32_t retryDelay() {
  const uint32_t d = 20000UL << (s_misses < 6 ? s_misses : 6);   // 20 s .. ~21 min
  const uint32_t cap = dimmer.asleep() ? 900000UL : 60000UL;
  return d < cap ? d : cap;
}
static volatile bool s_ntpSynced = false;

// Scans. The core gives up on an async scan after 6 s and from then on reports
// it failed, even once the results are in; a scan that ran long (Bluetooth
// sharing the radio makes them slower) was left "scanning..." forever. So the
// scan-done event says when it's finished, and the results are counted directly.
static volatile bool s_scanDoneEvt = false;
static bool s_scanWanted = false;            // asked for, not started: the radio was busy joining
static uint32_t s_scanAt = 0, s_scanTryAt = 0;

// Joining, and how each saved network's last attempt went, so the screen can say
// "wrong password?" instead of "searching" forever.
enum Result : uint8_t { J_NONE, J_OK, J_PASSWORD, J_NOT_FOUND, J_WEAK, J_NO_IP, J_OTHER };
static Result s_result[SAVED_MAX + 1];       // + the USB test slot
static int8_t s_joinSlot = -1, s_lastSlot = -1;
static uint32_t s_joinAt = 0;
static volatile bool s_assoc = false;        // joined, still waiting for an address
static volatile uint8_t s_reason = 0;        // why the join in progress ended, 0 = hasn't

static const Saved& net(int k) { return k == SAVED_MAX ? s_test : s_saved[k]; }

static void onEvent(arduino_event_id_t e, arduino_event_info_t info) {
  switch (e) {
    case ARDUINO_EVENT_WIFI_SCAN_DONE: s_scanDoneEvt = true; break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: s_assoc = true; break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      s_assoc = false;
      const uint8_t r = info.wifi_sta_disconnected.reason;
      if (r != WIFI_REASON_ASSOC_LEAVE) s_reason = r ? r : (uint8_t)WIFI_REASON_UNSPECIFIED;   // LEAVE: we hung up
      break;
    }
    default: break;
  }
}

static Result classify(uint8_t r) {
  switch (r) {
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: case WIFI_REASON_HANDSHAKE_TIMEOUT: case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_MIC_FAILURE: case WIFI_REASON_802_1X_AUTH_FAILED:
      return J_PASSWORD;
    case WIFI_REASON_NO_AP_FOUND:
      return J_NOT_FOUND;
    case WIFI_REASON_BEACON_TIMEOUT: case WIFI_REASON_ASSOC_FAIL: case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_ASSOC_EXPIRE: case WIFI_REASON_CONNECTION_FAIL: case WIFI_REASON_TIMEOUT:
      return J_WEAK;
    default:
      return J_OTHER;
  }
}

static const char* resultText(Result r) {
  switch (r) {
    case J_OK:        return "connected";
    case J_PASSWORD:  return "wrong password?";
    case J_NOT_FOUND: return "not found (2.4 GHz only)";
    case J_WEAK:      return "no answer, weak signal?";
    case J_NO_IP:     return "joined, router gave no address";
    case J_OTHER:     return "couldn't join";
    default:          return "";
  }
}

static void join(int k) {
  s_joinSlot = s_lastSlot = k;
  s_joinAt = millis() | 1;
  s_assoc = false;
  s_reason = 0;
  WiFi.begin(net(k).ssid, net(k).pass);
}

static int scanFound() {
  int n = 0;
  while (n < 100 && WiFi.getScanInfoByIndex(n)) n++;
  return n;
}

static void tryStartScan() {
  s_scanTryAt = millis();
  WiFi.scanDelete();
  s_scanDoneEvt = false;
  if (WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING) { s_scanWanted = false; return; }
  // A scan can't start while a join is under way: stop that one, try again next tick.
  if (!connected()) { s_joinSlot = -1; WiFi.disconnect(); }
}

static void load() {
  Preferences p;
  if (!p.begin("inw-wifi", true)) return;
  s_count = min<uint8_t>(p.getUChar("n", 0), SAVED_MAX);
  if (p.getBytesLength("nets") == sizeof(s_saved)) p.getBytes("nets", s_saved, sizeof(s_saved));
  else s_count = 0;
  p.end();
}

static void store() {
  Preferences p;
  if (!p.begin("inw-wifi", false)) return;
  p.putUChar("n", s_count);
  p.putBytes("nets", s_saved, sizeof(s_saved));
  p.end();
}

static void onNtp(struct timeval*) { s_ntpSynced = true; }

static void connectNext() {
  if (!s_count) return;
  // Prefer a saved network the last scan actually saw, strongest first, unless
  // its last try failed: then the others get a turn instead of that one forever.
  int best = -1, bestRssi = -1000;
  const int n = s_scanResults;
  for (int i = 0; i < n; i++) {
    for (uint8_t k = 0; k < s_count; k++) {
      if (s_result[k] > J_OK) continue;
      if (WiFi.SSID(i) == s_saved[k].ssid && WiFi.RSSI(i) > bestRssi) { best = k; bestRssi = WiFi.RSSI(i); }
    }
  }
  join(best >= 0 ? best : (s_try++ % s_count));
}

void begin() {
  load();
  WiFi.onEvent(onEvent);
  if (ui_settings.wifiOn) setEnabled(true);
}

void setEnabled(bool on) {
  if (on == s_on) return;
  s_on = on;
  s_misses = 0;                          // a fresh start: try straight away again
  if (on) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);                 // modem sleep: coexists with BLE, saves power
    WiFi.setAutoReconnect(false);        // we pick the network ourselves
    s_nextTry = millis();
    logs.add(LOG_INFO, "wifi on");
  } else {
    s_joinSlot = -1;
    s_scanning = s_scanWanted = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    s_wasConnected = false;
    logs.add(LOG_INFO, "wifi off");
  }
}

bool enabled() { return s_on; }
bool connected() { return s_on && WiFi.status() == WL_CONNECTED; }
const char* ssid() { static char b[33]; strlcpy(b, connected() ? WiFi.SSID().c_str() : "", sizeof(b)); return b; }

const char* statusText() {
  static char b[64];
  if (!s_on) return "off";
  if (connected()) {
    snprintf(b, sizeof(b), "%s  %s  %d dBm", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return b;
  }
  if (!s_count && s_joinSlot < 0) return "no saved networks - scan to add one";
  if (s_joinSlot >= 0) {
    snprintf(b, sizeof(b), "%s %.20s...", s_assoc ? "getting an address from" : "joining", net(s_joinSlot).ssid);
    return b;
  }
  if (s_lastSlot >= 0 && s_result[s_lastSlot] > J_OK) {
    snprintf(b, sizeof(b), "%.20s: %s", net(s_lastSlot).ssid, resultText(s_result[s_lastSlot]));
    return b;
  }
  return "searching";
}

const char* shortStatus() {
  if (!s_on) return "off";
  if (connected()) return ssid();
  if (s_joinSlot < 0 && s_lastSlot >= 0 && s_result[s_lastSlot] > J_OK) return resultText(s_result[s_lastSlot]);
  return "searching";
}

const char* savedState(uint8_t i) {
  if (!s_on || i >= s_count) return "";
  if (connected() && WiFi.SSID() == s_saved[i].ssid) return "connected";
  if (s_joinSlot == i) return "joining...";
  return s_result[i] > J_OK ? resultText(s_result[i]) : "";
}

void tick() {
  if (!s_on) return;
  const bool c = WiFi.status() == WL_CONNECTED;

  // How the join under way ended. Wrong passwords are retried less often: every
  // 20 s would just keep knocking on the router with it.
  if (s_joinSlot >= 0) {
    const uint8_t r = s_reason;
    if (c) {
      s_result[s_joinSlot] = J_OK;
      s_joinSlot = -1;
      s_misses = 0;
    } else if (r || (int32_t)(millis() - s_joinAt) > 15000) {
      const Result res = r ? classify(r) : s_assoc ? J_NO_IP : J_WEAK;
      s_result[s_joinSlot] = res;
      logs.add(LOG_WARN, "wifi %s: %s (reason %u)", net(s_joinSlot).ssid, resultText(res), r);
      s_joinSlot = -1;
      if (!r) WiFi.disconnect();                // gave up waiting: stop that attempt
      if (s_misses < 250) s_misses++;
      const uint32_t wait = retryDelay();
      s_nextTry = millis() + (res == J_PASSWORD && wait < 120000UL ? 120000UL : wait);
    }
  }
  // Someone picked it up while it was waiting out a long back-off: try within a minute.
  if (!dimmer.asleep() && (int32_t)(s_nextTry - millis()) > 60000) s_nextTry = millis() + 60000;

  if (s_scanning) {
    if (s_scanWanted) {
      if (millis() - s_scanTryAt > 300) tryStartScan();
    } else if (s_scanDoneEvt) {
      s_scanDoneEvt = false;
      s_scanning = false;
      s_scanResults = scanFound();
    }
    if (s_scanning && millis() - s_scanAt > 20000) {  // never came back: show what there is
      WiFi.scanComplete();                            // lets the core clear its "scanning" flag
      s_scanning = s_scanWanted = false;
      s_scanResults = scanFound();
      logs.add(LOG_WARN, "wifi scan gave up after 20 s, %d found", s_scanResults);
    }
  }

  if (c && !s_wasConnected) {
    logs.add(LOG_INFO, "wifi %s %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    if (ui_settings.ntpSync) {
      sntp_set_time_sync_notification_cb(onNtp);
      configTime(0, 0, "pool.ntp.org", "time.google.com");
    }
  }
  s_wasConnected = c;
  if (s_ntpSynced) {
    s_ntpSynced = false;
    app::setTime((uint32_t)time(nullptr));     // also writes the hardware RTC
    logs.add(LOG_INFO, "clock set from internet");
  }
  if (!c && !s_scanning && s_joinSlot < 0 && (int32_t)(millis() - s_nextTry) >= 0) {
    s_nextTry = millis() + 20000;
    connectNext();
  }
}

uint8_t savedCount() { return s_count; }
const char* savedSsid(uint8_t i) { return i < s_count ? s_saved[i].ssid : ""; }

void save(const char* ss, const char* pass) {
  int slot = -1;
  for (uint8_t i = 0; i < s_count; i++) if (!strcmp(s_saved[i].ssid, ss)) slot = i;
  if (slot < 0) {
    if (s_count == SAVED_MAX) { memmove(s_saved, s_saved + 1, sizeof(Saved) * (SAVED_MAX - 1)); s_count--; }
    slot = s_count++;
  }
  strlcpy(s_saved[slot].ssid, ss, sizeof(s_saved[slot].ssid));
  strlcpy(s_saved[slot].pass, pass, sizeof(s_saved[slot].pass));
  store();
  if (!s_on) { ui_settings.wifiOn = true; ui_settings.save(); setEnabled(true); }
  s_result[slot] = J_NONE;
  s_misses = 0;
  s_scanning = s_scanWanted = false;            // the join goes first
  WiFi.disconnect();
  join(slot);
  s_nextTry = millis() + 20000;
}

void forget(uint8_t i) {
  if (i >= s_count) return;
  memmove(&s_saved[i], &s_saved[i + 1], sizeof(Saved) * (s_count - i - 1));
  memmove(&s_result[i], &s_result[i + 1], sizeof(Result) * (s_count - i - 1));
  s_count--;
  memset(&s_saved[s_count], 0, sizeof(Saved));
  s_result[s_count] = J_NONE;
  if (s_joinSlot == i) s_joinSlot = -1; else if (s_joinSlot > i) s_joinSlot--;
  if (s_lastSlot == i) s_lastSlot = -1; else if (s_lastSlot > i) s_lastSlot--;
  store();
}

void startScan() {
  if (!s_on) setEnabled(true);
  s_scanResults = -1;
  s_scanning = s_scanWanted = true;
  s_scanAt = millis();
  tryStartScan();
}
bool scanDone() { return !s_scanning && s_scanResults >= 0; }

#if INW_DEV
void testJoin(const char* ss, const char* pass) {
  strlcpy(s_test.ssid, ss, sizeof(s_test.ssid));
  strlcpy(s_test.pass, pass, sizeof(s_test.pass));
  s_result[SAVED_MAX] = J_NONE;
  if (!s_on) setEnabled(true);
  s_scanning = s_scanWanted = false;
  WiFi.disconnect();
  join(SAVED_MAX);
  s_nextTry = millis() + 20000;
}
#endif
int scanCount() { return s_scanResults < 0 ? 0 : s_scanResults; }
const char* scanSsid(int i) { static char b[33]; strlcpy(b, WiFi.SSID(i).c_str(), sizeof(b)); return b; }
int scanRssi(int i) { return WiFi.RSSI(i); }
bool scanOpen(int i) { return WiFi.encryptionType(i) == WIFI_AUTH_OPEN; }
bool scanEnterprise(int i) { return WiFi.encryptionType(i) == WIFI_AUTH_WPA2_ENTERPRISE; }

// ---- tile fetching --------------------------------------------------------------------
struct Req { uint8_t z; int32_t x, y; };
static QueueHandle_t s_reqQ = nullptr, s_doneQ = nullptr;
static volatile uint16_t s_ok = 0, s_fail = 0;
static volatile int s_lastCode = 0;
static Req s_recent[48];
static uint8_t s_recentHead = 0;

static void buildUrl(const Req& r, char* url, size_t cap, bool& wantJpg) {
  wantJpg = false;
  switch (ui_settings.tileSource) {
    case 1:   // Wadamesh's proxy: HTTP, transcoded to JPEG
      snprintf(url, cap, "http://tiles.wadamesh.com/%u/%ld/%ld.jpg", r.z, (long)r.x, (long)r.y);
      wantJpg = true;
      break;
    case 2: {
      String base = ui_settings.tileUrl;
      while (base.endsWith("/")) base.remove(base.length() - 1);
      snprintf(url, cap, "%s/%u/%ld/%ld.png", base.c_str(), r.z, (long)r.x, (long)r.y);
      break;
    }
    default:  // OpenStreetMap: viewing-only use, identified, rate-limited
      snprintf(url, cap, "https://tile.openstreetmap.org/%u/%ld/%ld.png", r.z, (long)r.x, (long)r.y);
  }
}

static void fetchTask(void*) {
  Req r;
  for (;;) {
    if (xQueueReceive(s_reqQ, &r, portMAX_DELAY) != pdTRUE) continue;
    if (!connected()) { s_fail++; continue; }
    char url[160];
    bool jpg;
    buildUrl(r, url, sizeof(url), jpg);
    const bool https = !strncmp(url, "https", 5);
    // TLS needs ~40 KB of internal RAM; don't try if it isn't there.
    if (https && heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 48 * 1024) {
      s_lastCode = -99; s_fail++; vTaskDelay(pdMS_TO_TICKS(2000)); continue;
    }
    WiFiClient plain;
    WiFiClientSecure secure;
    if (https) secure.setInsecure();     // map imagery, nothing secret
    HTTPClient http;
    http.setConnectTimeout(4000);
    http.setTimeout(4000);
    bool began = https ? http.begin(secure, url) : http.begin(plain, url);
    if (!began) { s_fail++; continue; }
    http.addHeader("User-Agent", "SquatchMesh/1.0 (LilyGo T-Lora Pager mesh firmware; github.com/BamBam1121)");
    const int code = http.GET();
    s_lastCode = code;
    bool ok = false;
    if (code == HTTP_CODE_OK) {
      const int len = http.getSize();
      if (len > 0 && len <= 96 * 1024) {
        uint8_t* buf = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (buf) {
          WiFiClient* st = http.getStreamPtr();
          int got = 0;
          const uint32_t until = millis() + 8000;
          while (got < len && (int32_t)(millis() - until) < 0) {
            const int a = st->available();
            if (a > 0) got += st->read(buf + got, min(a, len - got));
            else vTaskDelay(pdMS_TO_TICKS(5));
          }
          const bool isPng = got > 4 && buf[0] == 0x89 && buf[1] == 'P';
          const bool isJpg = got > 3 && buf[0] == 0xFF && buf[1] == 0xD8;
          if (got == len && (isPng || isJpg)) {
            TileDone d{ r.z, r.x, r.y, isPng, buf, (size_t)len };
            if (xQueueSend(s_doneQ, &d, pdMS_TO_TICKS(1000)) == pdTRUE) ok = true;
          }
          if (!ok) free(buf);
        }
      }
    }
    http.end();
    if (ok) s_ok++; else s_fail++;
    vTaskDelay(pdMS_TO_TICKS(500));      // <= 2 requests/second, per OSM policy
  }
}

void requestTile(uint8_t z, int32_t x, int32_t y) {
  if (!ui_settings.tileFetch || !connected()) return;
  for (const Req& q : s_recent) if (q.z == z && q.x == x && q.y == y) return;   // asked recently
  if (!s_reqQ) {
    s_reqQ = xQueueCreate(24, sizeof(Req));
    s_doneQ = xQueueCreate(6, sizeof(TileDone));
    xTaskCreatePinnedToCore(fetchTask, "tiles", 8192, nullptr, 1, nullptr, 0);
  }
  Req r{ z, x, y };
  if (xQueueSend(s_reqQ, &r, 0) == pdTRUE) {
    s_recent[s_recentHead] = r;
    s_recentHead = (s_recentHead + 1) % 48;
  }
}

bool pollTile(TileDone& out) { return s_doneQ && xQueueReceive(s_doneQ, &out, 0) == pdTRUE; }
uint16_t tilesFetched() { return s_ok; }
uint16_t tilesFailed() { return s_fail; }
int lastHttpCode() { return s_lastCode; }

}  // namespace wifi
