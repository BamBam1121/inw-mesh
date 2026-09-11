#include "netwifi.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <esp_heap_caps.h>
#include "settings.h"
#include "logstore.h"

extern LogStore logs;
namespace app { void setTime(uint32_t epoch); }

namespace wifi {

struct Saved { char ssid[33]; char pass[65]; };
static Saved s_saved[SAVED_MAX];
static uint8_t s_count = 0;
static bool s_on = false, s_scanning = false;
static int s_scanResults = -1;
static uint8_t s_try = 0;
static uint32_t s_nextTry = 0;
static bool s_wasConnected = false;
static volatile bool s_ntpSynced = false;

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
  // Prefer a saved network the last scan actually saw, strongest first.
  int best = -1, bestRssi = -1000;
  const int n = WiFi.scanComplete();
  for (int i = 0; i < n; i++) {
    for (uint8_t k = 0; k < s_count; k++) {
      if (WiFi.SSID(i) == s_saved[k].ssid && WiFi.RSSI(i) > bestRssi) { best = k; bestRssi = WiFi.RSSI(i); }
    }
  }
  const uint8_t k = best >= 0 ? best : (s_try++ % s_count);
  WiFi.begin(s_saved[k].ssid, s_saved[k].pass);
}

void begin() {
  load();
  if (ui_settings.wifiOn) setEnabled(true);
}

void setEnabled(bool on) {
  if (on == s_on) return;
  s_on = on;
  if (on) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);                 // modem sleep: coexists with BLE, saves power
    WiFi.setAutoReconnect(false);        // we pick the network ourselves
    s_nextTry = millis();
    logs.add(LOG_INFO, "wifi on");
  } else {
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
  if (!s_count) return "no saved networks - scan to add one";
  return "searching";
}

void tick() {
  if (!s_on) return;
  const bool c = WiFi.status() == WL_CONNECTED;
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
  if (!c && !s_scanning && (int32_t)(millis() - s_nextTry) >= 0) {
    s_nextTry = millis() + 20000;
    connectNext();
  }
  if (s_scanning && WiFi.scanComplete() >= 0) { s_scanning = false; s_scanResults = WiFi.scanComplete(); }
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
  WiFi.disconnect();
  WiFi.begin(s_saved[slot].ssid, s_saved[slot].pass);
  s_nextTry = millis() + 20000;
}

void forget(uint8_t i) {
  if (i >= s_count) return;
  memmove(&s_saved[i], &s_saved[i + 1], sizeof(Saved) * (s_count - i - 1));
  s_count--;
  memset(&s_saved[s_count], 0, sizeof(Saved));
  store();
}

void startScan() {
  if (!s_on) setEnabled(true);
  WiFi.scanDelete();
  s_scanResults = -1;
  s_scanning = WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING;
}
bool scanDone() { return !s_scanning && s_scanResults >= 0; }
int scanCount() { return s_scanResults < 0 ? 0 : s_scanResults; }
const char* scanSsid(int i) { static char b[33]; strlcpy(b, WiFi.SSID(i).c_str(), sizeof(b)); return b; }
int scanRssi(int i) { return WiFi.RSSI(i); }
bool scanOpen(int i) { return WiFi.encryptionType(i) == WIFI_AUTH_OPEN; }

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
    http.addHeader("User-Agent", "INW-Pager/1.0 (LilyGo T-Lora Pager mesh firmware; github.com/BamBam1121)");
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
