// Per-conversation notification settings (channels, and contacts/rooms too).
// Stored in NVS under the conversation's 6-byte key, so they follow a channel
// even if its slot moves. Anything not set follows the global settings.

#pragma once
#include <Arduino.h>
#include "history.h"

enum NotifyMode : uint8_t {
  NM_DEFAULT = 0,    // follow Settings > Notifications
  NM_ALL,            // every message, sound and vibration
  NM_MENTIONS,       // only when @mentioned
  NM_SILENT,         // banner only, no sound or vibration
  NM_MUTED,          // nothing; still counted as unread
  NM_COUNT
};

uint8_t notifyMode(const ConvKey& k);
void setNotifyMode(const ConvKey& k, uint8_t mode);
const char* notifyModeName(uint8_t mode);
