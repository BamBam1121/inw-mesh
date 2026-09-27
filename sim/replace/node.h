// Simulator stand-in for src/node.h: the same names the screens use (InwNode,
// ContactInfo, ChannelDetails, NodePrefs, mesh::Utils), holding sample contacts
// and channels instead of a MeshCore node on a radio. Field names and types
// follow MeshCore's, so the firmware's screen code compiles against it unchanged.
#pragma once
#include <Arduino.h>

#define PUB_KEY_SIZE       32
#define PATH_HASH_SIZE     1
#define MAX_PATH_SIZE      64
#define MAX_HASH_SIZE      8
#define OUT_PATH_UNKNOWN   0xFF
#define ADV_TYPE_NONE      0
#define ADV_TYPE_CHAT      1
#define ADV_TYPE_REPEATER  2
#define ADV_TYPE_ROOM      3
#define ADV_TYPE_SENSOR    4
#define ADVERT_LOC_NONE    0
#define ADVERT_LOC_SHARE   1
#define BLE_NAME_PREFIX    "Squatch"
#define FIRMWARE_VERSION   "v1.9"
#ifndef MAX_GROUP_CHANNELS
#define MAX_GROUP_CHANNELS 40
#endif
#ifndef MAX_CONTACTS
#define MAX_CONTACTS 2000
#endif

namespace mesh {
struct Identity {
  uint8_t pub_key[PUB_KEY_SIZE];
  Identity() { memset(pub_key, 0, sizeof(pub_key)); }
  explicit Identity(const uint8_t* pub) { memcpy(pub_key, pub, PUB_KEY_SIZE); }
};
struct LocalIdentity : Identity {};
class GroupChannel {
public:
  uint8_t hash[PATH_HASH_SIZE];
  uint8_t secret[PUB_KEY_SIZE];
};
struct Utils {
  static void toHex(char* dest, const uint8_t* src, size_t len) {
    static const char H[] = "0123456789ABCDEF";
    for (size_t i = 0; i < len; i++) { dest[2 * i] = H[src[i] >> 4]; dest[2 * i + 1] = H[src[i] & 15]; }
    dest[2 * len] = 0;
  }
  // Not SHA-256: only the channel's identity depends on it here, and it just has
  // to be the same for the same name.
  static void sha256(uint8_t* hash, size_t hash_len, const uint8_t* msg, int msg_len) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < msg_len; i++) { h ^= msg[i]; h *= 16777619u; }
    for (size_t i = 0; i < hash_len; i++) { h ^= (uint32_t)i; h *= 16777619u; hash[i] = (uint8_t)(h >> 24); }
  }
  static bool fromHex(uint8_t* dest, int dest_size, const char* src) {
    const int n = (int)strlen(src);
    if (n != dest_size * 2) return false;
    for (int i = 0; i < dest_size; i++) {
      unsigned v = 0;
      if (sscanf(src + 2 * i, "%2x", &v) != 1) return false;
      dest[i] = (uint8_t)v;
    }
    return true;
  }
};
}  // namespace mesh

struct ContactInfo {
  mesh::Identity id;
  char name[32];
  uint8_t type;
  uint8_t flags;
  uint8_t out_path_len;
  bool shared_secret_valid;
  uint8_t out_path[MAX_PATH_SIZE];
  uint32_t last_advert_timestamp;
  uint32_t lastmod;
  int32_t gps_lat, gps_lon;
  uint32_t sync_since;
};

struct ChannelDetails {
  mesh::GroupChannel channel;
  char name[32];
};

struct NodePrefs {
  float freq = 910.525f;
  uint8_t sf = 7, cr = 5;
  float bw = 62.5f;
  int8_t tx_power_dbm = 22;
  char node_name[32] = "";
  double node_lat = 0, node_lon = 0;
  uint8_t advert_loc_policy = 0;
  uint8_t telemetry_mode_base = 1, telemetry_mode_loc = 1, telemetry_mode_env = 1;
  uint8_t manual_add_contacts = 0;
  uint8_t path_hash_mode = 1;
  float airtime_factor = 1.0f, rx_delay_base = 0.0f;
  uint8_t multi_acks = 0, rx_boosted_gain = 1, autoadd_config = 0, autoadd_max_hops = 0;
  uint32_t ble_pin = 123456;
  char default_scope_name[31] = "";
  uint8_t default_scope_key[16] = {0};
  bool repeat = false;
  bool isRepeatEn() const { return repeat; }
  void setRepeatEn(bool en) { repeat = en; }
};

class InwNode;
class ContactsIterator {
  int next_idx;
public:
  ContactsIterator(int start) : next_idx(start) {}
  bool hasNext(const InwNode* mesh, ContactInfo& dest);
};

enum class NodeEvent : uint8_t {
  DirectMsg, ChannelMsg, RoomMsg, NewContact, Delivered, Failed,
  LoginOk, LoginFail, Status, Telemetry, Trace, Discover, CliReply, ContactsChanged,
  Regions,
};

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
  int8_t   snr4[16] = {0};
  uint8_t  hashes[16] = {0};
  int8_t   finalSnr4 = 0;
  uint32_t rtt = 0;
};
struct DiscoverHit {
  uint8_t  pub[32];
  uint8_t  type;
  int8_t   theirSnr4, ourSnr4;
  int16_t  rssi;
  uint32_t at;
};
struct PacketLogEntry {
  uint32_t at;
  uint8_t  payloadType, routeType, hops, len;
  int8_t   snr4;
  int16_t  rssi;
  bool     tx;
};

struct AdvertPath {
  uint8_t pubkey_prefix[7];
  uint8_t path_len;
  char    name[32];
  uint32_t recv_timestamp;
  uint8_t path[MAX_PATH_SIZE];
};

class InwNode {
public:
  InwNode();
  // Radio statistics (Tools > signal), made up.
  int noiseFloor() { return -112; }
  uint32_t rxErrors() { return 3; }
  uint32_t airtimeSecs() { return 41; }
  uint32_t getNumSentFlood() { return 30; }
  uint32_t getNumSentDirect() { return 26; }
  uint32_t getNumRecvFlood() { return 1100; }
  uint32_t getNumRecvDirect() { return 134; }
  int getRecentlyHeard(AdvertPath*, int) { return 0; }
  mesh::LocalIdentity self_id;
  void (*onEvent)(NodeEvent e, const void* arg) = nullptr;

  // Remote admin, status, trace: nothing answers in the simulator.
  bool requestStatus(const uint8_t*) { return true; }
  bool requestTelemetry(const uint8_t*) { return true; }
  // Three nearby repeaters, answering at once: their regions ("*" = they still
  // pass the whole mesh).
  bool requestRegions(const uint8_t*) {
    static const char* LISTS[] = {"*,spokane,wa,", "spokane,cda,", "*,spokane,wa,idaho,"};
    strlcpy(regionsReply.names, LISTS[_regionAsks++ % 3], sizeof(regionsReply.names));
    regionsGen++;
    return true;
  }
  int _regionAsks = 0;
  struct RegionsReply { uint8_t pub[32]; char names[180]; };
  RegionsReply regionsReply = {};
  uint32_t regionsGen = 0;
  bool sendCli(const uint8_t*, const char*) { return true; }
  bool trace(const uint8_t*) { return true; }
  bool discover() { return true; }
  bool advertZeroHop() { return true; }
  bool advertFlood() { return true; }
  bool shareContact(const uint8_t*) { return true; }
  void resetPath(const uint8_t*) {}
  bool forgetContact(const uint8_t*) { return true; }
  int  countStale(uint32_t) { return 0; }
  int  forgetStale(uint32_t) { return 0; }
  void toggleFavourite(const uint8_t* pub) { ContactInfo* c = contact(pub); if (c) c->flags ^= 1; }
  bool removeChannel(uint8_t) { return true; }
  void saveContactsNow() {}
  void saveChannelsNow() {}
  void savePrefsNow() {}
  void applyRadio() {}
  const RemoteStatus& lastStatus() const { return _status; }
  const TraceResult&  lastTrace()  const { return _trace; }
  char telemetryText[200] = "";
  bool loginIsAdmin() const { return false; }
  static constexpr uint8_t DISCOVER_MAX = 24;
  DiscoverHit discovered[DISCOVER_MAX];
  uint8_t  discoveredCount = 0;
  uint32_t discoverStarted = 0;
  static constexpr uint8_t PKT_LOG_MAX = 64;
  PacketLogEntry pktLog[PKT_LOG_MAX];
  uint8_t  pktHead = 0, pktCount = 0;
  uint32_t pktGen = 0;
  static constexpr uint8_t CLI_LINES = 40;
  char     cliLog[CLI_LINES][72];
  uint8_t  cliCount = 0;
  uint32_t cliGen = 0;
  void cliAppend(const char*, const char*) {}
  void cliClear() { cliCount = 0; cliGen++; }
  float lastRssi() { return -92; }
  float lastSnr()  { return 6.5f; }
  uint32_t rxCount() { return 1234; }
  uint32_t txCount() { return 56; }
  RemoteStatus _status;
  TraceResult _trace;

  // sample data, filled by the simulator
  ContactInfo contacts[64];
  int num_contacts = 0;
  ChannelDetails channels[MAX_GROUP_CHANNELS];
  NodePrefs _prefs;

  bool sendDM(const uint8_t*, const char*, uint32_t) { return true; }
  bool sendChannel(uint8_t, const char*, uint32_t) { return true; }
  bool resendDM(const uint8_t*, const char*, uint32_t) { return true; }
  bool login(const uint8_t*, const char*) { return true; }
  uint8_t loginState(const uint8_t*) const { return 0; }
  bool addChannelNamed(const char*, const uint8_t*) { return true; }
  bool addContact(const ContactInfo& c) { if (num_contacts >= 64) return false; contacts[num_contacts++] = c; return true; }

  NodePrefs& prefs() { return _prefs; }
  const char* name() { return _prefs.node_name; }
  int getNumContacts() const { return num_contacts; }
  uint32_t contactsGen() const { return 1; }
  ContactsIterator startContactsIterator() { return ContactsIterator(0); }
  bool getChannel(int idx, ChannelDetails& d) {
    if (idx < 0 || idx >= MAX_GROUP_CHANNELS) return false;
    d = channels[idx];
    return true;
  }
  int findChannelBySecret(const uint8_t* secret6) {
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++)
      if (channels[i].name[0] && !memcmp(channels[i].channel.secret, secret6, 6)) return i;
    return -1;
  }
  ContactInfo* contact(const uint8_t* pub) { return contactByPrefix(pub, PUB_KEY_SIZE); }
  ContactInfo* contactByPrefix(const uint8_t* pre, int n) {
    for (int i = 0; i < num_contacts; i++) if (!memcmp(contacts[i].id.pub_key, pre, n)) return &contacts[i];
    return nullptr;
  }
};

inline bool ContactsIterator::hasNext(const InwNode* mesh, ContactInfo& dest) {
  if (next_idx >= mesh->num_contacts) return false;
  dest = mesh->contacts[next_idx++];
  return true;
}

extern InwNode* g_node;
struct SimSensors { double node_lat = 0, node_lon = 0; };
extern SimSensors sensors;
void bleSetEnabled(bool on);
bool bleEnabled();
bool bleConnected();
uint32_t blePin();
