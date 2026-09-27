// Squatch Mesh screen simulator: runs the firmware's screens on a PC and saves
// each frame as a PPM, the same way the pager's USB "shot" command composes one.
//
// usage: squatch_sim.exe <out dir>
#include <Arduino.h>
#include <sys/stat.h>
#include "sim.h"
#include "app.h"
#include "node.h"
#include "history.h"
#include "settings.h"
#include "ui.h"
#include "statusbar.h"

View* makeHomeView();
View* makeLockView();

static const char* s_out = ".";

// ---- a believable little mesh ---------------------------------------------------------------
static void pub(uint8_t* p, uint8_t seed) { for (int i = 0; i < 32; i++) p[i] = (uint8_t)(seed * 37 + i * 11 + 5); }

static ContactInfo& addContact(const char* name, uint8_t type, uint8_t seed, uint8_t pathLen, uint32_t agoS) {
  ContactInfo& c = g_node->contacts[g_node->num_contacts++];
  memset(&c, 0, sizeof(c));
  pub(c.id.pub_key, seed);
  strlcpy(c.name, name, sizeof(c.name));
  c.type = type;
  c.out_path_len = pathLen;
  c.lastmod = sim::epoch - agoS;
  c.last_advert_timestamp = c.lastmod;
  return c;
}

static void addChannel(int idx, const char* name, uint8_t seed) {
  ChannelDetails& ch = g_node->channels[idx];
  strlcpy(ch.name, name, sizeof(ch.name));
  for (int i = 0; i < 32; i++) ch.channel.secret[i] = (uint8_t)(seed * 13 + i * 7 + 1);
  ch.channel.hash[0] = ch.channel.secret[0];
}

static void populate() {
  strlcpy(g_node->prefs().node_name, "Squatch T-Deck", sizeof(g_node->prefs().node_name));
  addContact("Mt Spokane RPT", ADV_TYPE_REPEATER, 1, 0, 120);
  addContact("Browne Mtn RPT", ADV_TYPE_REPEATER, 2, 1, 600);
  addContact("CDA Hill RPT", ADV_TYPE_REPEATER, 3, 2, 3000);
  addContact("Trailhead", ADV_TYPE_CHAT, 4, 1, 300);
  addContact("Ridge Runner", ADV_TYPE_CHAT, 5, 2, 900);
  addContact("Basecamp", ADV_TYPE_CHAT, 6, 0, 60);
  addContact("Silver Valley Room", ADV_TYPE_ROOM, 7, 2, 7200);
  addContact("Pine Marten", ADV_TYPE_CHAT, 8, 3, 86400);
  addChannel(0, "Public", 1);
  addChannel(1, "#inw", 2);
  addChannel(2, "#hiking", 3);
}

// The messages, fresh for each theme (opening a thread marks it read).
static void populateHistory() {
  history.clearAll();
  const uint32_t t = sim::epoch;
  const ConvKey pubc = ConvKey::channel(g_node->channels[0].channel.secret);
  const ConvKey inw = ConvKey::channel(g_node->channels[1].channel.secret);
  const ConvKey hike = ConvKey::channel(g_node->channels[2].channel.secret);
  const ConvKey trail = ConvKey::contact(g_node->contacts[3].id.pub_key);
  const ConvKey ridge = ConvKey::contact(g_node->contacts[4].id.pub_key);
  const uint8_t p1[] = {0x2A}, p2[] = {0x2A, 0x51}, p3[] = {0x71, 0x2A, 0x51};

  history.add(inw, 0, ST_RECV, "Ridge Runner", "Anyone copy from the north side of the lake?", t - 3100, 2, 22, p2, 2);
  history.add(inw, HF_OUT, ST_DELIVERED, "Squatch T-Deck", "Loud and clear from Spokane", t - 3000);
  history.add(inw, 0, ST_RECV, "Pine Marten", "Copy. Browne Mtn repeater is back up", t - 2880, 3, 14, p3, 3);
  history.add(inw, 0, ST_RECV, "Trailhead", "Heading up Mt Spokane at 10, will check in from the top", t - 1500, 1, 30, p1, 1);
  history.add(inw, 0, ST_RECV, "Basecamp", "Nice. Coffee at the trailhead after?", t - 900, 0xFF, 36);
  history.markRead(inw);
  history.add(inw, 0, ST_RECV, "Ridge Runner", "Count me in, bringing the T-Deck", t - 240, 2, 18, p2, 2);
  history.add(pubc, 0, ST_RECV, "Silver Valley Room", "Weekly net tonight at 7 on Public", t - 7000, 2, 8, p2, 2);
  history.add(pubc, HF_OUT, ST_SENT, "Squatch T-Deck", "Checking in from the valley", t - 6800);
  history.markRead(pubc);
  history.add(hike, 0, ST_RECV, "Trailhead", "Snow at 5000 ft already", t - 1800, 1, 20, p1, 1);
  history.add(trail, 0, ST_RECV, "Trailhead", "At the summit, signal is great up here", t - 600, 1, 26, p1, 1);
  history.add(trail, HF_OUT, ST_DELIVERED, "Squatch T-Deck", "Nice! How many hops to you?", t - 540);
  history.add(trail, 0, ST_RECV, "Trailhead", "Just one, straight through Mt Spokane", t - 500, 1, 26, p1, 1);
  history.markRead(trail);
  history.add(ridge, 0, ST_RECV, "Ridge Runner", "Save me a seat", t - 200, 2, 16, p2, 2);
}

// ---- output ---------------------------------------------------------------------------------
static void savePPM(Canvas& g, const char* name) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.ppm", s_out, name);
  FILE* f = fopen(path, "wb");
  if (!f) { printf("can't write %s\n", path); return; }
  fprintf(f, "P6\n%d %d\n255\n", (int)g.width(), (int)g.height());
  for (int y = 0; y < g.height(); y++)
    for (int x = 0; x < g.width(); x++) {
      const uint16_t c = g.readPixel(x, y);
      const uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31) };
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
  printf("saved %s\n", name);
}

// What the USB "shot" command does: the screen as the panel would show it.
static void shot(const char* name) {
  View* v = nav.top();
  Canvas& g = nav.canvas();
  g.fillScreen(theme.bg);
  if (v && !v->isLock()) drawStatusBar(g, theme);
  if (v) v->draw(g);
  nav.drawOverlays(g);
  savePPM(g, name);
}

// Let a screen run for a while (animations, a carousel settling) before the shot.
static void run(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 16) { sim::advance(16); nav.tick(); if (nav.top()) nav.top()->tick(); }
}

static void clearTo(View* base) {
  while (nav.top() && nav.top() != base) nav.pop();
}

int main(int argc, char** argv) {
  if (argc > 1) s_out = argv[1];
  mkdir(s_out);
  ui_settings.miles = true;
  ui_settings.tzZone = 3;            // Pacific, with DST (regional.cpp)
  ui_settings.setupDone = 1;
  history.begin();
  populate();
  nav.begin(&display, &theme);
  static const char* THEME_NAMES[] = {"squatch", "blocks", "hero", "aurora"};
  View* home = makeHomeView();       // the bottom of the stack, as on the pager: never popped
  nav.push(home);
  for (int t = 0; t < THEME_COUNT && t < 4; t++) {
    ui_settings.themeId = t;
    app::applyTheme();
    populateHistory();
    home->resume();
    char n[64];
    const char* tn = THEME_NAMES[t];
    run(600);
    snprintf(n, sizeof(n), "%s_home_messages", tn); shot(n);
    static const char* TILE[] = {"contacts", "map", "tools", "settings"};
    for (int i = 0; i < 4; i++) {
      home->rotate(1); run(900);
      snprintf(n, sizeof(n), "%s_home_%s", tn, TILE[i]); shot(n);
    }
    for (int i = 0; i < 4; i++) home->rotate(-1);
    run(900);

    View* lock = makeLockView();
    nav.push(lock);
    run(1200);
    snprintf(n, sizeof(n), "%s_lock", tn); shot(n);
    nav.pop();

    app::openChats(); run(300);
    snprintf(n, sizeof(n), "%s_chats", tn); shot(n);
    app::openThreadForChannel(1); run(300);
    snprintf(n, sizeof(n), "%s_thread_inw", tn); shot(n);
    clearTo(home);
    app::openThreadForContact(g_node->contacts[3].id.pub_key); run(300);
    snprintf(n, sizeof(n), "%s_thread_trailhead", tn); shot(n);
    clearTo(home);
  }
  return 0;
}
