// NFC reader/writer and Type 4 tag emulation on the ST25R3916 (CS 39, IRQ 5).
// Driven by LilyGo's ST25R3916-NFC-RFAL, only while an NFC screen is open.
// Only built with INW_NFC (see README: ST's licence).
//
// MeshCore links use the app's QR format (docs.meshcore.io/qr_codes):
//   meshcore://contact/add?name=..&public_key=<64 hex>&type=N
//   meshcore://channel/add?name=..&secret=<32 hex>
// plus the bare meshcore://<hex> advert export.

#include <rfal_rfst25r3916.h>
#include <rfal_nfc.h>
#include <ndef_class.h>
#include <demo_ce.h>
#include "app.h"
#include "node.h"
#include "netwifi.h"
#include "haptic.h"
#include "board_pins.h"

// ---- hardware -----------------------------------------------------------------------------
namespace {

enum class Mode : uint8_t { Off, Read, Write, Emulate };

RfalRfST25R3916Class* s_rf = nullptr;
RfalNfcClass* s_nfc = nullptr;
NdefClass* s_ndef = nullptr;
bool s_ready = false, s_failed = false;
Mode s_mode = Mode::Off;
rfalNfcDiscoverParam s_disc;
bool s_handled = false, s_primed = false;
uint8_t* s_rx = nullptr;
uint16_t* s_rxLen = nullptr;
uint8_t s_tx[RFAL_NFC_RF_BUF_LEN];
uint8_t s_ceNfcid2[RFAL_NFCF_NFCID2_LEN] = {0x02, 0xFE, 0x49, 0x4E, 0x57, 0x50, 0x47, 0x52};

// What the screens see.
struct TagResult {
  bool fresh = false;
  uint32_t at = 0;
  char tech[16] = "";
  char uid[32] = "";
  bool hasNdef = false;
  uint8_t msg[1024];
  uint32_t msgLen = 0;
  uint32_t capacity = 0;
  bool writable = false;
};
TagResult s_tag;
uint8_t s_writeBuf[1024];
uint32_t s_writeLen = 0;
int s_writeResult = 0;          // 0 waiting, 1 ok, -1 failed
char s_writeErr[48] = "";
uint8_t s_ceFile[2 + 1024];
uint16_t s_ceReads = 0;

bool ensureInit() {
  if (s_ready) return true;
  if (s_failed) return false;
  s_rf = new RfalRfST25R3916Class(&inw_spi, PIN_NFC_CS, PIN_NFC_INT);
  s_nfc = new RfalNfcClass(s_rf);
  s_ndef = new NdefClass(s_nfc);
  inw_spi.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);   // no-op once the radio did it
  if (s_nfc->rfalNfcInitialize() != ST_ERR_NONE) { s_failed = true; return false; }
  s_ready = true;
  return true;
}

void onState(rfalNfcState st) {
  if (s_mode == Mode::Emulate) Serial.printf("[NFC] ce state %d\n", (int)st);
  if (st == RFAL_NFC_STATE_POLL_SELECT) {
    rfalNfcDevice* list = nullptr; uint8_t n = 0;
    if (s_nfc->rfalNfcGetDevicesFound(&list, &n) == ST_ERR_NONE && n) s_nfc->rfalNfcSelect(0);
  } else if (st == RFAL_NFC_STATE_START_DISCOVERY) {
    s_handled = false;
  }
}

void configPoll() {
  memset(&s_disc, 0, sizeof(s_disc));
  s_disc.compMode = RFAL_COMPLIANCE_MODE_NFC;
  s_disc.techs2Find = RFAL_NFC_POLL_TECH_A | RFAL_NFC_POLL_TECH_B | RFAL_NFC_POLL_TECH_F | RFAL_NFC_POLL_TECH_V;
  s_disc.totalDuration = 800;
  s_disc.devLimit = 1;
  s_disc.nfcfBR = RFAL_BR_212;
  s_disc.ap2pBR = RFAL_BR_424;
  s_disc.notifyCb = onState;
  s_disc.wakeupConfigDefault = true;
}

void configListen() {
  static const uint8_t nfcid1[RFAL_NFCID1_TRIPLE_LEN] = {0x5F, 'I', 'N', 'W', 0, 0, 0, 0, 0, 0};
  memset(&s_disc, 0, sizeof(s_disc));
  s_disc.compMode = RFAL_COMPLIANCE_MODE_NFC;
  s_disc.techs2Find = RFAL_NFC_LISTEN_TECH_A;         // phones read Type 4 over NFC-A
  s_disc.totalDuration = 1000;
  s_disc.devLimit = 1;
  s_disc.nfcfBR = RFAL_BR_212;
  s_disc.ap2pBR = RFAL_BR_424;
  s_disc.notifyCb = onState;
  s_disc.lmConfigPA.nfcidLen = RFAL_LM_NFCID_LEN_04;
  memcpy(s_disc.lmConfigPA.nfcid, nfcid1, sizeof(nfcid1));
  s_disc.lmConfigPA.SENS_RES[0] = 0x02;
  s_disc.lmConfigPA.SENS_RES[1] = 0x00;
  s_disc.lmConfigPA.SEL_RES = 0x20;
}

void stop() {
  if (s_ready && s_mode != Mode::Off) s_nfc->rfalNfcDeactivate(false);
  s_mode = Mode::Off;
}

bool start(Mode m) {
  if (!ensureInit()) return false;
  if (s_mode != Mode::Off) s_nfc->rfalNfcDeactivate(false);
  s_mode = m;
  s_handled = s_primed = false;
  if (m == Mode::Emulate) { configListen(); demoCeInit(s_ceNfcid2); }
  else configPoll();
  return s_nfc->rfalNfcDiscover(&s_disc) == ST_ERR_NONE;
}

const char* techName(const rfalNfcDevice* d) {
  switch (d->type) {
    case RFAL_NFC_LISTEN_TYPE_NFCA: return "NFC-A";
    case RFAL_NFC_LISTEN_TYPE_NFCB: return "NFC-B";
    case RFAL_NFC_LISTEN_TYPE_NFCF: return "NFC-F";
    case RFAL_NFC_LISTEN_TYPE_NFCV: return "NFC-V";
    case RFAL_NFC_LISTEN_TYPE_ST25TB: return "ST25TB";
    default: return "other";
  }
}

void handleTag(rfalNfcDevice* dev) {
  TagResult& t = s_tag;
  strlcpy(t.tech, techName(dev), sizeof(t.tech));
  t.uid[0] = 0;
  for (uint8_t i = 0; i < dev->nfcidLen && i < 10; i++) {
    char h[4]; snprintf(h, sizeof(h), i ? ":%02X" : "%02X", dev->nfcid[i]);
    strlcat(t.uid, h, sizeof(t.uid));
  }
  t.hasNdef = false; t.msgLen = 0; t.capacity = 0; t.writable = false;
  ndefInfo info;
  if (s_ndef->ndefPollerContextInitialization(dev) == ST_ERR_NONE && s_ndef->ndefPollerNdefDetect(&info) == ST_ERR_NONE) {
    t.capacity = info.areaLen;
    t.writable = info.state == NDEF_STATE_INITIALIZED || info.state == NDEF_STATE_READWRITE;
    if (s_mode == Mode::Write) {
      if (!t.writable) { s_writeResult = -1; strlcpy(s_writeErr, "tag is read-only", sizeof(s_writeErr)); }
      else if (s_writeLen > info.areaLen) {
        s_writeResult = -1;
        snprintf(s_writeErr, sizeof(s_writeErr), "needs %lu bytes, tag holds %lu", (unsigned long)s_writeLen, (unsigned long)info.areaLen);
      } else if (s_ndef->ndefPollerWriteRawMessage(s_writeBuf, s_writeLen) == ST_ERR_NONE) {
        s_writeResult = 1;
      } else { s_writeResult = -1; strlcpy(s_writeErr, "write failed - hold it still", sizeof(s_writeErr)); }
    }
    uint32_t got = 0;
    if (s_ndef->ndefPollerReadRawMessage(t.msg, sizeof(t.msg), &got) == ST_ERR_NONE) { t.hasNdef = got > 0; t.msgLen = got; }
  } else if (s_mode == Mode::Write) {
    s_writeResult = -1;
    strlcpy(s_writeErr, "not an NDEF tag (NTAG / Type 2-5 work)", sizeof(s_writeErr));
  }
  t.at = millis();
  t.fresh = true;
}

void cePrime() {
  if (s_nfc->rfalNfcDataExchangeStart(nullptr, 0, &s_rx, &s_rxLen, RFAL_FWT_NONE) != ST_ERR_NONE) {
    s_nfc->rfalNfcDeactivate(true); return;
  }
  s_primed = true;
  (void)s_nfc->rfalNfcDataExchangeGetStatus();
}

void ceExchange() {
  const ReturnCode err = s_nfc->rfalNfcDataExchangeGetStatus();
  if (err == ST_ERR_BUSY) return;
  if (err == ST_ERR_SLEEP_REQ) { s_primed = false; return; }
  rfalNfcDevice* dev = nullptr;
  if (err != ST_ERR_NONE || s_nfc->rfalNfcGetActiveDevice(&dev) != ST_ERR_NONE || !dev || !s_rx || !s_rxLen) {
    s_nfc->rfalNfcDeactivate(true); s_primed = false; return;
  }
  uint16_t out = 0;
  if (dev->type == RFAL_NFC_POLL_TYPE_NFCA && dev->rfInterface == RFAL_NFC_INTERFACE_ISODEP)
    out = demoCeT4T(s_rx, *s_rxLen, s_tx, sizeof(s_tx));
  Serial.printf("[NFC] ce apdu type=%d if=%d len=%u ins=%02X -> %u\n", (int)dev->type, (int)dev->rfInterface,
                (unsigned)*s_rxLen, *s_rxLen > 1 ? s_rx[1] : 0, (unsigned)out);
  if (!out) { s_nfc->rfalNfcDeactivate(true); s_primed = false; return; }
  // A READ BINARY of the file body (not just the length) means a phone read us.
  if (*s_rxLen >= 5 && s_rx[1] == 0xB0 && s_rx[3] >= 2) s_ceReads++;
  if (s_nfc->rfalNfcDataExchangeStart(s_tx, out, &s_rx, &s_rxLen, RFAL_FWT_NONE) != ST_ERR_NONE) {
    s_nfc->rfalNfcDeactivate(true); s_primed = false;
  }
}

void work() {
  if (!s_ready || s_mode == Mode::Off) return;
  s_nfc->rfalNfcWorker();
  const rfalNfcState st = s_nfc->rfalNfcGetState();
  if (s_mode == Mode::Emulate) {
    if (st == RFAL_NFC_STATE_ACTIVATED && !s_primed) cePrime();
    else if (st == RFAL_NFC_STATE_DATAEXCHANGE_DONE) ceExchange();
    else if (st == RFAL_NFC_STATE_IDLE) { s_primed = false; s_nfc->rfalNfcDiscover(&s_disc); }
    return;
  }
  if (st == RFAL_NFC_STATE_ACTIVATED && !s_handled) {
    rfalNfcDevice* dev = nullptr;
    if (s_nfc->rfalNfcGetActiveDevice(&dev) == ST_ERR_NONE && dev) handleTag(dev);
    s_handled = true;
    s_nfc->rfalNfcDeactivate(true);           // back to discovery
  } else if (st == RFAL_NFC_STATE_IDLE) {
    s_nfc->rfalNfcDiscover(&s_disc);
  }
}

// ---- NDEF ---------------------------------------------------------------------------------------
static const char* URI_PREFIX[] = {
  "", "http://www.", "https://www.", "http://", "https://", "tel:", "mailto:", "ftp://anonymous:anonymous@",
  "ftp://ftp.", "ftps://", "sftp://", "smb://", "nfs://", "ftp://", "dav://", "news:", "telnet://", "imap:",
  "rtsp://", "urn:", "pop:", "sip:", "sips:", "tftp:", "btspp://", "btl2cap://", "btgoep://", "tcpobex://",
  "irdaobex://", "file://", "urn:epc:id:", "urn:epc:tag:", "urn:epc:pat:", "urn:epc:raw:", "urn:epc:", "urn:nfc:",
};

// One record, as a raw NDEF message (MB|ME set). Returns the message length.
uint32_t buildRecord(uint8_t* out, uint32_t cap, uint8_t tnf, const char* type, const uint8_t* payload, uint32_t plen) {
  const uint8_t tlen = strlen(type);
  const bool sr = plen < 256;
  const uint32_t need = 2 + (sr ? 1 : 4) + tlen + plen;
  if (need > cap) return 0;
  uint32_t i = 0;
  out[i++] = 0xC0 | (sr ? 0x10 : 0) | (tnf & 7);
  out[i++] = tlen;
  if (sr) out[i++] = (uint8_t)plen;
  else { out[i++] = plen >> 24; out[i++] = plen >> 16; out[i++] = plen >> 8; out[i++] = plen; }
  memcpy(out + i, type, tlen); i += tlen;
  memcpy(out + i, payload, plen); i += plen;
  return i;
}

uint32_t buildUri(uint8_t* out, uint32_t cap, const char* uri) {
  uint8_t p[900];
  const uint32_t n = min<size_t>(strlen(uri), sizeof(p) - 1);
  p[0] = 0x00;                                  // no abbreviation: store it whole
  memcpy(p + 1, uri, n);
  return buildRecord(out, cap, 0x01, "U", p, n + 1);
}

uint32_t buildText(uint8_t* out, uint32_t cap, const char* text) {
  uint8_t p[900];
  const uint32_t n = min<size_t>(strlen(text), sizeof(p) - 3);
  p[0] = 0x02; p[1] = 'e'; p[2] = 'n';          // UTF-8, language "en"
  memcpy(p + 3, text, n);
  return buildRecord(out, cap, 0x01, "T", p, n + 3);
}

struct Rec {
  enum Kind : uint8_t { Uri, Text, Wifi, VCard, Mime, Other } kind;
  String title, value;          // value: full URI / text / ssid
  String extra;                 // wifi key
};

void parseWsc(const uint8_t* p, uint32_t n, String& ssid, String& key) {
  uint32_t i = 0;
  while (i + 4 <= n) {
    const uint16_t t = (p[i] << 8) | p[i + 1], l = (p[i + 2] << 8) | p[i + 3];
    i += 4;
    if (i + l > n) break;
    if (t == 0x100E) parseWsc(p + i, l, ssid, key);          // credential: nested
    else if (t == 0x1045) ssid = String((const char*)p + i).substring(0, l);
    else if (t == 0x1027) key = String((const char*)p + i).substring(0, l);
    i += l;
  }
}

std::vector<Rec> parseNdef(const uint8_t* m, uint32_t len) {
  std::vector<Rec> out;
  uint32_t i = 0;
  while (i + 3 <= len && out.size() < 12) {
    const uint8_t h = m[i++];
    const uint8_t tnf = h & 7;
    const uint8_t tlen = m[i++];
    uint32_t plen;
    if (h & 0x10) plen = m[i++];
    else { if (i + 4 > len) break; plen = ((uint32_t)m[i] << 24) | (m[i + 1] << 16) | (m[i + 2] << 8) | m[i + 3]; i += 4; }
    uint8_t idlen = 0;
    if (h & 0x08) idlen = m[i++];
    if (i + tlen + idlen + plen > len) break;
    String type = String((const char*)m + i).substring(0, tlen);
    const uint8_t* p = m + i + tlen + idlen;
    Rec r;
    if (tnf == 1 && type == "U" && plen >= 1) {
      r.kind = Rec::Uri;
      const uint8_t code = p[0];
      r.value = String(code < sizeof(URI_PREFIX) / sizeof(URI_PREFIX[0]) ? URI_PREFIX[code] : "");
      for (uint32_t k = 1; k < plen; k++) r.value += (char)p[k];
      r.title = r.value.startsWith("meshcore://") ? "MeshCore link" : "Link";
    } else if (tnf == 1 && type == "T" && plen >= 1) {
      r.kind = Rec::Text;
      const uint8_t lang = p[0] & 0x3F;
      for (uint32_t k = 1 + lang; k < plen; k++) r.value += (char)p[k];
      r.title = "Text";
    } else if (tnf == 2 && type == "application/vnd.wfa.wsc") {
      r.kind = Rec::Wifi;
      parseWsc(p, plen, r.value, r.extra);
      r.title = "Wi-Fi network";
    } else if (tnf == 2 && (type == "text/vcard" || type == "text/x-vcard")) {
      r.kind = Rec::VCard;
      for (uint32_t k = 0; k < plen && k < 300; k++) r.value += (char)p[k];
      const int fn = r.value.indexOf("FN:");
      r.title = "Contact card";
      if (fn >= 0) { int e = r.value.indexOf('\n', fn); r.value = r.value.substring(fn + 3, e < 0 ? r.value.length() : e); r.value.trim(); }
    } else if (tnf == 2) {
      r.kind = Rec::Mime;
      r.title = type;
      for (uint32_t k = 0; k < plen && k < 80; k++) r.value += (p[k] >= 32 && p[k] < 127) ? (char)p[k] : '.';
    } else {
      r.kind = Rec::Other;
      r.title = String("record tnf ") + tnf + " " + type;
      r.value = String(plen) + " bytes";
    }
    out.push_back(r);
    i += tlen + idlen + plen;
    if (h & 0x40) break;                        // ME
  }
  return out;
}

String urlDecode(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '+') o += ' ';
    else if (c == '%' && i + 2 < s.length()) { o += (char)strtol(s.substring(i + 1, i + 3).c_str(), nullptr, 16); i += 2; }
    else o += c;
  }
  return o;
}

String urlEncode(const char* s) {
  String o;
  for (; *s; s++) {
    const uint8_t c = *s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { char h[4]; snprintf(h, sizeof(h), "%%%02X", c); o += h; }
  }
  return o;
}

String queryParam(const String& uri, const char* key) {
  const int q = uri.indexOf('?');
  if (q < 0) return "";
  String k = String(key) + "=";
  int i = q + 1;
  while (i < (int)uri.length()) {
    int e = uri.indexOf('&', i);
    if (e < 0) e = uri.length();
    String part = uri.substring(i, e);
    if (part.startsWith(k)) return urlDecode(part.substring(k.length()));
    i = e + 1;
  }
  return "";
}

// Act on a MeshCore link. Returns a toast.
String applyMeshcoreLink(const String& uri) {
  if (!g_node) return "radio not running";
  String rest = uri.substring(11);                         // after meshcore://
  if (rest.startsWith("contact/add")) {
    const String name = queryParam(uri, "name"), key = queryParam(uri, "public_key");
    const int type = queryParam(uri, "type").toInt();
    uint8_t pub[32];
    if (key.length() != 64 || !mesh::Utils::fromHex(pub, 32, key.c_str())) return "bad contact link";
    if (g_node->contact(pub)) return name + " is already a contact";
    ContactInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.id = mesh::Identity(pub);
    strlcpy(ci.name, name.length() ? name.c_str() : "nfc contact", sizeof(ci.name));
    ci.type = type >= 1 && type <= 4 ? type : 1;
    ci.out_path_len = OUT_PATH_UNKNOWN;
    ci.lastmod = app::now();
    if (!g_node->addContact(ci)) return "contact list is full";
    g_node->saveContactsNow();
    return "added " + name;
  }
  if (rest.startsWith("channel/add")) {
    const String name = queryParam(uri, "name"), sec = queryParam(uri, "secret");
    uint8_t s[16];
    if (sec.length() != 32 || !mesh::Utils::fromHex(s, 16, sec.c_str())) return "bad channel link";
    if (g_node->findChannelBySecret(s) >= 0) return "already in " + name;
    return g_node->addChannelNamed(name.c_str(), s) ? "joined " + name : String("no free channel slot");
  }
  // meshcore://<hex>: an exported, signed advert.
  uint8_t buf[200];
  const int n = rest.length() / 2;
  if (n > 0 && n <= (int)sizeof(buf) && mesh::Utils::fromHex(buf, n, rest.c_str()) && g_node->importContact(buf, n)) {
    g_node->saveContactsNow();
    return "contact imported";
  }
  return "unknown meshcore link";
}

String myContactLink() {
  if (!g_node) return "";
  char hex[65];
  mesh::Utils::toHex(hex, g_node->self_id.pub_key, 32);
  return "meshcore://contact/add?name=" + urlEncode(g_node->name()) + "&public_key=" + hex + "&type=1";
}

String channelLink(int idx) {
  ChannelDetails ch;
  if (!g_node || !g_node->getChannel(idx, ch) || !ch.name[0]) return "";
  char hex[33];
  mesh::Utils::toHex(hex, ch.channel.secret, 16);
  return "meshcore://channel/add?name=" + urlEncode(ch.name) + "&secret=" + hex;
}

}  // namespace

// ---- screens ------------------------------------------------------------------------------------
class NfcReadView : public View {
public:
  NfcReadView() { _ok = start(Mode::Read); s_tag.fresh = false; s_tag.at = 0; }
  ~NfcReadView() override { stop(); }
  void tick() override {
    work();
    if (s_tag.fresh) {
      s_tag.fresh = false;
      // Same tag still on the reader: don't re-buzz every pass.
      if (strcmp(_lastUid, s_tag.uid) || millis() - _lastAt > 3000) { haptic.tick(); _sel = 0; }
      strlcpy(_lastUid, s_tag.uid, sizeof(_lastUid));
      _lastAt = millis();
      _recs = s_tag.hasNdef ? parseNdef(s_tag.msg, s_tag.msgLen) : std::vector<Rec>();
      dirty = true;
    }
    if (millis() - _anim > 250) { _anim = millis(); _phase++; if (!s_tag.at) dirty = true; }
  }
  void rotate(int d) override { if (!_recs.empty()) { _sel = ((_sel + d) % (int)_recs.size() + _recs.size()) % _recs.size(); dirty = true; } }
  void press() override {
    if (_recs.empty()) return;
    const Rec& r = _recs[_sel];
    if (r.kind == Rec::Uri && r.value.startsWith("meshcore://")) { nav.toast(applyMeshcoreLink(r.value).c_str(), 3500); return; }
    if (r.kind == Rec::Wifi && r.value.length()) {
      const String ss = r.value, pw = r.extra;
      confirm("Join " + ss + "?", "saves it and connects", [ss, pw] { wifi::save(ss.c_str(), pw.c_str()); nav.toast("joining"); });
      return;
    }
    nav.push(new TextPageView(r.title, [r](std::vector<String>& out) {
      String v = r.value;
      while (v.length()) { out.push_back(v.substring(0, 56)); v = v.substring(min<size_t>(56, v.length())); }
    }, 60000));
  }
  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    drawHeader(g, "NFC  read", _ok ? "hold a tag to the pager" : "");
    if (!_ok) {
      g.setTextColor(t.red, t.bg);
      g.drawString("the NFC chip did not answer", 14, L::BODY_Y + 12);
      return;
    }
    if (!s_tag.at) {
      const int cx = L::W / 2, cy = L::BODY_Y + 80;
      for (int k = 0; k < 3; k++) {
        const int r = 18 + ((_phase + k * 3) % 9) * 7;
        g.drawCircle(cx, cy, r, k == 0 ? t.green : t.greenDim);
      }
      g.setTextColor(t.dim, t.bg);
      const char* s = "listening for tags, cards and stickers";
      g.drawString(s, (L::W - g.textWidth(s)) / 2, L::H - 24);
      return;
    }
    char line[96];
    snprintf(line, sizeof(line), "%s  %s", s_tag.tech, s_tag.uid);
    g.setTextColor(t.green, t.bg);
    g.drawString(line, 12, L::BODY_Y + 4);
    if (s_tag.capacity) {
      snprintf(line, sizeof(line), "%lu / %lu bytes%s", (unsigned long)s_tag.msgLen, (unsigned long)s_tag.capacity, s_tag.writable ? "" : "  read-only");
      g.setTextColor(t.dim, t.bg);
      g.drawString(line, L::W - 12 - g.textWidth(line), L::BODY_Y + 4);
    }
    if (_recs.empty()) {
      g.setTextColor(t.dim, t.bg);
      g.drawString(s_tag.capacity ? "empty tag - write something to it from the NFC menu" : "no NDEF data (id only)", 12, L::BODY_Y + 30);
      return;
    }
    for (int i = 0; i < (int)_recs.size() && i < 4; i++) {
      const Rec& r = _recs[i];
      const int y = L::BODY_Y + 26 + i * 38;
      const bool on = i == _sel;
      const uint16_t bg = on ? t.focus : t.panel;
      g.fillRoundRect(8, y, L::W - 16, 34, 6, bg);
      if (on) g.drawRoundRect(8, y, L::W - 16, 34, 6, t.green);
      g.setTextColor(on ? t.green : t.txt, bg);
      drawUtf8(g, r.title.c_str(), 16, y + 1, 200);
      String hint = r.kind == Rec::Uri && r.value.startsWith("meshcore://") ? (r.value.indexOf("channel/") > 0 ? "press: join" : "press: add")
                  : r.kind == Rec::Wifi ? "press: join" : "press: open";
      g.setTextColor(t.dim, bg);
      g.drawString(hint, L::W - 20 - g.textWidth(hint), y + 1);
      String v = r.value;
      if (r.kind == Rec::Uri && v.startsWith("meshcore://contact/add")) v = "contact: " + queryParam(v, "name");
      else if (r.kind == Rec::Uri && v.startsWith("meshcore://channel/add")) v = "channel: " + queryParam(v, "name");
      g.setTextColor(t.white, bg);
      drawUtf8(g, v.c_str(), 16, y + 17, L::W - 40);
    }
  }
private:
  bool _ok = false;
  std::vector<Rec> _recs;
  int _sel = 0, _phase = 0;
  uint32_t _anim = 0, _lastAt = 0;
  char _lastUid[32] = "";
};

class NfcWriteView : public View {
public:
  NfcWriteView(const String& what, const uint8_t* msg, uint32_t len) : _what(what) {
    memcpy(s_writeBuf, msg, len);
    s_writeLen = len;
    s_writeResult = 0;
    s_writeErr[0] = 0;
    _ok = start(Mode::Write);
  }
  ~NfcWriteView() override { stop(); }
  void tick() override {
    if (s_writeResult == 0) work();
    if (s_writeResult != _shown) {
      _shown = s_writeResult;
      if (_shown == 1) { haptic.buzz(1); stop(); }
      dirty = true;
    }
    if (millis() - _anim > 250) { _anim = millis(); _phase++; if (!_shown) dirty = true; }
  }
  void press() override { if (_shown == -1) { s_writeResult = 0; _shown = 0; start(Mode::Write); dirty = true; } }
  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    drawHeader(g, "NFC  write", String(s_writeLen).c_str());
    g.setTextColor(t.txt, t.bg);
    drawUtf8(g, _what.c_str(), 14, L::BODY_Y + 8, L::W - 28);
    if (!_ok) { g.setTextColor(t.red, t.bg); g.drawString("the NFC chip did not answer", 14, L::BODY_Y + 40); return; }
    const int cx = L::W / 2, cy = L::BODY_Y + 96;
    if (_shown == 1) {
      g.fillCircle(cx, cy, 30, t.greenDim);
      g.setTextColor(t.white, t.greenDim);
      g.drawString("done", cx - g.textWidth("done") / 2, cy - 8);
      g.setTextColor(t.green, t.bg);
      const char* s = "written. backspace to go back";
      g.drawString(s, (L::W - g.textWidth(s)) / 2, L::H - 24);
    } else if (_shown == -1) {
      g.setTextColor(t.red, t.bg);
      g.drawString(s_writeErr, (L::W - g.textWidth(s_writeErr)) / 2, cy - 8);
      g.setTextColor(t.dim, t.bg);
      g.drawString("press to try again", (L::W - g.textWidth("press to try again")) / 2, L::H - 24);
    } else {
      for (int k = 0; k < 3; k++) g.drawCircle(cx, cy, 16 + ((_phase + k * 3) % 9) * 6, k ? t.greenDim : t.amber);
      g.setTextColor(t.amber, t.bg);
      const char* s = "hold a tag to the pager to write it";
      g.drawString(s, (L::W - g.textWidth(s)) / 2, L::H - 24);
    }
  }
private:
  String _what;
  bool _ok = false;
  int _shown = 0, _phase = 0;
  uint32_t _anim = 0;
};

class NfcEmulateView : public View {
public:
  NfcEmulateView(const String& what, const uint8_t* msg, uint32_t len) : _what(what) {
    s_ceFile[0] = len >> 8; s_ceFile[1] = len & 0xFF;
    memcpy(s_ceFile + 2, msg, len);
    demoCeSetNdefFile(s_ceFile, len + 2);
    s_ceReads = 0;
    _ok = start(Mode::Emulate);
  }
  ~NfcEmulateView() override { stop(); }
  void tick() override {
    // A reader expects an answer within milliseconds, far quicker than one pass
    // of the main loop (screen push, radio). So spin on the chip in ~120 ms bursts,
    // and don't let go at all while a reader is mid-conversation.
    const uint32_t start = millis();
    for (;;) {
      work();
      const rfalNfcState st = s_ready ? s_nfc->rfalNfcGetState() : RFAL_NFC_STATE_IDLE;
      const bool busy = st >= RFAL_NFC_STATE_LISTEN_ACTIVATION && st != RFAL_NFC_STATE_LISTEN_SLEEP;
      const uint32_t el = millis() - start;
      if (el > 2000 || (!busy && el > 120)) break;
    }
    if (s_ceReads != _reads) { _reads = s_ceReads; haptic.tick(); dirty = true; }
    // Redraw rarely: drawing costs the time the reader is waiting on.
    if (millis() - _anim > 2000) { _anim = millis(); _phase++; dirty = true; }
  }
  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    drawHeader(g, "NFC  be a tag", _reads ? (String(_reads) + " reads").c_str() : "");
    if (!_ok) { g.setTextColor(t.red, t.bg); g.drawString("the NFC chip did not answer", 14, L::BODY_Y + 12); return; }
    g.setTextColor(t.txt, t.bg);
    drawUtf8(g, _what.c_str(), 14, L::BODY_Y + 8, L::W - 28);
    const int cx = L::W / 2, cy = L::BODY_Y + 96;
    for (int k = 0; k < 3; k++) g.drawCircle(cx, cy, 16 + ((_phase + k * 3) % 9) * 6, k ? t.greenDim : t.green);
    g.setTextColor(t.dim, t.bg);
    const char* s = "tap a phone to the pager - it opens in the MeshCore app";
    g.drawString(s, (L::W - g.textWidth(s)) / 2, L::H - 24);
  }
private:
  String _what;
  bool _ok = false;
  uint16_t _reads = 0;
  int _phase = 0;
  uint32_t _anim = 0;
};

static void writeUri(const String& what, const String& uri) {
  uint8_t msg[1024];
  const uint32_t n = buildUri(msg, sizeof(msg), uri.c_str());
  if (!n) { nav.toast("too long for a tag"); return; }
  nav.push(new NfcWriteView(what, msg, n));
}

static void emulateUri(const String& what, const String& uri) {
  uint8_t msg[1024];
  const uint32_t n = buildUri(msg, sizeof(msg), uri.c_str());
  if (!n) { nav.toast("too long"); return; }
  nav.push(new NfcEmulateView(what, msg, n));
}

static void pickChannel(bool emulate) {
  auto* m = new MenuView(emulate ? "Share channel" : "Write channel");
  for (int i = 0; i < MAX_GROUP_CHANNELS && g_node; i++) {
    ChannelDetails ch;
    if (!g_node->getChannel(i, ch) || !ch.name[0]) continue;
    const String nm = ch.name;
    m->action(nm, [i, nm, emulate] {
      nav.pop();
      const String link = channelLink(i);
      if (emulate) emulateUri("sharing channel " + nm, link);
      else writeUri("channel invite: " + nm, link);
    });
  }
  nav.push(m);
}

void openNfc() {
  if (!ensureInit()) { nav.toast("the NFC chip did not answer"); }
  auto* m = new MenuView("NFC");
  m->header("read");
  m->action("read a tag or card", [] { nav.push(new NfcReadView()); });
  m->header("write a tag");
  m->action("my contact (tap to add me)", [] { writeUri(String("contact: ") + (g_node ? g_node->name() : ""), myContactLink()); });
  m->action("a channel invite", [] { pickChannel(false); });
  m->action("text", [] {
    prompt("Write text", "what the tag should say", "", 120, [](const String& s) {
      if (!s.length()) return;
      uint8_t msg[1024];
      const uint32_t n = buildText(msg, sizeof(msg), s.c_str());
      nav.push(new NfcWriteView("text: " + s, msg, n));
    });
  });
  m->action("a link", [] {
    prompt("Write link", "e.g. https://inwmesh.org", "https://", 120, [](const String& s) {
      if (s.length() > 8) writeUri("link: " + s, s);
    });
  });
  m->header("be a tag (phones tap the pager)");
  m->action("share my contact", [] { emulateUri(String("sharing ") + (g_node ? g_node->name() : ""), myContactLink()); });
  m->action("share a channel", [] { pickChannel(true); });
  nav.push(m);
}
