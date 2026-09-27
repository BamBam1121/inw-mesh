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
#ifndef MAX_GROUP_CHANNELS
#define MAX_GROUP_CHANNELS 40
#endif
#ifndef MAX_CONTACTS
#define MAX_CONTACTS 2000
#endif

namespace mesh {
struct Identity { uint8_t pub_key[PUB_KEY_SIZE]; };
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
};

class InwNode {
public:
  InwNode();
  mesh::LocalIdentity self_id;
  void (*onEvent)(NodeEvent e, const void* arg) = nullptr;

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
bool bleEnabled();
bool bleConnected();
uint32_t blePin();
