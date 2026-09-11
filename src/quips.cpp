// The one-liner under the lock clock: jokes, mesh tips and a few lines built
// from this node's own contact list. A shuffled deck, so nothing repeats until
// every line has had its turn.

#include "quips.h"
#include "app.h"
#include "node.h"
#include "theme.h"
#include "settings.h"
#include "power.h"

static const char* const GENERAL[] = {
  // jokes
  "Somewhere a repeater on a roof is judging you.",
  "A flood advert is the mesh yelling MARCO.",
  "Your packet took four hops. It has seen things.",
  "No bars? No problem. No towers, even better.",
  "LoRa: long range, short messages, big feelings.",
  "The mesh never sleeps. It does occasionally nap.",
  "62.5 kHz of pure, uncut conversation.",
  "Every repeater was once just someone's bad idea.",
  "Antenna up. Standards high. Expectations moderate.",
  "Sent with love and a spreading factor of seven.",
  "Not a phone. Better. It can't get notifications from work.",
  "Your message is on its way. Probably. Mostly.",
  "Signal is temporary. Line of sight is forever.",
  "Hills: beautiful, majestic, and in the way.",
  "A good antenna is worth a hundred milliwatts.",
  "Two nodes walk into a bar. The bar forwards them.",
  "Delivered. The ack said so, and acks don't lie.",
  "Somebody somewhere just typed 'test'. Again.",
  "Airtime is a shared snack. Don't eat it all.",
  "If a packet floods and nobody hears it, it still counts.",
  "This pager has more friends than your phone.",
  "Batteries not included. Repeaters not guaranteed.",
  "Keep calm and check your SNR.",
  "Hop count is just a packet's frequent flyer miles.",
  "Low power, high hopes.",
  "Radio waves don't read no-trespassing signs.",
  "Your contact list is basically a neighbourhood map.",
  "Whoever put a node on that tower: legend.",
  "The mesh remembers. The mesh forwards. The mesh abides.",
  "Off grid, on mesh.",
  "Weather's bad? The mesh doesn't care. Much.",
  "Plot twist: the repeater was solar the whole time.",
  "Fewer bars, more character.",
  "A pager in 2026. Retro, but make it encrypted.",
  "Talking to strangers across the valley since boot.",
  "Out here, 'one hop' is a whole town.",
  "The best range test is just going for a walk.",
  "Somewhere a node is running on three AA batteries and spite.",
  // tips
  "Tip: favourite the nodes you DM so they never get pushed out.",
  "Tip: whole-mesh adverts cost everyone airtime. Go easy.",
  "Tip: height beats power. Lift the antenna before the watts.",
  "Tip: hold the wheel in a chat to pick an emoji.",
  "Tip: letter keys on home jump straight to an app.",
  "Tip: trace a route in Tools to see every hop.",
  "Tip: back up to SD before you tinker with settings.",
  "Tip: a nearby-only advert reaches neighbours without flooding.",
  "Tip: map tiles download over Wi-Fi as you browse.",
  "Tip: share a contact by tapping another phone with NFC.",
  "Tip: direct paths are faster. Say hi and let one form.",
  "Tip: set your position so people can find you on the map.",
  "Tip: themes live in Settings. Try Aurora at night.",
  "Tip: channel keys are secrets. Share them in person.",
  "Tip: one bad antenna hurts more than one weak radio.",
  "Tip: backups run daily. Keep a card in the pager.",
};

#include "quips_more.h"

static constexpr int N_THEMED = 20;
static const char* const THEMED[][N_THEMED] = {
  // INW
  { "Sasquatch approves this signal strength.",
    "Pine trees: nature's attenuators.",
    "Inland Northwest: great views, better line of sight.",
    "Spokane to the lake, one hop at a time.",
    "Big sky, small packets.",
    "Somewhere in the woods, a node is listening.",
    "Sasquatch has never been photographed. Nor has a clean SNR.",
    "The Palouse: rolling hills, rolling packets.",
    "Huckleberry season, mesh season. Same thing.",
    "Wheat fields make excellent Fresnel zones.",
    "From the Selkirks to the Snake, the mesh has you.",
    "Smoke season? Radio doesn't mind the haze.",
    "Lake cabin, no bars, full mesh.",
    "Up here, a hill is worth a thousand milliwatts.",
    "Basalt cliffs: dramatic, rugged, occasionally blocking.",
    "The river valleys carry signal like a hallway.",
    "Inland Northwest nights are dark. Nodes glow anyway.",
    "Moose crossing. Packet crossing. Watch for both.",
    "A tall pine is a free antenna mast. Ask it nicely.",
    "Rumour says Sasquatch runs a repeater. Unconfirmed." },
  // Blocks
  { "Punch trees. Place repeaters.",
    "Night falls. Adverts spawn.",
    "Mine a little, mesh a little.",
    "One block at a time, one hop at a time.",
    "Crafting table optional. Antenna required.",
    "Dig straight down? Terrible for signal.",
    "Build your repeater tower out of dirt. It works.",
    "Placed a torch. Placed a node. Both glow.",
    "Redstone wishes it had this much range.",
    "Every biome deserves a repeater.",
    "Chunk loaded. Mesh loaded.",
    "Survival tip: bring a pager.",
    "Found diamonds. Found a direct path. Good day.",
    "The pixel sky is clear. Great propagation.",
    "Building a base? Put an antenna on the roof.",
    "Sheep can't block signal. Hills can.",
    "Hunger bar full. Battery bar, less so.",
    "Respawn point: next to the charger.",
    "Cobblestone tower: ugly, tall, perfect for LoRa.",
    "The creepiest sound is an unacked message." },
  // Hero
  { "A hero never skips the ack.",
    "Hearts full. Signal full. Onward.",
    "Every hop is a dungeon cleared.",
    "The quest: deliver one message across the kingdom.",
    "Found: a secret repeater behind the waterfall.",
    "Legends speak of a node with perfect SNR.",
    "You got a new contact! Da-da-da-daaa.",
    "The old man in the cave had a repeater. Wise man.",
    "Rupees can't buy line of sight.",
    "Every tower you climb is a better repeater site.",
    "Save point reached. Backup complete.",
    "A shield blocks arrows. A mountain blocks signal.",
    "Heart containers: 3. Hops to the lake: 4.",
    "The kingdom is vast. The mesh is vaster.",
    "Slay dragons. Relay packets.",
    "Your sword is sharp. Your antenna is sharper.",
    "Adventurer's rule: never travel without a radio.",
    "The fairy fountain is also great for coverage.",
    "Side quest: find the farthest node on the map.",
    "The hero's journey ends with a perfect ack." },
  // Aurora
  { "The sky is busy tonight. So is the mesh.",
    "Quiet lights, quiet channels.",
    "Somewhere north, a repeater is watching the sky glow.",
    "Solar wind outside. Solar panels on the tower.",
    "Look up once in a while. The mesh will wait.",
    "Green sky, green ack.",
    "Curtains of light, curtains of chirps.",
    "The aurora is charged particles. So is your battery.",
    "Magnetic storms bother satellites more than LoRa.",
    "Cold clear nights make the stars and signals sharp.",
    "Somewhere a ridge-top node has the best view in town.",
    "The night is long. The packets are short.",
    "Stars don't need towers either.",
    "Soft light, soft vibes, soft SNR.",
    "The northern sky is a mesh of its own.",
    "Watch the sky dance. The mesh keeps time.",
    "Breathe in. Breathe out. Check the channel.",
    "Violet, teal, green. Also your status bar.",
    "Calm night on the mesh. Enjoy it.",
    "Everything is glowing. Even the unread count." },
};
static constexpr int N_GENERAL = sizeof(GENERAL) / sizeof(GENERAL[0]);
static constexpr int N_MORE = sizeof(MORE) / sizeof(MORE[0]);
static constexpr int N_LIVE = 11;

// Lines built from the contact list. Empty when there is nothing true to say.
static bool liveLine(int which, char* out, size_t cap) {
  if (!g_node) return false;
  const uint32_t now = app::now();
  const bool clock = app::timeValid();
  double mlat = 0, mlon = 0;
  const bool haveMe = app::myPosition(mlat, mlon);
  int heardDay = 0, repeaters = 0;
  float farKm = -1;
  char farName[33] = "", newest[33] = "";
  uint32_t newestAt = 0;
  ContactsIterator it = g_node->startContactsIterator();
  ContactInfo c;
  while (it.hasNext(g_node, c)) {
    if (!c.type) continue;
    if (c.type == ADV_TYPE_REPEATER) repeaters++;
    if (clock && c.lastmod && now - c.lastmod < 86400) heardDay++;
    if (c.lastmod > newestAt) { newestAt = c.lastmod; strlcpy(newest, c.name, sizeof(newest)); }
    if (haveMe && (c.gps_lat || c.gps_lon)) {
      const float d = (float)app::distanceKm(mlat, mlon, c.gps_lat / 1e6, c.gps_lon / 1e6);
      if (d > farKm && d < 1000) { farKm = d; strlcpy(farName, c.name, sizeof(farName)); }
    }
  }
  switch (which) {
    case 0: if (heardDay < 2) return false;
            snprintf(out, cap, "%d nodes checked in over the last day.", heardDay); return true;
    case 1: if (repeaters < 2) return false;
            snprintf(out, cap, "%d repeaters holding this mesh together.", repeaters); return true;
    case 2: if (farKm < 1) return false;
            snprintf(out, cap, "Furthest node on record: %s, %.0f km out.", farName, farKm); return true;
    case 3: snprintf(out, cap, "%d nodes in your contacts. Popular.", g_node->getNumContacts()); return true;
    case 4: if (!clock || !newestAt || now - newestAt > 3600) return false;
            snprintf(out, cap, "Latest to check in: %s.", newest); return true;
    case 5: if (app::batteryPct() > 25 || !app::batteryPct()) return false;
            snprintf(out, cap, "Battery at %u%%. The mesh can wait, the charger can't.", app::batteryPct()); return true;
  }
  const int hour = clock ? (int)((((int64_t)now + ui_settings.tzMinutes * 60) % 86400 + 86400) % 86400 / 3600) : -1;
  switch (which) {
    case 6: if (hour < 0 || hour > 4) return false;
            snprintf(out, cap, "%d:%02d and still up. The mesh respects it.", hour ? hour : 12, (int)((now / 60) % 60)); return true;
    case 7: if (hour < 5 || hour > 9) return false;
            snprintf(out, cap, "Morning. %d nodes were busy while you slept.", heardDay > 1 ? heardDay : g_node->getNumContacts()); return true;
    case 8: if (!power::saver()) return false;
            snprintf(out, cap, "Battery saver on. Radios napping, messages still flow."); return true;
    case 9: if (!power::holding()) return false;
            snprintf(out, cap, "Charging paused at 80%% to go easy on the battery."); return true;
    case 10: if (!g_node || repeaters < 1) return false;
            snprintf(out, cap, "%d of your %d contacts are repeaters.", repeaters, g_node->getNumContacts()); return true;
  }
  return false;
}

const char* quipNext() {
  static const int TOTAL = N_GENERAL + N_MORE + N_THEMED + N_LIVE;
  static uint16_t deck[TOTAL];
  static int pos = TOTAL;
  static char line[96];
  for (int tries = 0; tries < TOTAL * 2; tries++) {
    if (pos >= TOTAL) {
      for (int i = 0; i < TOTAL; i++) deck[i] = i;
      for (int i = TOTAL - 1; i > 0; i--) { const int j = esp_random() % (i + 1); const uint16_t t = deck[i]; deck[i] = deck[j]; deck[j] = t; }
      pos = 0;
    }
    int k = deck[pos++];
    if (k < N_GENERAL) return GENERAL[k];
    k -= N_GENERAL;
    if (k < N_MORE) return MORE[k];
    k -= N_MORE;
    if (k < N_THEMED) {
      const uint8_t s = nav.theme().style;
      return THEMED[s < 4 ? s : 0][k];
    }
    if (liveLine(k - N_THEMED, line, sizeof(line))) return line;
  }
  return GENERAL[0];
}
