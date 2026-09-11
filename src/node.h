// InwNode: MeshCore's companion node (examples/companion_radio MyMesh) with our
// hooks. Every override calls MyMesh first, so a phone on BLE sees exactly what
// stock firmware would send it; the on-device UI just listens in.

#pragma once
#include <Arduino.h>
#include <MyMesh.h>

// ---- what the UI gets told ---------------------------------------------------
enum class NodeEvent : uint8_t {
  DirectMsg, ChannelMsg, RoomMsg, NewContact, Delivered, Failed,
  LoginOk, LoginFail, Status, Telemetry, Trace, Discover, CliReply, ContactsChanged,
};

// Result of the last status request, RepeaterStats from simple_repeater.
struct RemoteStatus {
  bool     valid = false;
  uint8_t  pub[6] = {0};
  uint16_t battMv = 0, txQueue = 0;
  int16_t  noiseFloor = 0, lastRssi = 0, lastSnr4 = 0;
  uint32_t recv = 0, sent = 0, airSecs = 0, upSecs = 0;
  uint32_t sentFlood = 0, sentDirect = 0, recvFlood = 0, recvDirect = 0;
  uint32_t rtt = 0;
};

struct TraceResult {
  bool     valid = false;
  uint8_t  hops = 0;
  int8_t   snr4[16] = {0};      // per hop, x4
  uint8_t  hashes[16] = {0};
  int8_t   finalSnr4 = 0;
  uint32_t rtt = 0;
};

struct DiscoverHit {
  uint8_t  pub[32];
  uint8_t  type;
  int8_t   theirSnr4;           // how they heard us
  int8_t   ourSnr4;             // how we heard them
  int16_t  rssi;
  uint32_t at;                  // millis
};

struct PacketLogEntry {
  uint32_t at;                  // millis
  uint8_t  payloadType, routeType, hops, len;
  int8_t   snr4;
  int16_t  rssi;
  bool     tx;
};

// One outgoing DM awaiting its ACK.
struct PendingDM {
  bool     used = false;
  uint8_t  pub[32];
  uint32_t histId = 0;
  uint32_t ts = 0;              // the message timestamp: kept across retries so the
                                // recipient sees one message, not three
  uint32_t acks[4] = {0};       // one expected ack per attempt
  uint8_t  attempt = 0;
  uint32_t sentAt = 0, deadline = 0;
};

class InwNode : public MyMesh {
public:
  InwNode(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc,
          SimpleMeshTables& tables, DataStore& store);

  void (*onEvent)(NodeEvent e, const void* arg) = nullptr;

  // -- sending ----------------------------------------------------------------
  // DMs are tracked to delivery with retries; channel posts are fire-and-forget
  // and count the repeaters heard re-broadcasting them.
  bool sendDM(const uint8_t* pub, const char* text, uint32_t histId);
  bool sendChannel(uint8_t idx, const char* text, uint32_t histId);
  bool resendDM(const uint8_t* pub, const char* text, uint32_t histId);

  // -- infrastructure -----------------------------------------------------------
  bool login(const uint8_t* pub, const char* password);
  bool requestStatus(const uint8_t* pub);
  bool requestTelemetry(const uint8_t* pub);
  bool sendCli(const uint8_t* pub, const char* cmd);
  bool trace(const uint8_t* pub);
  bool discover();
  bool advertZeroHop();
  bool advertFlood();
  bool shareContact(const uint8_t* pub);
  void resetPath(const uint8_t* pub);
  bool forgetContact(const uint8_t* pub);
  void toggleFavourite(const uint8_t* pub);
  bool addChannelNamed(const char* name, const uint8_t* secret16);
  bool removeChannel(uint8_t idx);

  // -- persistence ----------------------------------------------------------------
  void saveContactsNow();
  void saveChannelsNow();
  void savePrefsNow() { savePrefs(); }
  void applyRadio();            // push prefs radio params to the SX1262 now

  // -- state for the UI -----------------------------------------------------------
  void tick();                  // DM retries and timeouts
  NodePrefs& prefs() { return *getNodePrefs(); }
  const char* name() { return getNodeName(); }
  uint32_t contactsGen() const { return _contactsGen; }
  ContactInfo* contact(const uint8_t* pub) { return lookupContactByPubKey(pub, PUB_KEY_SIZE); }
  ContactInfo* contactByPrefix(const uint8_t* pre, int n) { return lookupContactByPubKey(pre, n); }
  int findChannelBySecret(const uint8_t* secret6);

  const RemoteStatus& lastStatus() const { return _status; }
  const TraceResult&  lastTrace()  const { return _trace; }
  char   telemetryText[200] = "";
  uint8_t loginState(const uint8_t* pub) const;   // 0 none, 1 pending, 2 ok, 3 failed
  bool   loginIsAdmin() const { return _loginAdmin; }

  static constexpr uint8_t DISCOVER_MAX = 24;
  DiscoverHit discovered[DISCOVER_MAX];
  uint8_t  discoveredCount = 0;
  uint32_t discoverStarted = 0;

  static constexpr uint8_t PKT_LOG_MAX = 64;
  PacketLogEntry pktLog[PKT_LOG_MAX];
  uint8_t  pktHead = 0, pktCount = 0;
  uint32_t pktGen = 0;

  // CLI replies from a remote repeater/room, newest last.
  static constexpr uint8_t CLI_LINES = 40;
  char     cliLog[CLI_LINES][72];
  uint8_t  cliCount = 0;
  uint32_t cliGen = 0;
  void cliAppend(const char* prefix, const char* text);
  void cliClear() { cliCount = 0; cliGen++; }

  float lastRssi() { return radio_driver.getLastRSSI(); }
  float lastSnr()  { return radio_driver.getLastSNR(); }
  uint32_t rxCount() { return radio_driver.getPacketsRecv(); }
  uint32_t txCount() { return radio_driver.getPacketsSent(); }
  uint32_t rxErrors() { return radio_driver.getPacketsRecvErrors(); }
  int noiseFloor() { return radio_driver.getNoiseFloor(); }
  uint32_t airtimeSecs() { return getTotalAirTime() / 1000; }

protected:
  ContactInfo* processAck(const uint8_t* data) override;
  void onMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t ts, const char* text) override;
  void onCommandDataRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t ts, const char* text) override;
  void onSignedMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t ts,
                           const uint8_t* sender_prefix, const char* text) override;
  void onChannelMessageRecv(const mesh::GroupChannel& ch, mesh::Packet* pkt, uint32_t ts,
                            const char* text) override;
  void onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) override;
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onContactPathUpdated(const ContactInfo& contact) override;
  void onTraceRecv(mesh::Packet* packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                   const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) override;
  void onControlDataRecv(mesh::Packet* packet) override;
  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;
  void logRx(mesh::Packet* packet, int len, float score) override;
  void logTx(mesh::Packet* packet, int len) override;

private:
  void emit(NodeEvent e, const void* arg = nullptr) { if (onEvent) onEvent(e, arg); }
  void logPacket(bool tx, uint8_t header, uint8_t pathLen, uint8_t len, float snr, float rssi);
  PendingDM* freePending();
  bool transmitDM(PendingDM& p, const char* text);

  DataStore& _ds;
  uint32_t _contactsGen = 1;

  static constexpr uint8_t PENDING_MAX = 8;
  PendingDM _pending[PENDING_MAX];

  // Our own originated flood posts, by packet hash, so repeats heard back can be
  // counted against the message that caused them.
  struct Echo { uint8_t hash[MAX_HASH_SIZE]; uint32_t histId; uint32_t at; };
  static constexpr uint8_t ECHO_MAX = 8;
  Echo     _echo[ECHO_MAX];
  uint8_t  _echoHead = 0;
  uint32_t _awaitTx[4] = {0};   // hist ids queued but not yet on air, FIFO
  uint8_t  _awaitCount = 0;

  uint8_t  _loginPub[4] = {0};
  uint8_t  _loginState = 0;
  bool     _loginAdmin = false;
  uint32_t _loginDeadline = 0;

  uint8_t  _statusPub[4] = {0};
  uint32_t _statusTag = 0, _statusSent = 0;
  uint8_t  _telemPub[4] = {0};
  uint32_t _telemTag = 0;
  uint32_t _traceTag = 0, _traceSent = 0;
  uint32_t _discoverTag = 0;

  RemoteStatus _status;
  TraceResult  _trace;
};

// Globals owned by node.cpp.
extern InwNode* g_node;
extern DataStore g_store;
extern MultiSerialInterface g_ifaces;

// Bring the radio and node up. Returns false if the radio didn't answer.
bool nodeBegin();
void nodeLoop();
bool bleEnabled();
void bleSetEnabled(bool on);
bool bleConnected();
uint32_t blePin();
