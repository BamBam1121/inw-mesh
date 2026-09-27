// What other screens can ask of the chats (chats.cpp): a home screen's view of
// the latest conversations, and opening one.
#pragma once
#include <Arduino.h>
#include "history.h"

struct ChatEntry {
  ConvKey key;
  char name[32];
  uint8_t kind;
  uint32_t lastTs, lastId;
  uint16_t unread;
  bool mention;
  char preview[72];
};

// The conversations with messages in them, most recent activity first: the same
// names and previews the Messages list shows. Returns how many were filled in.
uint8_t recentChats(ChatEntry* out, uint8_t max);
void openThread(const ConvKey& k);
