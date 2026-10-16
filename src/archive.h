// Messages kept on the SD card after they leave history (GitHub issue #7).
//
// History holds the newest 800 messages across every chat (history.h). A message
// about to be pushed out of it is copied here instead of lost: one text file per
// chat on the card, /inw/msgs/<c|g><6 key bytes>.txt, a line a message. A chat's
// menu reads the newest few hundred back ("older messages"). No card: nothing is
// kept, as before.
//
// The card shares its bus with the radio and the screen, so nothing is written the
// moment a message is pushed out: they wait in a small queue and go to the card
// while the screen is dark, or straight away if the queue is nearly full.
#pragma once
#include <Arduino.h>
#include <vector>
#include "history.h"

namespace archive {
void push(const HistMsg& m);                 // history.cpp, just before m is overwritten
void tick(bool screenDark);                  // from loop()
bool has(const ConvKey& k);                  // anything on the card for this chat
// The newest `max` archived messages of a chat, newest first, as lines for a
// TextPageView ('#' lines are the dim headers), each message wrapped to `cols`.
void read(const ConvKey& k, std::vector<String>& out, int max, int cols);
void forget(const ConvKey& k);               // the chat was cleared
void forgetAll();                            // every chat was cleared
}
