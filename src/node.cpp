#include "node.h"
#include "history.h"
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <new>
#include <helpers/esp32/SerialBLEInterface.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/AdvertDataHelpers.h>

// Same values MyMesh.cpp uses internally.
#ifndef REQ_TYPE_GET_STATUS
#define REQ_TYPE_GET_STATUS 0x01
#endif
#ifndef REQ_TYPE_GET_TELEMETRY_DATA
#define REQ_TYPE_GET_TELEMETRY_DATA 0x03
#endif
#define CTL_DISCOVER_REQ  0x80
#define CTL_DISCOVER_RESP 0x90

InwNode* g_node = nullptr;
DataStore g_store(SPIFFS, rtc_clock);
MultiSerialInterface g_ifaces;
static SerialBLEInterface g_ble;
static SimpleMeshTables g_tables;
static StdRNG g_rng;

// Settings the node needs from the app, owned by settings.cpp.
extern bool inwAutoRetry();
extern bool inwAutoResetPath();
extern bool inwBleWanted();

// ---------------------------------------------------------------------------
InwNode::InwNode(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc,
                 SimpleMeshTables& tables, DataStore& store)
    : MyMesh(radio, rng, rtc, tables, store, nullptr), _ds(store) {
  memset(_echo, 0, sizeof(_echo));
  memset(discovered, 0, sizeof(discovered));
  memset(pktLog, 0, sizeof(pktLog));
  memset(cliLog, 0, sizeof(cliLog));
}

static uint8_t hopsOf(mesh::Packet* pkt) {
  return pkt->isRouteFlood() ? pkt->getPathHashCount() : 0xFF;
}

// The repeaters that carried a flood packet: first byte of each path hash,
// oldest hop first. Direct packets have no path.
static uint8_t pathOf(mesh::Packet* pkt, uint8_t* out, uint8_t cap) {
  if (!pkt->isRouteFlood()) return 0;
  const uint8_t sz = pkt->getPathHashSize();
  uint8_t n = min<uint8_t>(pkt->getPathHashCount(), cap);
  for (uint8_t i = 0; i < n; i++) out[i] = pkt->path[i * sz];
  return n;
}

static bool mentions(const char* text, const char* me) {
  if (!me || !*me) return false;
  char tag[40];
  snprintf(tag, sizeof(tag), "@[%s]", me);
  if (strstr(text, tag)) return true;
  snprintf(tag, sizeof(tag), "@%s", me);
  return strcasestr(text, tag) != nullptr;
}

// ---- DMs ---------------------------------------------------------------------
PendingDM* InwNode::freePending() {
  for (auto& p : _pending) if (!p.used) return &p;
  // Table full: the oldest pending gives up rather than the new message.
  PendingDM* oldest = &_pending[0];
  for (auto& p : _pending) if (p.sentAt < oldest->sentAt) oldest = &p;
  history.setStatus(oldest->histId, ST_FAILED, oldest->attempt + 1);
  oldest->used = false;
  return oldest;
}

bool InwNode::transmitDM(PendingDM& p, const char* text) {
  ContactInfo* c = contact(p.pub);
  if (!c) return false;
  uint32_t ack = 0, est = 0;
  const int r = sendMessage(*c, p.ts, p.attempt, text, ack, est);
  if (r == MSG_SEND_FAILED) return false;
  p.acks[p.attempt & 3] = ack;
  p.sentAt = millis();
  p.deadline = p.sentAt + est + 2000;
  if (_awaitCount < 4) _awaitTx[_awaitCount++] = p.histId;
  return true;
}

bool InwNode::sendDM(const uint8_t* pub, const char* text, uint32_t histId) {
  PendingDM* p = freePending();
  memset(p, 0, sizeof(*p));
  p->used = true;
  memcpy(p->pub, pub, 32);
  p->histId = histId;
  p->ts = getRTCClock()->getCurrentTimeUnique();
  p->attempt = 0;
  if (!transmitDM(*p, text)) { p->used = false; history.setStatus(histId, ST_FAILED, 1); return false; }
  history.setStatus(histId, ST_SENDING, 1);
  return true;
}

bool InwNode::resendDM(const uint8_t* pub, const char* text, uint32_t histId) {
  return sendDM(pub, text, histId);
}

void InwNode::tick() {
  const uint32_t now = millis();
  for (auto& p : _pending) {
    if (!p.used || (int32_t)(now - p.deadline) < 0) continue;
    HistMsg* m = history.find(p.histId);
    const uint8_t maxAttempts = inwAutoRetry() ? 3 : 1;
    if (m && p.attempt + 1 < maxAttempts) {
      p.attempt++;
      // Last try: the stored route may be stale, so drop it and flood.
      if (p.attempt == maxAttempts - 1 && inwAutoResetPath()) {
        ContactInfo* c = contact(p.pub);
        if (c && c->out_path_len != OUT_PATH_UNKNOWN) resetPathTo(*c);
      }
      if (transmitDM(p, m->text)) { history.setStatus(p.histId, ST_SENDING, p.attempt + 1); continue; }
    }
    history.setStatus(p.histId, ST_FAILED, p.attempt + 1);
    p.used = false;
    emit(NodeEvent::Failed, &p.histId);
  }
  if (_loginState == 1 && (int32_t)(now - _loginDeadline) >= 0) {
    _loginState = 3;
    emit(NodeEvent::LoginFail);
  }
}

ContactInfo* InwNode::processAck(const uint8_t* data) {
  uint32_t ack;
  memcpy(&ack, data, 4);
  for (auto& p : _pending) {
    if (!p.used) continue;
    for (uint8_t a = 0; a < 4; a++) {
      if (!p.acks[a] || p.acks[a] != ack) continue;
      const uint32_t rtt = millis() - p.sentAt;
      history.setStatus(p.histId, ST_DELIVERED, p.attempt + 1, (uint16_t)min<uint32_t>(rtt / 10, 65535));
      p.used = false;
      emit(NodeEvent::Delivered, &p.histId);
      ContactInfo* c = contact(p.pub);
      return c ? c : MyMesh::processAck(data);
    }
  }
  return MyMesh::processAck(data);
}

// ---- channels ----------------------------------------------------------------
bool InwNode::sendChannel(uint8_t idx, const char* text, uint32_t histId) {
  ChannelDetails ch;
  if (!getChannel(idx, ch) || !ch.name[0]) { history.setStatus(histId, ST_FAILED); return false; }
  const uint32_t ts = getRTCClock()->getCurrentTimeUnique();
  const bool ok = sendGroupMessage(ts, ch.channel, getNodeName(), text, strlen(text));
  history.setStatus(histId, ok ? ST_SENT : ST_FAILED);
  if (ok && _awaitCount < 4) _awaitTx[_awaitCount++] = histId;
  return ok;
}

int InwNode::findChannelBySecret(const uint8_t* secret6) {
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (getChannel(i, ch) && ch.name[0] && !memcmp(ch.channel.secret, secret6, 6)) return i;
  }
  return -1;
}

bool InwNode::addChannelNamed(const char* name, const uint8_t* secret16) {
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (!getChannel(i, ch)) return false;
    if (ch.name[0]) {
      if (!memcmp(ch.channel.secret, secret16, 16)) return false;   // already have it
      continue;
    }
    memset(&ch, 0, sizeof(ch));
    strlcpy(ch.name, name, sizeof(ch.name));
    memcpy(ch.channel.secret, secret16, 16);
    setChannel(i, ch);
    saveChannelsNow();
    return true;
  }
  return false;
}

bool InwNode::removeChannel(uint8_t idx) {
  ChannelDetails ch;
  memset(&ch, 0, sizeof(ch));
  if (!setChannel(idx, ch)) return false;
  saveChannelsNow();
  return true;
}

// ---- receive hooks -------------------------------------------------------------
void InwNode::onMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t ts, const char* text) {
  MyMesh::onMessageRecv(from, pkt, ts, text);
  const bool room = from.type == ADV_TYPE_ROOM;
  uint8_t flags = mentions(text, getNodeName()) ? HF_MENTION : 0;
  if (room) flags |= HF_ROOM;
  uint8_t hp[8];
  const uint8_t hn = pathOf(pkt, hp, sizeof(hp));
  history.add(ConvKey::contact(from.id.pub_key), flags, ST_RECV, from.name, text,
              getRTCClock()->getCurrentTime(), hopsOf(pkt), (int8_t)(pkt->getSNR() * 4), hp, hn);
  emit(room ? NodeEvent::RoomMsg : NodeEvent::DirectMsg, &from);
}

void InwNode::onSignedMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t ts,
                                  const uint8_t* sender_prefix, const char* text) {
  MyMesh::onSignedMessageRecv(from, pkt, ts, sender_prefix, text);
  // Room server posts: `from` is the room, the author is identified by prefix.
  char who[24];
  ContactInfo* author = lookupContactByPubKey(sender_prefix, 4);
  if (author) strlcpy(who, author->name, sizeof(who));
  else if (!memcmp(sender_prefix, self_id.pub_key, 4)) strlcpy(who, getNodeName(), sizeof(who));
  else snprintf(who, sizeof(who), "%02x%02x%02x%02x", sender_prefix[0], sender_prefix[1],
                sender_prefix[2], sender_prefix[3]);
  uint8_t flags = HF_ROOM | (mentions(text, getNodeName()) ? HF_MENTION : 0);
  if (!memcmp(sender_prefix, self_id.pub_key, 4)) flags |= HF_OUT;
  uint8_t hp[8];
  const uint8_t hn = pathOf(pkt, hp, sizeof(hp));
  history.add(ConvKey::contact(from.id.pub_key), flags, (flags & HF_OUT) ? ST_DELIVERED : ST_RECV,
              who, text, ts ? ts : getRTCClock()->getCurrentTime(), hopsOf(pkt), (int8_t)(pkt->getSNR() * 4), hp, hn);
  emit(NodeEvent::RoomMsg, &from);
}

void InwNode::onCommandDataRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t ts, const char* text) {
  MyMesh::onCommandDataRecv(from, pkt, ts, text);
  cliAppend("", text);
  emit(NodeEvent::CliReply, &from);
}

void InwNode::onChannelMessageRecv(const mesh::GroupChannel& ch, mesh::Packet* pkt, uint32_t ts,
                                   const char* text) {
  MyMesh::onChannelMessageRecv(ch, pkt, ts, text);
  // Channel text travels as "<sender>: <message>".
  char who[24] = "";
  const char* body = text;
  const char* sep = strstr(text, ": ");
  if (sep && sep - text < (int)sizeof(who)) {
    memcpy(who, text, sep - text);
    who[sep - text] = 0;
    body = sep + 2;
  }
  const uint8_t flags = mentions(body, getNodeName()) ? HF_MENTION : 0;
  uint8_t hp[8];
  const uint8_t hn = pathOf(pkt, hp, sizeof(hp));
  history.add(ConvKey::channel(ch.secret), flags, ST_RECV, who, body,
              getRTCClock()->getCurrentTime(), hopsOf(pkt), (int8_t)(pkt->getSNR() * 4), hp, hn);
  int idx = findChannelIdx(ch);
  emit(NodeEvent::ChannelMsg, &idx);
}

void InwNode::onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  MyMesh::onDiscoveredContact(contact, is_new, path_len, path);
  _contactsGen++;
  if (is_new) emit(NodeEvent::NewContact, &contact);
  else emit(NodeEvent::ContactsChanged);
}

void InwNode::onContactPathUpdated(const ContactInfo& contact) {
  MyMesh::onContactPathUpdated(contact);
  _contactsGen++;
}

// ---- requests: login / status / telemetry ------------------------------------------
bool InwNode::login(const uint8_t* pub, const char* password) {
  ContactInfo* c = contact(pub);
  if (!c) return false;
  uint32_t est = 0;
  if (sendLogin(*c, password, est) == MSG_SEND_FAILED) return false;
  memcpy(_loginPub, pub, 4);
  _loginState = 1;
  _loginAdmin = false;
  _loginDeadline = millis() + est + 4000;
  return true;
}

uint8_t InwNode::loginState(const uint8_t* pub) const {
  if (!pub || memcmp(pub, _loginPub, 4)) return 0;
  return _loginState;
}

bool InwNode::requestStatus(const uint8_t* pub) {
  ContactInfo* c = contact(pub);
  if (!c) return false;
  uint32_t tag = 0, est = 0;
  if (sendRequest(*c, REQ_TYPE_GET_STATUS, tag, est) == MSG_SEND_FAILED) return false;
  _statusTag = tag; memcpy(_statusPub, pub, 4); _statusSent = millis();
  _status.valid = false;
  return true;
}

bool InwNode::requestTelemetry(const uint8_t* pub) {
  ContactInfo* c = contact(pub);
  if (!c) return false;
  uint32_t tag = 0, est = 0;
  if (sendRequest(*c, REQ_TYPE_GET_TELEMETRY_DATA, tag, est) == MSG_SEND_FAILED) return false;
  _telemTag = tag; memcpy(_telemPub, pub, 4);
  telemetryText[0] = 0;
  return true;
}

bool InwNode::sendCli(const uint8_t* pub, const char* cmd) {
  ContactInfo* c = contact(pub);
  if (!c) return false;
  uint32_t est = 0;
  const uint32_t ts = getRTCClock()->getCurrentTimeUnique();
  if (sendCommandData(*c, ts, 0, cmd, est) == MSG_SEND_FAILED) return false;
  cliAppend("> ", cmd);
  return true;
}

void InwNode::cliAppend(const char* prefix, const char* text) {
  // One entry per line of the reply, wrapped to the panel.
  const char* p = text;
  bool first = true;
  while (*p || first) {
    const char* nl = strchr(p, '\n');
    size_t n = nl ? (size_t)(nl - p) : strlen(p);
    do {
      const size_t take = min<size_t>(n, 64);
      if (cliCount == CLI_LINES) { memmove(cliLog[0], cliLog[1], sizeof(cliLog[0]) * (CLI_LINES - 1)); cliCount--; }
      snprintf(cliLog[cliCount++], sizeof(cliLog[0]), "%s%.*s", first ? prefix : "  ", (int)take, p);
      first = false;
      p += take; n -= take;
    } while (n);
    if (!nl) break;
    p = nl + 1;
  }
  cliGen++;
}

// CayenneLPP: enough of it to read what repeaters and sensors actually send.
static void decodeLpp(const uint8_t* d, int len, char* out, size_t cap) {
  size_t w = 0;
  out[0] = 0;
  int i = 0;
  auto add = [&](const char* fmt, double v) {
    if (w < cap) w += snprintf(out + w, cap - w, fmt, v);
  };
  while (i + 2 <= len) {
    const uint8_t type = d[i + 1];
    i += 2;
    switch (type) {
      case 116: if (i + 2 > len) return; add("battery %.2f V\n", ((d[i] << 8) | d[i + 1]) / 100.0); i += 2; break;
      case 103: if (i + 2 > len) return; add("temp %.1f C\n", (int16_t)((d[i] << 8) | d[i + 1]) / 10.0); i += 2; break;
      case 104: if (i + 1 > len) return; add("humidity %.0f %%\n", d[i] / 2.0); i += 1; break;
      case 115: if (i + 2 > len) return; add("pressure %.1f hPa\n", ((d[i] << 8) | d[i + 1]) / 10.0); i += 2; break;
      case 117: if (i + 2 > len) return; add("current %.3f A\n", ((d[i] << 8) | d[i + 1]) / 1000.0); i += 2; break;
      case 128: if (i + 2 > len) return; add("power %.0f W\n", (double)((d[i] << 8) | d[i + 1])); i += 2; break;
      case 2:   if (i + 2 > len) return; add("analog %.2f\n", (int16_t)((d[i] << 8) | d[i + 1]) / 100.0); i += 2; break;
      case 136: {
        if (i + 9 > len) return;
        auto s24 = [&](int o) { int32_t v = (d[o] << 16) | (d[o + 1] << 8) | d[o + 2]; if (v & 0x800000) v |= 0xFF000000; return v; };
        if (w < cap) w += snprintf(out + w, cap - w, "gps %.4f %.4f\n", s24(i) / 10000.0, s24(i + 3) / 10000.0);
        i += 9; break;
      }
      default: return;   // unknown type: length unknown, stop rather than misread
    }
  }
}

void InwNode::onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) {
  uint32_t tag = 0;
  if (len >= 4) memcpy(&tag, data, 4);

  if (_loginState == 1 && len >= 5 && !memcmp(_loginPub, contact.id.pub_key, 4)) {
    if (!memcmp(&data[4], "OK", 2)) {
      _loginState = 2;
    } else if (data[4] == RESP_SERVER_LOGIN_OK) {
      _loginState = 2;
      const uint16_t keepAlive = len > 5 ? (uint16_t)data[5] * 16 : 0;
      if (keepAlive) startConnection(contact, keepAlive);
      _loginAdmin = (len > 6 && data[6]) || (len > 7 && (data[7] & 3) == 3);
    } else {
      _loginState = 3;
    }
    emit(_loginState == 2 ? NodeEvent::LoginOk : NodeEvent::LoginFail, &contact);
    return;
  }
  if (_statusTag && len > 4 && (tag == _statusTag || !memcmp(_statusPub, contact.id.pub_key, 4))) {
    struct { uint16_t batt, txq; int16_t nf, rssi; uint32_t recv, sent, air, up, sf, sd, rf, rd;
             uint16_t err; int16_t snr; } s;
    memset(&s, 0, sizeof(s));
    memcpy(&s, &data[4], min<size_t>(len - 4, sizeof(s)));
    _status.valid = true;
    memcpy(_status.pub, contact.id.pub_key, 6);
    _status.battMv = s.batt; _status.txQueue = s.txq; _status.noiseFloor = s.nf;
    _status.lastRssi = s.rssi; _status.lastSnr4 = s.snr;
    _status.recv = s.recv; _status.sent = s.sent; _status.airSecs = s.air; _status.upSecs = s.up;
    _status.sentFlood = s.sf; _status.sentDirect = s.sd; _status.recvFlood = s.rf; _status.recvDirect = s.rd;
    _status.rtt = millis() - _statusSent;
    _statusTag = 0;
    emit(NodeEvent::Status, &contact);
    return;
  }
  if (_telemTag && len > 4 && (tag == _telemTag || !memcmp(_telemPub, contact.id.pub_key, 4))) {
    decodeLpp(&data[4], len - 4, telemetryText, sizeof(telemetryText));
    if (!telemetryText[0]) strlcpy(telemetryText, "no readable sensors", sizeof(telemetryText));
    _telemTag = 0;
    emit(NodeEvent::Telemetry, &contact);
    return;
  }
  MyMesh::onContactResponse(contact, data, len);
}

// ---- trace / discover -----------------------------------------------------------------
bool InwNode::trace(const uint8_t* pub) {
  ContactInfo* c = contact(pub);
  if (!c) return false;
  uint8_t hashSz = 1, n = 0;
  const uint8_t* src = nullptr;
  if (c->out_path_len != OUT_PATH_UNKNOWN) {
    hashSz = (c->out_path_len >> 6) + 1;
    n = c->out_path_len & 63;
    src = c->out_path;
  }
  // The trace hash width must be 1, 2 or 4 bytes; a prefix of a longer hash
  // still matches, so anything else is traced on its first byte.
  const uint8_t sz = (hashSz == 2 || hashSz == 4) ? hashSz : 1;
  const uint8_t pathSz = sz == 1 ? 0 : sz == 2 ? 1 : 2;
  uint8_t list[48];
  uint8_t cnt = 0;
  auto push = [&](const uint8_t* h) { if ((cnt + 1) * sz <= sizeof(list)) { memcpy(&list[cnt * sz], h, sz); cnt++; } };
  for (uint8_t i = 0; i < n; i++) push(&src[i * hashSz]);
  const bool infra = c->type == ADV_TYPE_REPEATER || c->type == ADV_TYPE_ROOM;
  if (infra) push(c->id.pub_key);        // it forwards, so it can be the far end
  for (int i = (int)n - 1 - (infra ? 0 : 1); i >= 0; i--) push(&src[i * hashSz]);
  if (!cnt) return false;                // a chat node with no repeaters between us

  _traceTag = getRNG()->nextInt(1, 0x7FFFFFFF);
  mesh::Packet* pkt = createTrace(_traceTag, 0, pathSz);
  if (!pkt) return false;
  sendDirect(pkt, list, cnt * sz);
  _traceSent = millis();
  _trace.valid = false;
  return true;
}

void InwNode::onTraceRecv(mesh::Packet* packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                          const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) {
  MyMesh::onTraceRecv(packet, tag, auth_code, flags, path_snrs, path_hashes, path_len);
  if (!_traceTag || tag != _traceTag) return;
  const uint8_t sz = 1 << (flags & 3);
  _trace.valid = true;
  _trace.hops = min<uint8_t>(path_len / sz, 16);
  for (uint8_t i = 0; i < _trace.hops; i++) {
    _trace.snr4[i] = (int8_t)path_snrs[i];
    _trace.hashes[i] = path_hashes[i * sz];
  }
  _trace.finalSnr4 = (int8_t)(packet->getSNR() * 4);
  _trace.rtt = millis() - _traceSent;
  _traceTag = 0;
  emit(NodeEvent::Trace);
}

bool InwNode::discover() {
  uint8_t data[10];
  _discoverTag = getRNG()->nextInt(1, 0x7FFFFFFF);
  data[0] = CTL_DISCOVER_REQ;
  data[1] = (1 << ADV_TYPE_REPEATER) | (1 << ADV_TYPE_ROOM) | (1 << ADV_TYPE_SENSOR);
  memcpy(&data[2], &_discoverTag, 4);
  memset(&data[6], 0, 4);
  mesh::Packet* pkt = createControlData(data, sizeof(data));
  if (!pkt) return false;
  sendZeroHop(pkt);
  discoveredCount = 0;
  discoverStarted = millis();
  return true;
}

void InwNode::onControlDataRecv(mesh::Packet* packet) {
  if ((packet->payload[0] & 0xF0) == CTL_DISCOVER_RESP && packet->payload_len >= 6 + PUB_KEY_SIZE && _discoverTag) {
    uint32_t tag;
    memcpy(&tag, &packet->payload[2], 4);
    if (tag == _discoverTag) {
      const uint8_t* pub = &packet->payload[6];
      DiscoverHit* h = nullptr;
      for (uint8_t i = 0; i < discoveredCount; i++) if (!memcmp(discovered[i].pub, pub, 32)) h = &discovered[i];
      if (!h && discoveredCount < DISCOVER_MAX) h = &discovered[discoveredCount++];
      if (h) {
        memcpy(h->pub, pub, 32);
        h->type = packet->payload[0] & 0x0F;
        h->theirSnr4 = (int8_t)packet->payload[1];
        h->ourSnr4 = (int8_t)(packet->getSNR() * 4);
        h->rssi = (int16_t)radio_driver.getLastRSSI();
        h->at = millis();
        emit(NodeEvent::Discover, h);
      }
    }
  }
  MyMesh::onControlDataRecv(packet);
}

// ---- adverts / contacts ------------------------------------------------------------------
bool InwNode::advertZeroHop() { return advert(); }

bool InwNode::advertFlood() {
  NodePrefs& p = prefs();
  mesh::Packet* pkt = p.advert_loc_policy == ADVERT_LOC_NONE
      ? createSelfAdvert(p.node_name)
      : createSelfAdvert(p.node_name, sensors.node_lat, sensors.node_lon);
  if (!pkt) return false;
  sendFlood(pkt, (uint32_t)0, (uint8_t)(p.path_hash_mode + 1));
  return true;
}

bool InwNode::shareContact(const uint8_t* pub) {
  ContactInfo* c = contact(pub);
  return c && shareContactZeroHop(*c);
}

void InwNode::resetPath(const uint8_t* pub) {
  ContactInfo* c = contact(pub);
  if (!c) return;
  resetPathTo(*c);
  _contactsGen++;
  saveContactsNow();
}

bool InwNode::forgetContact(const uint8_t* pub) {
  ContactInfo* c = contact(pub);
  if (!c) return false;
  uint8_t key[32];
  memcpy(key, pub, 32);
  if (!removeContact(*c)) return false;
  _ds.deleteBlobByKey(key, PUB_KEY_SIZE);
  _contactsGen++;
  saveContactsNow();
  return true;
}

void InwNode::toggleFavourite(const uint8_t* pub) {
  ContactInfo* c = contact(pub);
  if (!c) return;
  c->flags ^= 0x01;
  _contactsGen++;
  saveContactsNow();
}

static bool saveFilter(const ContactInfo& c) { return c.type != ADV_TYPE_NONE; }
void InwNode::saveContactsNow() { _ds.saveContacts(this, saveFilter); }
void InwNode::saveChannelsNow() { _ds.saveChannels(this); }

void InwNode::applyRadio() {
  NodePrefs& p = prefs();
  radio_driver.setParams(p.freq, p.bw, p.sf, p.cr);
  radio_driver.setTxPower(p.tx_power_dbm);
  radio_driver.setRxBoostedGainMode(p.rx_boosted_gain);
}

// ---- packet log / repeat counting ----------------------------------------------------------
void InwNode::logPacket(bool tx, uint8_t header, uint8_t pathLen, uint8_t len, float snr, float rssi) {
  PacketLogEntry& e = pktLog[pktHead];
  e.at = millis();
  e.tx = tx;
  e.routeType = header & 0x03;
  e.payloadType = (header >> 2) & 0x0F;
  e.hops = pathLen & 63;
  e.len = len;
  e.snr4 = (int8_t)(snr * 4);
  e.rssi = (int16_t)rssi;
  pktHead = (pktHead + 1) % PKT_LOG_MAX;
  if (pktCount < PKT_LOG_MAX) pktCount++;
  pktGen++;
}

void InwNode::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
  MyMesh::logRxRaw(snr, rssi, raw, len);
  if (len < 2) return;
  const uint8_t header = raw[0];
  const uint8_t route = header & 0x03;
  // Transport-coded routes carry 4 bytes of codes before the path length.
  const int pl = (route == 0 || route == 3) ? 5 : 1;
  logPacket(false, header, len > pl ? raw[pl] : 0, (uint8_t)min(len, 255), snr, rssi);
}

void InwNode::logTx(mesh::Packet* packet, int len) {
  MyMesh::logTx(packet, len);
  logPacket(true, packet->header, packet->path_len, (uint8_t)min(len, 255), 0, 0);
  // A text post leaving with an empty path is one we originated.
  const uint8_t t = packet->getPayloadType();
  if ((t == PAYLOAD_TYPE_GRP_TXT || t == PAYLOAD_TYPE_TXT_MSG) && packet->getPathHashCount() == 0 &&
      packet->isRouteFlood() && _awaitCount) {
    Echo& e = _echo[_echoHead];
    packet->calculatePacketHash(e.hash);
    e.histId = _awaitTx[0];
    e.at = millis();
    _echoHead = (_echoHead + 1) % ECHO_MAX;
    memmove(_awaitTx, _awaitTx + 1, sizeof(uint32_t) * 3);
    _awaitCount--;
  } else if ((t == PAYLOAD_TYPE_TXT_MSG) && _awaitCount && !packet->isRouteFlood()) {
    // Direct DM: nothing to count echoes of, just release the slot.
    memmove(_awaitTx, _awaitTx + 1, sizeof(uint32_t) * 3);
    _awaitCount--;
  }
}

void InwNode::logRx(mesh::Packet* packet, int len, float score) {
  MyMesh::logRx(packet, len, score);
  const uint8_t t = packet->getPayloadType();
  if (t != PAYLOAD_TYPE_GRP_TXT && t != PAYLOAD_TYPE_TXT_MSG) return;
  uint8_t h[MAX_HASH_SIZE];
  packet->calculatePacketHash(h);
  const uint32_t now = millis();
  for (auto& e : _echo) {
    if (e.histId && now - e.at < 120000 && !memcmp(e.hash, h, MAX_HASH_SIZE)) {
      history.bumpRepeat(e.histId);
      return;
    }
  }
}

// ---- bring-up ----------------------------------------------------------------------------
bool nodeBegin() {
  if (!radio_init()) return false;
  g_rng.begin(radio_driver.getRngSeed());

  // ~250 KB with 1000 contacts: PSRAM, not the internal heap BLE needs.
  void* mem = heap_caps_malloc(sizeof(InwNode), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!mem) mem = malloc(sizeof(InwNode));
  if (!mem) return false;
  g_node = new (mem) InwNode(radio_driver, g_rng, rtc_clock, g_tables, g_store);

  g_store.begin();
  g_node->begin(false);

  g_ble.begin(BLE_NAME_PREFIX, g_node->getNodePrefs()->node_name, g_node->getBLEPin());
  g_ifaces.addInterface(InterfaceType::Bluetooth, &g_ble);
  g_node->startInterface(g_ifaces);
  if (!inwBleWanted()) g_ifaces.disableBluetooth();
  sensors.begin();
  return true;
}

void nodeLoop() {
  if (!g_node) return;
  // Note for later: with a big contact list (1000+), MeshCore's own loop stalls
  // for about a second whenever traffic marks contacts dirty - it rewrites the
  // whole contacts file. Both that write and its timer are private to MyMesh,
  // so it can't be deferred from here; fixing it means patching MeshCore.
  g_node->loop();
  g_node->tick();
  rtc_clock.tick();
}

bool bleEnabled()   { return g_ifaces.isBluetoothEnabled(); }
void bleSetEnabled(bool on) { if (on) g_ifaces.enableBluetooth(); else g_ifaces.disableBluetooth(); }
bool bleConnected() { return g_ifaces.isConnected(); }
uint32_t blePin()   { return g_node ? g_node->getBLEPin() : 0; }
