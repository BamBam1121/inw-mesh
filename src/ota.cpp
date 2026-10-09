#include "ota.h"
#include <HTTPClient.h>
#include "tls_client.h"   // WiFiClientSecure without its stray close(0)
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <ed_25519.h>
#include "app.h"
#include "netwifi.h"
#include "settings.h"
#include "logstore.h"
#include "ui.h"
#include "backlight.h"     // dimmer.idleFor(): only check for updates in a gap
#include "board_pins.h"    // OTA_SUBDIR, OTA_BOARD
#include "fieldtools.h"    // field::wantsGps(): an SOS or range test isn't interrupted
#include "node.h"          // bleConnected()
#include <Preferences.h>

extern LogStore logs;
void inwProgress(const char* what, uint32_t done, uint32_t total);   // main.cpp

bool reportPosting();   // bugreport.cpp: a problem report is out on the network (the two take turns)

namespace ota {

// Each board has its own folder; the pager's is the original, top-level one.
#ifndef OTA_SUBDIR
#define OTA_SUBDIR ""
#endif
static const char* SITE = "https://bambam1121.github.io/inw-mesh/firmware/" OTA_SUBDIR;

// Public half of the release signing key. Releases signed with anything else
// are refused.
static const uint8_t RELEASE_KEY[32] = {
  0x76, 0x91, 0xb2, 0x81, 0x6f, 0xbb, 0x12, 0x76, 0xdc, 0xd0, 0x4a, 0x63, 0x26, 0xa6, 0xbc, 0xb5,
  0x47, 0xb2, 0xec, 0xe6, 0xa6, 0x00, 0x56, 0xa2, 0xb7, 0x8f, 0x97, 0x29, 0xef, 0xb4, 0x9b, 0x12,
};

static uint8_t s_sha[32];                // from the last good check
#ifndef OTA_BOARD
static uint8_t s_sig[64];
#endif

bool supported() {
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  return next && next != esp_ota_get_running_partition();
}

// An update is written to the other app slot. On a board set up with a multi-boot
// launcher that slot holds another firmware, and updates used to replace it without a
// word. So the slot is looked at first: it starts like an app (0xE9) and nowhere says
// any of the things every build of this firmware says (the update site it has had since
// it could update itself, its boot line, the help site) - someone else's. Read once per
// start: a few megabytes of flash, a second at most. Anything unclear (a read that
// fails) counts as ours: the update goes ahead as it always did.
bool replacesOther() {
  static int8_t known = -1;
  if (known >= 0) return known;
  known = 0;
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (!next || next == esp_ota_get_running_partition()) return false;
  static const char* const MARKS[] = {"github.io/inw-mesh", "squatchmesh.com", "[INW] boot"};
  const size_t ML = 18, CHUNK = 4096;                              // ML: the longest of them
  uint8_t* buf = (uint8_t*)malloc(CHUNK + ML);
  if (!buf) return false;
  bool app = false, ours = false, whole = true;
  size_t carry = 0;
  for (size_t off = 0; off < next->size && !ours; off += CHUNK) {
    const size_t n = min(CHUNK, (size_t)(next->size - off));
    if (esp_partition_read(next, off, buf + carry, n) != ESP_OK) { whole = false; break; }
    if (off == 0) { app = buf[0] == 0xE9; if (!app) break; }       // empty, or not firmware at all
    const size_t have = carry + n;
    for (const char* m : MARKS) {
      const size_t l = strlen(m);
      for (size_t i = 0; i + l <= have && !ours; i++)
        if (buf[i] == (uint8_t)m[0] && !memcmp(buf + i, m, l)) ours = true;
    }
    carry = min(ML - 1, have);                                     // a marker across two chunks
    memmove(buf, buf + have - carry, carry);
  }
  free(buf);
  known = app && whole && !ours;
  if (known) logs.add(LOG_WARN, "another firmware is in the update slot (%s): updates won't replace it by themselves", next->label);
  return known;
}

// "1.2.10" > "1.2.9", and a release comes after its betas:
// "1.2.2" > "1.2.2-beta2" > "1.2.2-beta1". Pager versions have no suffix.
static void parseVersion(const char* s, int v[4]) {
  sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]);
  const char* b = strstr(s, "-beta");
  v[3] = b ? atoi(b + 5) : 1000000;
}
static bool isNewer(const char* remote, const char* local) {
  int r[4] = {0}, l[4] = {0};
  parseVersion(remote, r);
  parseVersion(local, l);
  for (int i = 0; i < 4; i++) if (r[i] != l[i]) return r[i] > l[i];
  return false;
}

static bool fromHex(const char* s, uint8_t* out, size_t n) {
  if (!s || strlen(s) != n * 2) return false;
  for (size_t i = 0; i < n; i++) {
    unsigned v;
    if (sscanf(s + i * 2, "%2x", &v) != 1) return false;
    out[i] = (uint8_t)v;
  }
  return true;
}

// The check is seconds of waiting on the network - the secure handshake alone is most
// of ten at the speed the chip idles at with the screen dark - and from the loop that
// was the screen and the radio frozen for it. The automatic check now runs fetch() in a
// task of its own (tick); the log isn't safe from there, so what fetch() has to say
// goes into s_warn and is logged by whoever takes the result.
static char s_warn[160] = "";
static void flushWarn() { if (s_warn[0]) { logs.add(LOG_WARN, "%s", s_warn); s_warn[0] = 0; } }
static Info fetch(bool beta);

Info check() { return check(ui_settings.betaUpdates); }
Info check(bool beta) { const Info i = fetch(beta); flushWarn(); return i; }

static Info fetch(bool beta) {
  Info info;
  if (!wifi::connected()) { strlcpy(info.error, "connect to wi-fi first", sizeof(info.error)); return info; }
  TlsClient tls;
  tls.setInsecure();                     // authenticity comes from the signature, not TLS
  HTTPClient http;
  http.setTimeout(8000);
  info.beta = beta;
  String url = String(SITE) + (info.beta ? "ota-beta.json" : "ota.json");
  if (!http.begin(tls, url)) { strlcpy(info.error, "couldn't reach the update site", sizeof(info.error)); return info; }
  int code = http.GET();
  if (code < 0) {
    // Below zero the site never answered: the connection couldn't be opened or dropped
    // (-1 is "refused", which is also what a TLS handshake short of memory looks like).
    // Note what there was to work with, and try once more.
    snprintf(s_warn, sizeof(s_warn), "update check: no connection (%d %s), internal RAM %u kB free, largest %u kB", code,
             HTTPClient::errorToString(code).c_str(),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
    http.end();
    delay(400);
    if (http.begin(tls, url)) code = http.GET();
  }
  if (code != 200) {
#ifdef OTA_BOARD
    if (code == 404) strlcpy(info.error, "no updates for this device yet", sizeof(info.error));
    else
#endif
    if (code < 0) snprintf(info.error, sizeof(info.error), "couldn't connect to the update site (%d)", code);
    else snprintf(info.error, sizeof(info.error), "update site said %d", code);
    http.end();
    return info;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) { strlcpy(info.error, "update info unreadable", sizeof(info.error)); return info; }
  strlcpy(info.version, doc["version"] | "", sizeof(info.version));
  strlcpy(info.notes, doc["notes"] | "", sizeof(info.notes));
  info.size = doc["size"] | 0;
#ifdef OTA_BOARD
  // Boards after the pager are signed with "sig3" alone, which binds the board as
  // well as the hash, version and size. Their ota.json has neither of the pager's
  // signatures, so no pager - however old its firmware - can take one for its own;
  // and the pager's ota.json has no sig3, so this board never takes the pager's.
  uint8_t sig3[64];
  if (!info.version[0] || !info.size || !fromHex(doc["sha256"] | "", s_sha, 32) || !fromHex(doc["sig3"] | "", sig3, 64)) {
    strlcpy(info.error, doc["sig2"].is<const char*>() ? "update is not for this device" : "update info incomplete",
            sizeof(info.error));
    return info;
  }
  char tail[48];
  const int tl = snprintf(tail, sizeof(tail), "%s\n%lu", info.version, (unsigned long)info.size);
  static const char PREFIX3[] = "squatch-ota-v3\n" OTA_BOARD "\n";
  uint8_t msg3[sizeof(PREFIX3) - 1 + 32 + sizeof(tail)];
  size_t m3 = 0;
  memcpy(msg3, PREFIX3, sizeof(PREFIX3) - 1); m3 += sizeof(PREFIX3) - 1;
  memcpy(msg3 + m3, s_sha, 32); m3 += 32;
  if (tl > 0 && tl < (int)sizeof(tail)) { memcpy(msg3 + m3, tail, tl); m3 += tl; }
  if (tl <= 0 || tl >= (int)sizeof(tail) || !ed25519_verify(sig3, msg3, m3, RELEASE_KEY)) {
    strlcpy(info.error, "update is not for this device", sizeof(info.error));
    snprintf(s_warn, sizeof(s_warn), "ota: %s is not signed for " OTA_BOARD ", ignored", info.version);
    return info;
  }
#else
  if (!info.version[0] || !info.size || !fromHex(doc["sha256"] | "", s_sha, 32) || !fromHex(doc["sig"] | "", s_sig, 64)) {
    strlcpy(info.error, "update info incomplete", sizeof(info.error));
    return info;
  }
  // "sig" signs only the firmware hash, so an older signed release could be served
  // relabelled as a newer version. "sig2" also binds the version and size; require it.
  uint8_t sig2[64];
  if (!fromHex(doc["sig2"] | "", sig2, 64)) {
    strlcpy(info.error, "update info incomplete", sizeof(info.error));
    return info;
  }
  char tail[48];
  const int tl = snprintf(tail, sizeof(tail), "%s\n%lu", info.version, (unsigned long)info.size);
  static const char PREFIX[] = "squatch-ota-v2\n";
  uint8_t msg[sizeof(PREFIX) - 1 + 32 + sizeof(tail)];
  size_t ml = 0;
  memcpy(msg, PREFIX, sizeof(PREFIX) - 1); ml += sizeof(PREFIX) - 1;
  memcpy(msg + ml, s_sha, 32); ml += 32;
  memcpy(msg + ml, tail, tl); ml += tl;
  if (tl <= 0 || tl >= (int)sizeof(tail) || !ed25519_verify(s_sig, s_sha, 32, RELEASE_KEY) ||
      !ed25519_verify(sig2, msg, ml, RELEASE_KEY)) {
    strlcpy(info.error, "update signature is not valid", sizeof(info.error));
    snprintf(s_warn, sizeof(s_warn), "ota: bad signature on %s, ignored", info.version);
    return info;
  }
#endif
  info.ok = true;
  info.newer = isNewer(info.version, FW_VERSION);
  return info;
}

const char* install(const Info& info) {
  static char msg[48];
  if (!info.ok || !info.newer) return "no update to install";
  if (!supported()) return "needs a one-time usb reinstall first";
  TlsClient tls;
  tls.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(tls, String(SITE) + (info.beta ? "firmware-beta.bin" : "firmware.bin"))) return "couldn't reach the update site";
  const int code = http.GET();
  if (code != 200) { http.end(); snprintf(msg, sizeof(msg), "download failed (%d)", code); return msg; }
  const int len = http.getSize();
  if (len != (int)info.size) { http.end(); return "download size doesn't match"; }
  if (!Update.begin(len, U_FLASH)) { http.end(); return "not enough room for the update"; }

  char title[40];
  snprintf(title, sizeof(title), "updating to %s", info.version);
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  WiFiClient* st = http.getStreamPtr();
  static uint8_t buf[4096];
  int got = 0;
  uint32_t lastData = millis();
  bool ok = true;
  while (got < len) {
    const int avail = st->available();
    if (!avail) {
      if (!http.connected() || millis() - lastData > 15000) { ok = false; break; }
      delay(2);
      continue;
    }
    const int n = st->readBytes(buf, min(avail, (int)sizeof(buf)));
    if (n <= 0) continue;
    lastData = millis();
    mbedtls_sha256_update(&sha, buf, n);
    if (Update.write(buf, n) != (size_t)n) { ok = false; break; }
    got += n;
    inwProgress(title, got, len);
  }
  http.end();
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  inwProgress(nullptr, 0, 0);
  if (!ok || got != len) { Update.abort(); return "download interrupted, nothing changed"; }
  if (memcmp(digest, s_sha, 32) != 0) {
    Update.abort();
    logs.add(LOG_WARN, "ota: hash mismatch on %s, discarded", info.version);
    return "download was corrupted, nothing changed";
  }
  if (!Update.end(true)) { snprintf(msg, sizeof(msg), "couldn't finish: %s", Update.errorString()); return msg; }
  logs.add(LOG_INFO, "ota: installed %s, rebooting", info.version);
  nav.busy("updated - restarting");
  delay(1200);
  app::reboot();
  return "restarting";
}

// ---- installing by itself ------------------------------------------------------------------------
// Kept in its own NVS namespace, not the settings blob, so its layout is untouched.
static int8_t s_auto = -1;                  // -1: not read yet

bool autoInstall() {
  if (s_auto < 0) {
    Preferences p;
    s_auto = 1;                             // on unless it's been turned off
    if (p.begin("inw-ota", true)) { s_auto = p.getBool("auto", true) ? 1 : 0; p.end(); }
  }
  return s_auto == 1;
}

void setAutoInstall(bool on) {
  s_auto = on ? 1 : 0;
  Preferences p;
  if (p.begin("inw-ota", false)) { p.putBool("auto", on); p.end(); }
}

// An update the owner said no to isn't offered again: not at the next restart, not
// six hours later. Settings > System > check for updates still offers it, and a
// newer version asks afresh.
static String declined() {
  Preferences p;
  String v;
  if (p.begin("inw-ota", true)) { v = p.getString("no", ""); p.end(); }
  return v;
}
static void setDeclined(const char* version) {
  Preferences p;
  if (p.begin("inw-ota", false)) { p.putString("no", version); p.end(); }
}

void announce() {
  Preferences p;
  if (!p.begin("inw-ota", false)) return;
  const String was = p.getString("ran", "");
  if (was != FW_VERSION) {
    p.putString("ran", FW_VERSION);
    if (was.length()) {
      logs.add(LOG_INFO, "updated: %s -> %s", was.c_str(), FW_VERSION);
      nav.banner("Updated", (String("now on ") + FW_VERSION + ", everything kept").c_str(), 6000);
    }
  }
  p.end();
}

// Nobody is using it: screen off and untouched for two minutes, enough battery to
// finish (or on a charger), and nothing running that a restart would cut short.
static bool idleForUpdate() {
  return dimmer.asleep() && dimmer.idleFor() > 120000UL &&
         (app::pluggedIn() || app::batteryPct() >= 30) &&
         !field::wantsGps() &&                  // an SOS, range test or trail
         !bleConnected();                        // the phone app mid-sync
}

// The question is put only where it can be answered on purpose: on the home screen,
// lit and unlocked. Never over the lock screen (a key pressed in a pocket could
// answer it) and never over a message being typed (a "y" or "n" in it would).
static bool canAsk() {
  return !dimmer.asleep() && nav.top() && nav.top()->isHome() && !nav.overlayActive();
}

// A while after Wi-Fi comes up, so it doesn't compete with startup; once per boot, or
// every 6 hours where updates install by themselves.
// The automatic check, in its task: 0 not running, 1 under way, 2 answer ready.
static volatile uint8_t s_bg = 0;
static Info s_bgInfo;
static bool s_bgAlsoRelease = false;
static void checkTask(void*) {
  Info info = fetch(ui_settings.betaUpdates);
  if (s_bgAlsoRelease && info.beta) {            // see tick(): a newer release goes in by itself
    const Info rel = fetch(false);
    if (rel.ok && rel.newer) info = rel;
  }
  s_bgInfo = info;
  s_bg = 2;
  vTaskDelete(nullptr);
}

bool checking() { return s_bg == 1; }

void tick() {
  static bool checked = false, pending = false, asking = false;
  static uint32_t connectedAt = 0, lastCheck = 0;
  static Info found;
  const bool autoOn = autoInstall();
  if (asking) {                                  // found one to ask about: wait for the home screen
    if (!canAsk()) return;
    asking = false;
    const Info info = found;
    // The notes usually name the version themselves ("1.2.11 beta: ..."): once is enough.
    const String what = !info.notes[0] ? String("version ") + info.version
                        : strstr(info.notes, info.version) ? String(info.notes)
                        : String(info.version) + ": " + info.notes;
    const String body = what + ". takes about a minute; messages pause while it downloads.";
    confirm(String("Update to ") + info.version + "?", body,
            [info] { nav.toast(install(info), 5000); },
            [info] {
              setDeclined(info.version);
              logs.add(LOG_INFO, "update %s declined", info.version);
              nav.toast("ok. it's in Settings > System when you want it", 4000);
            });
    return;
  }
  if (!ui_settings.autoUpdateCheck && !autoOn) return;
  if (!wifi::connected()) { connectedAt = 0; return; }
  if (!connectedAt) { connectedAt = millis(); return; }
  if (millis() - connectedAt < 20000) return;

  if (pending) {
    if (!autoOn) { pending = false; return; }  // turned off meanwhile: offer it next boot
    if (!idleForUpdate()) return;
    pending = false;
    logs.add(LOG_INFO, "installing %s by itself", found.version);
    const char* r = install(found);            // restarts when it works
    logs.add(LOG_WARN, "update %s: %s", found.version, r);
    lastCheck = millis();                      // try again at the next check
    return;
  }

  if (s_bg == 1) return;                         // the check is out: nothing to do until it's back
  if (!s_bg) {
    // One job on the network at a time: each takes a stack and about 40 kB of internal
    // RAM for its secure connection, and there are only about 85 kB to go round.
    if (reportPosting()) return;
    if (checked && !(autoOn && millis() - lastCheck > 6UL * 3600UL * 1000UL)) return;
    if (dimmer.idleFor() < 3000) return;         // not while someone is in the middle of something
    checked = true;
    lastCheck = millis();
#ifdef OTA_BOARD
    s_bgAlsoRelease = false;
#else
    // The pager puts in only official releases by itself. On beta updates it looks
    // at the release feed too: a newer release installs itself, a beta still asks.
    s_bgAlsoRelease = autoOn;
#endif
    s_bg = 1;
    if (xTaskCreatePinnedToCore(checkTask, "otacheck", 10240, nullptr, 1, nullptr, 0) != pdPASS) {
      s_bg = 0;                                  // no room for a task: as it used to be, on the loop
      Info i = fetch(ui_settings.betaUpdates);
      if (s_bgAlsoRelease && i.beta) { const Info rel = fetch(false); if (rel.ok && rel.newer) i = rel; }
      s_bgInfo = i;
      s_bg = 2;
    }
    if (s_bg != 2) return;
  }
  s_bg = 0;
  flushWarn();
  Info info = s_bgInfo;
#ifdef OTA_BOARD
  const bool autoThis = autoOn;                // every build of this board is a beta for now
#else
  const bool autoThis = autoOn && !info.beta;
#endif
  if (!info.ok) { logs.add(LOG_INFO, "update check: %s", info.error); return; }
  if (!info.newer) { logs.add(LOG_INFO, "update check: up to date (%s)", FW_VERSION); return; }
  logs.add(LOG_INFO, "update available: %s", info.version);
  if (!supported()) { nav.banner("Update available", "needs one USB reinstall first", 6000); return; }
  // A multi-boot setup: say so once and leave it to the owner (their launcher, or
  // Settings > System, which says what it replaces). Never by itself, and no prompt.
  if (replacesOther()) {
    static bool told = false;
    if (!told) nav.banner("Update available", "install it from your launcher", 8000);
    told = true;
    return;
  }
  if (autoThis) {                              // no questions: it goes in when nobody's using it
    found = info;
    pending = true;
    logs.add(LOG_INFO, "%s installs itself when idle", info.version);
    return;
  }
  if (declined() == info.version) { logs.add(LOG_INFO, "%s was declined: not asking again", info.version); return; }
  found = info;
  asking = true;
}

}  // namespace ota
