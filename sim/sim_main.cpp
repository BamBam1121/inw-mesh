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
#include "touch.h"
#include "bootscreen.h"
#include "regions.h"
void startSetup();   // settings_ui.cpp
#if BOARD_HAS_TOUCH
#include "hwcheck.h"
#include "touch_gt911.h"
#include "rotary.h"
#include "keyboard.h"
extern Keyboard keyboard;
#endif

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
  // Positions round Spokane, in MeshCore's micro-degrees, so the map and the
  // distances have something to show. Basecamp is a favourite.
  auto at = [](ContactInfo& c, double lat, double lon) { c.gps_lat = (int32_t)(lat * 1e6); c.gps_lon = (int32_t)(lon * 1e6); };
  at(addContact("Mt Spokane RPT", ADV_TYPE_REPEATER, 1, 0, 120), 47.9217, -117.1133);
  at(addContact("Browne Mtn RPT", ADV_TYPE_REPEATER, 2, 1, 600), 47.6135, -117.3526);
  at(addContact("CDA Hill RPT", ADV_TYPE_REPEATER, 3, 2, 3000), 47.6905, -116.7640);
  at(addContact("Trailhead", ADV_TYPE_CHAT, 4, 1, 300), 47.8960, -117.1280);
  at(addContact("Ridge Runner", ADV_TYPE_CHAT, 5, 2, 900), 47.7512, -117.2050);
  ContactInfo& base = addContact("Basecamp", ADV_TYPE_CHAT, 6, 0, 60);
  at(base, 47.6720, -117.4080);
  base.flags |= 1;
  at(addContact("Silver Valley Room", ADV_TYPE_ROOM, 7, 2, 7200), 47.5410, -116.1210);
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

// ---- a finger, for touchscreen boards -------------------------------------------------------
static Gestures s_finger;
static void feedTouch(bool down, int x, int y) {
  TouchEvent e;
  while (s_finger.feed(down, x, y, millis(), e)) nav.touch(e);
}
static void tap(int x, int y) { feedTouch(true, x, y); run(60); feedTouch(false, x, y); run(500); }
static void doubleTap(int x, int y) {
  feedTouch(true, x, y); run(40); feedTouch(false, x, y); run(120);
  feedTouch(true, x, y); run(40); feedTouch(false, x, y); run(500);
}
static void swipe(int x0, int y0, int x1, int y1, int steps = 8) {
  feedTouch(true, x0, y0);
  for (int k = 1; k <= steps; k++) { sim::advance(16); feedTouch(true, x0 + (x1 - x0) * k / steps, y0 + (y1 - y0) * k / steps); }
  feedTouch(false, x1, y1);
  run(500);
}
static void drag(int x0, int y0, int x1, int y1) {   // slow: a scroll, not a flick
  feedTouch(true, x0, y0);
  for (int k = 1; k <= 20; k++) { sim::advance(40); feedTouch(true, x0 + (x1 - x0) * k / 20, y0 + (y1 - y0) * k / 20); }
  feedTouch(false, x1, y1);
  run(500);
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
  // The boot screen, half way through starting, and the power-off teardown.
  for (int t = 0; t < THEME_COUNT && t < 4; t++) {
    ui_settings.themeId = t;
    app::applyTheme();
    Canvas& g = nav.canvas();
    boot::drawLogo(g);
    g.fillRect(boot::BAR_X + 1, boot::BAR_Y + 1, (boot::BAR_W - 2) * 7 / 12, 3, theme.green);
    boot::drawMark(g, 0, 0, 900, true);
    char n[48];
    snprintf(n, sizeof(n), "%s_boot", THEME_NAMES[t]); savePPM(g, n);
    if (t == 0) {
      boot::drawGoodbye(g, 1200, -1); savePPM(g, "goodbye_saving");
      boot::drawGoodbye(g, 1500, 280); savePPM(g, "goodbye_teardown");
    }
  }
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
#if BOARD_HOME_DASHBOARD
    snprintf(n, sizeof(n), "%s_home", tn); shot(n);
    home->rotate(1); run(200);           // the trackball's highlight appears
    home->rotate(4); run(200);
    snprintf(n, sizeof(n), "%s_home_trackball", tn); shot(n);
    run(16000);                          // left alone, the highlight goes
#else
    snprintf(n, sizeof(n), "%s_home_messages", tn); shot(n);
    static const char* TILE[] = {"contacts", "map", "tools", "settings"};
    for (int i = 0; i < 4; i++) {
      home->rotate(1); run(900);
      snprintf(n, sizeof(n), "%s_home_%s", tn, TILE[i]); shot(n);
    }
    for (int i = 0; i < 4; i++) home->rotate(-1);
    run(900);
#endif

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

#if BOARD_HAS_TOUCH
  // A finger's tour, in the first theme: every step is a picture to check.
  ui_settings.themeId = 0;
  app::applyTheme();
  populateHistory();
  home = nullptr;
  clearTo(nullptr == home ? nav.top() : home);
  View* base = nav.top();
  nav.push(makeLockView()); run(600);
  shot("touch_0_locked");
  tap(160, 120);                       shot("touch_1_tap_says_swipe");
  swipe(160, 200, 160, 80);            shot("touch_2_swiped_up");
  tap(8 + 28, 200);                    shot("touch_3_tapped_chats");
  tap(160, 42 + 22);                   shot("touch_4_tapped_first_chat");
  drag(160, 90, 160, 190);             shot("touch_5_dragged_older");
  swipe(4, 120, 150, 120);             shot("touch_6_edge_swipe_back");
  tap(20, 30);                         shot("touch_7_header_back");
  tap(160, 62 + 3 + 34 + 17);          shot("touch_8_tapped_second_row");
  clearTo(base);

  // Tools > hardware check, part way through being tried: touch and keys done,
  // the ball rolled every way but right, not clicked yet.
  openHardwareCheck(); run(300);       shot("check_0_fresh");
  touchPanel.count = 14;
  keyboard.last = 'h';
  tdeck_tb::simCounts[0] = 4; tdeck_tb::simCounts[1] = 6; tdeck_tb::simCounts[2] = 3;
  tap(160, 120);                       shot("check_1_part_way");
  tdeck_tb::simCounts[3] = 2;
  nav.top()->press(); run(300);        shot("check_2_all_green");
  clearTo(base);

  // Tools > touch test: two rings tapped, then all four, then a unit whose
  // touch comes out mirrored left-right (the taps land on the far side).
  openTouchTest(); run(300);           shot("touchtest_0");
  drag(70, 110, 250, 170);
  tap(24, 42 + 24); tap(296, 42 + 24); shot("touchtest_1_two_rings");
  tap(296, 216); tap(24, 216);         shot("touchtest_2_lines_up");
  nav.top()->press(); run(100);
  tap(296, 42 + 24); tap(24, 42 + 24); tap(24, 216); tap(296, 216);
  shot("touchtest_3_mirrored_fixed");
  ui_settings.orient = 0;
  clearTo(base);

  // The dock's other screens, by finger: Settings, People, Map.
  app::openSettings(); run(300);          shot("dock_settings_0");
  drag(160, 210, 160, 90);                shot("dock_settings_1_dragged");
  clearTo(base);
  app::openContacts(); run(300);          shot("dock_people_0");
  tap(150, 42 + 12);                      shot("dock_people_1_sort_tapped");
  drag(160, 200, 160, 110);               shot("dock_people_2_dragged");
  clearTo(base);
  app::openMap(0, 0, nullptr); run(300);  shot("dock_map_0");
  tap(320 - 24, 18 + 30 + 42 + 18);       shot("dock_map_1_zoomed_out");
  drag(200, 140, 120, 110);               shot("dock_map_2_dragged");
  doubleTap(120, 150);                    shot("dock_map_3_double_tap");
  tap(8, 8);
  clearTo(base);

  // What's under the dock, by finger.
  app::openTools(); run(300);             shot("tools_0");
  // Discover: three answers already in, then the listening time runs out.
  for (int i = 0; i < 3; i++) {
    DiscoverHit& h = g_node->discovered[i];
    memcpy(h.pub, g_node->contacts[i].id.pub_key, 32);
    h.type = ADV_TYPE_REPEATER; h.theirSnr4 = 40 - 30 * i; h.ourSnr4 = 32 - 36 * i; h.rssi = -70 - 12 * i; h.at = millis();
  }
  g_node->discoveredCount = 3;
  tap(160, 42 + 3 * 28 + 14); run(13000); shot("tools_1_discover");
  clearTo(base);
  app::openSettings(); run(300);
  tap(8 + 149 + 6 + 70, 46 + 2 * 58 + 26); run(300);   shot("settings_wifi");
  tap(160, 42 + 2 * 28 + 14); run(300);                shot("settings_wifi_scan");
  clearTo(base);
  app::openSettings(); run(300);
  nav.top()->key('d'); nav.top()->press(); run(300);   shot("settings_display_0");
  drag(160, 220, 160, 60); drag(160, 220, 160, 60);    shot("settings_display_1_touch");
  clearTo(base);
  app::openContacts(); run(300);
  tap(160, 42 + 28 + 17); run(300);                    shot("contact_detail");
  clearTo(base);

  // Pop-ups over the home screen, and typing a password.
  nav.banner("Ridge Runner", "Count me in, bringing the T-Deck", 6000); run(400);   shot("overlay_banner");
  nav.toast("not a contact yet - it will be once it adverts", 6000); run(400);      shot("overlay_toast_long");
  run(7000);
  app::openSettings(); run(300);
  tap(8 + 149 + 6 + 70, 46 + 2 * 58 + 26); run(300);
  tap(160, 42 + 2 * 28 + 14); run(300);
  tap(160, 42 + 14); run(300);
  for (const char* k = "hunter2"; *k; k++) nav.top()->key(*k);
  run(1200);                                           shot("prompt_password");
  clearTo(base);

  // Region scopes: Settings > Radio & Mesh > region scope, ask the repeaters,
  // pick one; then a channel of its own, and its chat header.
  app::openSettings(); run(300);
  tap(8 + 149 + 6 + 70, 46 + 26); run(300);           // the Radio & Mesh tile
  drag(160, 220, 160, 80);                             // five rows down
  run(300);                                            shot("regions_0_radio_menu");
  tap(160, 42 + 4 * 28 + 14); run(300);                shot("regions_1_scope_menu");
  tap(160, 42 + 2 * 28 + 14); run(13000);              shot("regions_2_asked");
  tap(160, 42 + 28 + 14); run(400);                    shot("regions_3_picked");
  clearTo(base);
  // Per channel, from its chat as in the app: its title shows the region; tapping
  // the title opens Set Region Scope.
  regions::setForChannel(g_node->channels[2].channel.secret, "wa");
  app::openThreadForChannel(2); run(300);              shot("regions_4_channel_header");
  tap(260, 18 + 12); run(300);                         shot("regions_5_channel_scope");
  tap(160, 42 + 28 + 14); run(400);                    shot("regions_6_cleared");
  clearTo(base);
  regions::setForChannel(g_node->channels[2].channel.secret, "");
  regions::setDefault("");
  clearTo(base);

  // First-start setup, all three steps, keeping what's there each time.
  startSetup(); run(300);                              shot("setup_1_region");
  tap(160, 42 + 14); run(300);                         shot("setup_2_zone");
  tap(160, 42 + 14); run(300);                         shot("setup_3_units");
  clearTo(base);

  // Tools' text pages at this width: signal, then device info.
  app::openTools(); run(300);
  drag(160, 220, 160, 80); run(200);                   // five rows down
  tap(160, 42 + 3 * 28 + 14); run(600);                shot("text_signal");
  clearTo(base);
  app::openTools(); run(300);
  drag(160, 220, 160, 80); drag(160, 220, 160, 80); run(200);
  tap(160, 42 + 2 * 28 + 14); run(600);                shot("text_device");
  clearTo(base);
#endif
  return 0;
}
