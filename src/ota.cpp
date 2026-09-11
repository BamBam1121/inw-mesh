#include "ota.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <ed_25519.h>
#include "app.h"
#include "netwifi.h"
#include "settings.h"
#include "logstore.h"
#include "ui.h"

extern LogStore logs;
void inwProgress(const char* what, uint32_t done, uint32_t total);   // main.cpp

namespace ota {

static const char* SITE = "https://bambam1121.github.io/inw-mesh/firmware/";

// Public half of the release signing key. Releases signed with anything else
// are refused.
static const uint8_t RELEASE_KEY[32] = {
  0x76, 0x91, 0xb2, 0x81, 0x6f, 0xbb, 0x12, 0x76, 0xdc, 0xd0, 0x4a, 0x63, 0x26, 0xa6, 0xbc, 0xb5,
  0x47, 0xb2, 0xec, 0xe6, 0xa6, 0x00, 0x56, 0xa2, 0xb7, 0x8f, 0x97, 0x29, 0xef, 0xb4, 0x9b, 0x12,
};

static uint8_t s_sha[32], s_sig[64];     // from the last good check

bool supported() {
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  return next && next != esp_ota_get_running_partition();
}

// "1.2.10" > "1.2.9"
static bool isNewer(const char* remote, const char* local) {
  int r[3] = {0}, l[3] = {0};
  sscanf(remote, "%d.%d.%d", &r[0], &r[1], &r[2]);
  sscanf(local, "%d.%d.%d", &l[0], &l[1], &l[2]);
  for (int i = 0; i < 3; i++) if (r[i] != l[i]) return r[i] > l[i];
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

Info check() {
  Info info;
  if (!wifi::connected()) { strlcpy(info.error, "connect to wi-fi first", sizeof(info.error)); return info; }
  WiFiClientSecure tls;
  tls.setInsecure();                     // authenticity comes from the signature, not TLS
  HTTPClient http;
  http.setTimeout(8000);
  String url = String(SITE) + "ota.json";
  if (!http.begin(tls, url)) { strlcpy(info.error, "couldn't reach the update site", sizeof(info.error)); return info; }
  const int code = http.GET();
  if (code != 200) {
    snprintf(info.error, sizeof(info.error), "update site said %d", code);
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
  if (!info.version[0] || !info.size || !fromHex(doc["sha256"] | "", s_sha, 32) || !fromHex(doc["sig"] | "", s_sig, 64)) {
    strlcpy(info.error, "update info incomplete", sizeof(info.error));
    return info;
  }
  if (!ed25519_verify(s_sig, s_sha, 32, RELEASE_KEY)) {
    strlcpy(info.error, "update signature is not valid", sizeof(info.error));
    logs.add(LOG_WARN, "ota: bad signature on %s, ignored", info.version);
    return info;
  }
  info.ok = true;
  info.newer = isNewer(info.version, FW_VERSION);
  return info;
}

const char* install(const Info& info) {
  static char msg[48];
  if (!info.ok || !info.newer) return "no update to install";
  if (!supported()) return "needs a one-time usb reinstall first";
  WiFiClientSecure tls;
  tls.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(tls, String(SITE) + "firmware.bin")) return "couldn't reach the update site";
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

// Once per boot, a while after Wi-Fi comes up, so it doesn't compete with startup.
void tick() {
  static bool done = false;
  static uint32_t connectedAt = 0;
  if (done || !ui_settings.autoUpdateCheck) return;
  if (!wifi::connected()) { connectedAt = 0; return; }
  if (!connectedAt) { connectedAt = millis(); return; }
  if (millis() - connectedAt < 20000) return;
  done = true;
  const Info info = check();
  if (!info.ok) { logs.add(LOG_INFO, "update check: %s", info.error); return; }
  if (!info.newer) { logs.add(LOG_INFO, "update check: up to date (%s)", FW_VERSION); return; }
  logs.add(LOG_INFO, "update available: %s", info.version);
  if (!supported()) { nav.banner("Update available", "reinstall once over usb to enable wi-fi updates", 6000); return; }
  const String body = String("version ") + info.version + (info.notes[0] ? String(" - ") + info.notes : String("")) +
                      ". takes about a minute; messages pause while it downloads.";
  confirm(String("Update to ") + info.version + "?", body, [info] { nav.toast(install(info), 5000); });
}

}  // namespace ota
