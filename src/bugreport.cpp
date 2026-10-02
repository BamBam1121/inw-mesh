// Problem reports (bugreport.h).
#include "bugreport.h"
#include <SPIFFS.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_core_dump.h>
#include <esp_random.h>
#include "logstore.h"
#include "app.h"
#include "netwifi.h"
#include "backlight.h"     // dimmer.idleFor(): send in a gap, not mid-scroll
#include "node.h"          // bleConnected()
#include "board_pins.h"    // REPORT_BOARD

#ifndef REPORT_BOARD
#define REPORT_BOARD "t-lora-pager"
#endif

extern LogStore logs;

namespace report {

static const char* URL = "https://squatchmesh.com/api/help/report";
static const char* COUNT_URL = "https://squatchmesh.com/api/help/count";
static const char* DIR = "/rpt";
static constexpr uint8_t MAX_WAITING = 4;
static constexpr uint8_t MAX_PER_BOOT = 6;          // a crash loop can't flood the inbox
static constexpr uint8_t ERRORS_PER_BOOT = 2;       // LOG_ERROR lines that file a report

// ---- the last run's log, kept through a crash --------------------------------------------
// RTC memory that a restart (but not a power cut) leaves alone. Every log line is
// copied here as it's written, so after a crash the lines leading up to it survive.
static constexpr uint32_t MAGIC = 0x52505431;       // "RPT1"
static constexpr uint8_t  LINES = 40, W = 52;
struct Mirror { uint32_t magic; uint8_t head, count; char line[LINES][W]; };
RTC_NOINIT_ATTR static Mirror s_m;

static String s_prev;          // the last run's lines, oldest first, read at capture()
static int8_t s_on = -1;
static uint8_t s_sent = 0, s_errors = 0;
static bool s_mounted = false;
static char s_errWhy[40] = "";   // a LOG_ERROR waiting for tick() to file it

// Lines that could carry something personal are cut to what's useful for a bug.
static void scrub(const char* in, char* out, size_t n) {
  if (!strncmp(in, "wifi ", 5) && strcmp(in, "wifi on") && strcmp(in, "wifi off") && strncmp(in, "wifi scan", 9)) {
    const char* why = strstr(in, ": ");                // "wifi <name>: wrong password (reason 15)"
    snprintf(out, n, "wifi%s%s", why ? ":" : " joined", why ? why + 1 : "");
    return;
  }
  if (!strncmp(in, "sos sent", 8)) {                   // "sos sent (3) on <channel>"
    const char* on = strstr(in, " on ");
    snprintf(out, n, "%.*s", on ? (int)(on - in) : (int)strlen(in), in);
    return;
  }
  if (!strncmp(in, "trail started: ", 15)) { strlcpy(out, "trail started (sd)", n); return; }
  const char* gap = strstr(in, "  ");                  // "<node name>  812 contacts" at boot
  const size_t len = strlen(in);
  if (gap && len > 9 && !strcmp(in + len - 9, " contacts")) { snprintf(out, n, "node up, %s", gap + 2); return; }
  strlcpy(out, in, n);
}

static void onLog(LogLevel lv, const char* text) {
  char clean[W - 12];
  scrub(text, clean, sizeof(clean));
  snprintf(s_m.line[s_m.head], W, "%6lu %c %s", (unsigned long)(millis() / 1000), "IWE"[lv], clean);
  s_m.head = (s_m.head + 1) % LINES;
  if (s_m.count < LINES) s_m.count++;
  // A real error (the radio failing to start, an SOS with nowhere to go) files a
  // report too - twice a boot at most, and not while a crash report is the story.
  // (Filed from tick(): a log line can be written from anywhere, mid-save included.)
  if (lv == LOG_ERROR && s_errors < ERRORS_PER_BOOT && !s_errWhy[0]) {
    s_errors++;
    strlcpy(s_errWhy, clean, sizeof(s_errWhy));
  }
}

static void recentLines(String& out, const Mirror& m) {
  for (uint8_t i = 0; i < m.count; i++) {
    const uint8_t idx = (m.head + LINES - m.count + i) % LINES;
    char l[W];
    memcpy(l, m.line[idx], W);
    l[W - 1] = 0;
    out += l; out += '\n';
  }
}

void capture() {
  if (s_m.magic == MAGIC && s_m.count <= LINES && s_m.head < LINES) recentLines(s_prev, s_m);
  memset(&s_m, 0, sizeof(s_m));
  s_m.magic = MAGIC;
  logs.onAdd = onLog;
}

// ---- settings ------------------------------------------------------------------------------
bool enabled() {
  if (s_on < 0) {
    Preferences p;
    s_on = 1;                                          // on for the beta
    if (p.begin("inw-rpt", true)) { s_on = p.getBool("on", true) ? 1 : 0; p.end(); }
  }
  return s_on == 1;
}

void setEnabled(bool on) {
  s_on = on ? 1 : 0;
  Preferences p;
  if (p.begin("inw-rpt", false)) { p.putBool("on", on); p.end(); }
}

// A random id for this device: what the daily check-in is counted by, and what ties
// a device's reports together if its name changes.
static String deviceId() {
  Preferences p;
  String id;
  if (p.begin("inw-rpt", false)) {
    id = p.getString("id", "");
    if (id.length() != 8) {
      char b[9];
      snprintf(b, sizeof(b), "%08lx", (unsigned long)esp_random());
      id = b;
      p.putString("id", id);
    }
    p.end();
  }
  return id;
}

// ---- the store ---------------------------------------------------------------------------
// How many reports are waiting to go. Counted from the store once, then kept: SPIFFS
// has no real folders, so finding ours means listing every file there is, and with a
// thousand contacts' worth of files that takes over a second. tick() used to do that
// every minute, and the whole device stood still for it each time.
static int16_t s_waiting = -1;              // -1: not counted yet
uint8_t waiting() {
  if (!s_mounted) return 0;
  if (s_waiting < 0) {
    int16_t n = 0;
    File root = SPIFFS.open("/");
    for (File f = root.openNextFile(); f; f = root.openNextFile())
      if (!strncmp(f.path(), DIR, 4)) n++;
    s_waiting = n;
  }
  return (uint8_t)min<int16_t>(s_waiting, 255);
}

static bool save(const String& json) {
  if (!s_mounted || waiting() >= MAX_WAITING) return false;
  char path[24];
  snprintf(path, sizeof(path), "%s%08lx.json", DIR, (unsigned long)esp_random());
  File f = SPIFFS.open(path, FILE_WRITE);
  if (!f) return false;
  const bool ok = f.print(json) == json.length();
  f.close();
  if (!ok) SPIFFS.remove(path);
  else s_waiting++;
  return ok;
}

static void jsonEscape(String& out, const String& in) {
  for (size_t i = 0; i < in.length(); i++) {
    const char c = in[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if ((uint8_t)c < 0x20) out += ' ';
    else out += c;
  }
}

static const char* resetName(int r) {
  switch (r) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    default: return "unknown";
  }
}

// Everything but the log: what it's running and how it's doing.
static String header(const char* kind, const char* why) {
  String j = "{\"kind\":\"";
  j += kind;
  j += "\",\"board\":\"" REPORT_BOARD "\",\"version\":\"" FW_VERSION "\",\"device\":\"";
  j += deviceId();
  j += "\",\"why\":\"";
  jsonEscape(j, String(why ? why : ""));
  j += "\",\"reset\":\"";
  j += resetName((int)esp_reset_reason());
  char b[200];
  snprintf(b, sizeof(b), "\",\"uptime_s\":%lu,\"heap_free\":%u,\"heap_min\":%u,\"psram_free\":%u,"
           "\"battery\":%u,\"charging\":%s,\"radio\":%s",
           (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
           (unsigned)ESP.getFreePsram(), (unsigned)app::batteryPct(), app::pluggedIn() ? "true" : "false",
           app::radioOk() ? "true" : "false");
  j += b;
  return j;
}

bool sendLog(const char* why) {
  Mirror m;
  memcpy(&m, &s_m, sizeof(m));
  String lines;
  recentLines(lines, m);
  String j = header("log", why);
  j += ",\"log\":\"";
  jsonEscape(j, lines);
  j += "\"}";
  return save(j);
}

// A restart that came from a crash, a watchdog or the power sagging: file what the
// core dump knows, with the lines the last run wrote before it went.
void begin() {
  s_mounted = true;
  const int rr = (int)esp_reset_reason();
  const bool crashed = rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT ||
                       rr == ESP_RST_WDT || rr == ESP_RST_BROWNOUT;
  const bool dump = esp_core_dump_image_check() == ESP_OK;
  if (!crashed && !dump) return;
  if (!enabled()) { if (dump) esp_core_dump_image_erase(); return; }
  String j = header("crash", resetName(rr));
  if (dump) {
    esp_core_dump_summary_t* s = (esp_core_dump_summary_t*)calloc(1, sizeof(esp_core_dump_summary_t));
    if (s && esp_core_dump_get_summary(s) == ESP_OK) {
      char b[96];
      String task;
      jsonEscape(task, String(s->exc_task));
      snprintf(b, sizeof(b), ",\"task\":\"%s\",\"pc\":\"0x%08lx\",\"cause\":%lu,\"vaddr\":\"0x%08lx\"",
               task.c_str(), (unsigned long)s->exc_pc, (unsigned long)s->ex_info.exc_cause,
               (unsigned long)s->ex_info.exc_vaddr);
      j += b;
      j += ",\"backtrace\":\"";
      for (uint32_t i = 0; i < s->exc_bt_info.depth && i < 16; i++) {
        snprintf(b, sizeof(b), "%s0x%08lx", i ? " " : "", (unsigned long)s->exc_bt_info.bt[i]);
        j += b;
      }
      if (s->exc_bt_info.corrupted) j += " |corrupted";
      j += "\",\"elf_sha\":\"";
      j += String((const char*)s->app_elf_sha256).substring(0, 16);
      j += "\"";
    }
    free(s);
    esp_core_dump_image_erase();             // one report per crash
  }
  j += ",\"log\":\"";
  jsonEscape(j, s_prev.length() ? s_prev : String("(the log didn't survive: the power was cut)"));
  j += "\"}";
  save(j);
  s_prev = String();
}

// ---- sending -----------------------------------------------------------------------------
static bool post(const String& body, const char* url = URL) {
  WiFiClientSecure tls;
  tls.setInsecure();                          // nothing secret in it; the server checks the shape
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(tls, url)) return false;
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(body);
  http.end();
  // 2xx: it landed. 4xx: the server won't ever take this one (too big, rate limit
  // for today): drop it rather than retry forever. Anything else: try later.
  return code >= 200 && code < 500;
}

// Once a day (by the clock, or once a boot until the clock is set): "a device on
// this version is in use". Board, version, the random id - nothing else.
static bool s_checkedThisBoot = false;
static bool checkInDue() {
  if (!app::timeValid()) return !s_checkedThisBoot;
  Preferences p;
  uint32_t last = 0;
  if (p.begin("inw-rpt", true)) { last = p.getUInt("chk", 0); p.end(); }
  const uint32_t now = app::now();
  return now < last || now - last >= 86400UL;
}

static void checkIn() {
  const String body = String("{\"event\":\"checkin\",\"board\":\"" REPORT_BOARD "\",\"version\":\"" FW_VERSION
                             "\",\"device\":\"") + deviceId() + "\"}";
  post(body, COUNT_URL);                      // whatever it answers: one try a day is plenty
  s_checkedThisBoot = true;
  Preferences p;
  if (app::timeValid() && p.begin("inw-rpt", false)) { p.putUInt("chk", app::now()); p.end(); }
}

// Whose report it is: the name the device goes by on the mesh, so the developer can
// tell who to ask about it. Put on as it is sent (the node isn't up yet when a crash
// is filed). The daily check-in never carries it.
static String withName(const String& body) {
  if (!g_node || body.length() < 2 || body[0] != '{') return body;
  String j = "{\"name\":\"";
  jsonEscape(j, String(g_node->name()));
  j += "\",";
  j += body.substring(1);
  return j;
}

void tick() {
  static uint32_t lastTry = 0, connectedAt = 0, lastLook = 0;
  if (!s_mounted || !enabled()) { s_errWhy[0] = 0; return; }
  if (s_errWhy[0]) { sendLog(s_errWhy); s_errWhy[0] = 0; }
  if (!wifi::connected()) { connectedAt = 0; return; }
  if (!connectedAt) { connectedAt = millis(); return; }
  if (millis() - connectedAt < 30000) return;           // after the update check has had its go
  if (lastTry && millis() - lastTry < 60000) return;
  if (dimmer.idleFor() < 4000 || bleConnected()) return;    // a blocking second: not mid-scroll
  if (!lastLook || millis() - lastLook > 3600000UL) {   // looked at hourly, sent daily
    lastLook = millis();
    if (checkInDue()) { lastTry = millis(); checkIn(); return; }
  }
  if (s_sent >= MAX_PER_BOOT) return;
  lastTry = millis();
  if (!waiting()) return;                               // nothing to send: no listing of the store
  File root = SPIFFS.open("/");
  String path;
  for (File f = root.openNextFile(); f; f = root.openNextFile())
    if (!strncmp(f.path(), DIR, 4)) { path = f.path(); break; }
  root.close();
  if (!path.length()) { s_waiting = 0; return; }
  File f = SPIFFS.open(path, FILE_READ);
  if (!f) return;
  const String body = f.readString();
  f.close();
  if (!body.length() || post(withName(body))) {
    SPIFFS.remove(path);
    if (s_waiting > 0) s_waiting--;
    s_sent++;
    Serial.printf("[report] sent %s\n", path.c_str());
  }
}

}  // namespace report
