// Message history. MeshCore keeps none on the node, so we do: a PSRAM ring
// persisted as an append-only log (one small write per message instead of
// rewriting the store, which on SPIFFS stalls the radio loop).
//
// Conversations are keyed by 6 stable bytes: a contact's key prefix or a
// channel's secret prefix, since channel slots can be reordered.

#pragma once
#include <Arduino.h>
#include <SPIFFS.h>

enum : uint8_t { CONV_CONTACT = 1, CONV_CHANNEL = 2 };
enum : uint8_t { ST_RECV = 0, ST_SENDING, ST_SENT, ST_DELIVERED, ST_FAILED };
enum : uint8_t { HF_OUT = 1, HF_MENTION = 2, HF_ROOM = 4 };

struct ConvKey {
  uint8_t type = 0;
  uint8_t id[6] = {0};
  bool operator==(const ConvKey& o) const { return type == o.type && !memcmp(id, o.id, 6); }
  bool valid() const { return type != 0; }
  static ConvKey contact(const uint8_t* pub) { ConvKey k; k.type = CONV_CONTACT; memcpy(k.id, pub, 6); return k; }
  static ConvKey channel(const uint8_t* secret) { ConvKey k; k.type = CONV_CHANNEL; memcpy(k.id, secret, 6); return k; }
};

struct HistMsg {
  uint32_t id;
  ConvKey  conv;
  uint8_t  flags;
  uint8_t  status;
  uint8_t  hops;          // 0xFF = direct / unknown
  int8_t   snr4;
  uint8_t  repeats;       // repeaters heard re-sending our post
  uint8_t  attempts;
  uint16_t rtt10;         // ack round trip, 10 ms units
  uint32_t ts;            // epoch, our clock at receipt / send
  char     sender[24];
  char     text[160];
};

class History {
public:
  static constexpr uint16_t CAP = 800;
  static constexpr uint8_t  READ_MAX = 96;

  bool begin();
  uint32_t add(const ConvKey& k, uint8_t flags, uint8_t status, const char* sender,
               const char* text, uint32_t ts, uint8_t hops = 0xFF, int8_t snr4 = 0);
  HistMsg* find(uint32_t id);
  void setStatus(uint32_t id, uint8_t st, uint8_t attempts = 0xFF, uint16_t rtt10 = 0);
  void bumpRepeat(uint32_t id);

  // Oldest first. Callers that walk a whole conversation should use collect().
  uint16_t collect(const ConvKey& k, uint32_t* ids, uint16_t max);
  HistMsg* last(const ConvKey& k);
  uint16_t count(const ConvKey& k);
  uint16_t unread(const ConvKey& k);
  uint16_t totalUnread();
  bool     hasMention(const ConvKey& k);
  void markRead(const ConvKey& k);
  void clearConv(const ConvKey& k);
  void clearAll();

  // Distinct conversations that have messages, most recent first.
  uint16_t conversations(ConvKey* out, uint16_t max);

  uint32_t gen = 1;             // bumps on any change, for views to redraw
  uint16_t size() const { return _count; }

private:
  HistMsg* at(uint16_t i) { return &_ring[(_head + CAP - _count + i) % CAP]; } // i: 0 oldest
  void appendRecord(char kind, const void* data, size_t len);
  void writeStatus(const HistMsg& m);
  void loadLog();
  void compact();
  uint32_t readMark(const ConvKey& k);
  void saveReads();
  void loadReads();

  HistMsg* _ring = nullptr;
  uint16_t _head = 0, _count = 0;
  uint32_t _nextId = 1;
  struct ReadMark { ConvKey k; uint32_t id; };
  ReadMark _reads[READ_MAX];
  uint8_t  _readCount = 0;
};

extern History history;
