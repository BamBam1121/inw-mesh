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
#include "battery.h"
#include "serve_net.h"
// LovyanGFX's PNG writer (utility/lgfx_miniz.c), declared only in that file.
extern "C" void* tdefl_write_image_to_png_file_in_memory_ex(const void* img, int w, int h, int chans, size_t* len, unsigned int level, int flip);
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
#if BOARD_HAS_TOUCH
  // "squatch_sim OUT serve PORT": the T-Deck live, in a browser. The screens run in
  // real time; the page shows each frame as a PNG and sends back touches, keys and
  // the trackball. Listens on 127.0.0.1 only (reach it through `tailscale serve`).
  if (argc > 2 && !strcmp(argv[2], "serve")) {
    const int port = argc > 3 ? atoi(argv[3]) : 8124;
    if (!net_listen(port)) { printf("can't listen on 127.0.0.1:%d\n", port); return 1; }
    printf("live on 127.0.0.1:%d\n", port);
    ui_settings.themeId = 3;                           // Aurora, as posted
    app::applyTheme();
    populateHistory();
    View* base = makeHomeView();
    nav.push(base);
    nav.push(makeLockView());                          // starts locked: swipe up
    static const char PAGE[] = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>T-Deck simulator</title><style>
body{margin:0;background:#0b0f1a;color:#cfd8ff;font:15px system-ui,sans-serif;display:flex;flex-direction:column;align-items:center}
h1{font-size:15px;font-weight:600;margin:10px 0 0;color:#8f9bd6}
#s{width:min(96vw,960px);image-rendering:pixelated;touch-action:none;display:block;margin-top:8px;border-radius:10px;box-shadow:0 0 0 2px #2a3150;background:#040716}
.row{display:flex;gap:8px;margin:6px;flex-wrap:wrap;justify-content:center}
button{background:#1b2340;color:#dfe6ff;border:1px solid #33406b;border-radius:10px;padding:11px 15px;font-size:17px;min-width:50px;touch-action:manipulation}
button:active{background:#2c3a6b}
input{font-size:17px;padding:10px;border-radius:10px;border:1px solid #33406b;background:#111830;color:#fff;width:58vw;max-width:520px}
p{color:#7f89b8;font-size:13px;margin:4px 12px;text-align:center;max-width:640px}
</style></head><body>
<h1>Squatch Mesh T-Deck simulator</h1>
<canvas id=s width=320 height=240></canvas>
<div class=row><button data-r=-1>&#9650;</button><button data-r=1>&#9660;</button><button data-p=1>&#9679; click</button><button data-k=8>&#9003; back</button></div>
<div class=row><input id=tx placeholder="type, then Enter" autocomplete=off autocapitalize=off><button id=go>&#9166;</button></div>
<div class=row><button data-u="/lock">lock</button><button data-u="/home">home</button><button data-u="/msg">new message</button><button data-u="/plug">charger</button><button data-u="/theme">theme</button></div>
<p>The screen is the touchscreen: tap, drag, swipe up to unlock. Poke the sasquatch. On a computer, arrow keys are the trackball and typing goes to the keyboard.</p>
<script>
const c=document.getElementById('s'),g=c.getContext('2d');
async function frame(){try{const r=await fetch('/f?'+Date.now());if(r.ok){const im=await createImageBitmap(await r.blob());g.drawImage(im,0,0);}}catch(e){}setTimeout(frame,30);}
frame();
// One request at a time, in order; a run of drags collapses to the latest.
const q=[];let sending=false;
function send(u,move){if(move&&q.length&&q[q.length-1].m)q[q.length-1]={u,m:true};else q.push({u,m:!!move});pump();}
async function pump(){if(sending)return;sending=true;while(q.length){const x=q.shift();try{await fetch(x.u);}catch(e){}}sending=false;}
function pos(e){const r=c.getBoundingClientRect();return[Math.max(0,Math.min(319,Math.round((e.clientX-r.left)*320/r.width))),Math.max(0,Math.min(239,Math.round((e.clientY-r.top)*240/r.height)))];}
let down=false;
c.addEventListener('pointerdown',e=>{down=true;c.setPointerCapture(e.pointerId);const[x,y]=pos(e);send(`/t?d=1&x=${x}&y=${y}`);e.preventDefault();});
c.addEventListener('pointermove',e=>{if(!down)return;const[x,y]=pos(e);send(`/t?d=1&x=${x}&y=${y}`,true);e.preventDefault();});
function up(e){if(!down)return;down=false;const[x,y]=pos(e);send(`/t?d=0&x=${x}&y=${y}`);}
c.addEventListener('pointerup',up);c.addEventListener('pointercancel',up);
document.querySelectorAll('button[data-r]').forEach(b=>b.onclick=()=>send('/b?r='+b.dataset.r));
document.querySelectorAll('button[data-p]').forEach(b=>b.onclick=()=>send('/b?p=1'));
document.querySelectorAll('button[data-k]').forEach(b=>b.onclick=()=>send('/k?c='+b.dataset.k));
document.querySelectorAll('button[data-u]').forEach(b=>b.onclick=()=>send(b.dataset.u));
const tx=document.getElementById('tx');
function typeOut(){for(const ch of tx.value)send('/k?c='+ch.charCodeAt(0));send('/k?c=13');tx.value='';}
document.getElementById('go').onclick=typeOut;
tx.addEventListener('keydown',e=>{if(e.key==='Enter'){typeOut();e.preventDefault();}});
document.addEventListener('keydown',e=>{if(e.target===tx)return;
 const m={ArrowUp:'/b?r=-1',ArrowLeft:'/b?r=-1',ArrowDown:'/b?r=1',ArrowRight:'/b?r=1',Enter:'/k?c=13',Backspace:'/k?c=8',Escape:'/k?c=8'};
 if(m[e.key]){send(m[e.key]);e.preventDefault();}else if(e.key.length===1&&!e.ctrlKey&&!e.metaKey){send('/k?c='+e.key.charCodeAt(0));e.preventDefault();}});
</script></body></html>)HTML";
    static const char* MSGS[][2] = {
        {"Ridge Runner", "Anyone up on Mt Spokane?"}, {"Trailhead", "Made it to the top!"},
        {"Pine Marten", "Browne Mtn repeater looks good"}, {"Basecamp", "Coffee's on"},
        {"Ridge Runner", "Heard you 3 hops out"}};
    int msgN = 0;
    auto arg = [](const char* req, const char* key) -> int {
      const char* p = strstr(req, key);
      return p ? atoi(p + strlen(key)) : 0;
    };
    uint32_t last = net_ms(), acc = 0;
    char req[2048];
    for (;;) {
      // Real time, in the same 16 ms steps the other runs use.
      const uint32_t now = net_ms();
      acc += now - last > 250 ? 250 : now - last;
      last = now;
      while (acc >= 16) { sim::advance(16); nav.tick(); if (nav.top()) nav.top()->tick(); acc -= 16; }
      const int fd = net_poll(8, req, sizeof(req));
      if (fd < 0) continue;
      const char* path = req + 4;                      // after "GET "
      if (!strncmp(req, "GET / ", 6)) { net_reply(fd, 200, "text/html; charset=utf-8", PAGE, (int)strlen(PAGE)); continue; }
      if (!strncmp(path, "/f", 2)) {                   // the screen, as the panel would show it
        View* v = nav.top();
        Canvas& g = nav.canvas();
        g.fillScreen(theme.bg);
        if (v && !v->isLock()) drawStatusBar(g, theme);
        if (v) v->draw(g);
        nav.drawOverlays(g);
        // RGB888 rows for LovyanGFX's own PNG encoder (lgfx_miniz.c); a BMP if it fails.
        const int w = g.width(), h = g.height();
        static uint8_t* rgb = nullptr;
        if (!rgb) rgb = (uint8_t*)malloc((size_t)w * h * 3);
        for (int y = 0; y < h; y++)
          for (int x = 0; x < w; x++) {
            const uint16_t px = g.readPixel(x, y);
            uint8_t* o = rgb + ((size_t)y * w + x) * 3;
            o[0] = ((px >> 11) & 31) * 255 / 31; o[1] = ((px >> 5) & 63) * 255 / 63; o[2] = (px & 31) * 255 / 31;
          }
        size_t len = 0;
        void* png = tdefl_write_image_to_png_file_in_memory_ex(rgb, w, h, 3, &len, 3, 0);
        if (png) { net_reply(fd, 200, "image/png", png, (int)len); free(png); continue; }
        static uint8_t* bmp = nullptr;
        const int row = w * 3, size = 54 + row * h;
        if (!bmp) bmp = (uint8_t*)malloc(size);
        memset(bmp, 0, 54);
        bmp[0] = 'B'; bmp[1] = 'M'; memcpy(bmp + 2, &size, 4); bmp[10] = 54; bmp[14] = 40;
        memcpy(bmp + 18, &w, 4); const int nh = -h; memcpy(bmp + 22, &nh, 4); bmp[26] = 1; bmp[28] = 24;
        for (int y = 0; y < h; y++)
          for (int x = 0; x < w; x++) {
            const uint8_t* i = rgb + ((size_t)y * w + x) * 3; uint8_t* o = bmp + 54 + y * row + x * 3;
            o[0] = i[2]; o[1] = i[1]; o[2] = i[0];
          }
        net_reply(fd, 200, "image/bmp", bmp, size);
        continue;
      }
      if (!strncmp(path, "/t?", 3)) {                  // a finger: d=1 down or moving, d=0 lifted
        feedTouch(arg(path, "d=") != 0, arg(path, "x="), arg(path, "y="));
      } else if (!strncmp(path, "/k?", 3)) {           // a key: 8 backspace, 13 enter
        const int k = arg(path, "c=");
        View* v = nav.top();
        if (k == 8) nav.backspace();
        else if (k == 13 || k == 10) {
          if (v && !v->wantsAllKeys() && !v->isHome() && !v->isLock()) nav.press();
          else nav.key('\n');
        } else if (k >= 32 && k < 127) nav.key((char)k);
      } else if (!strncmp(path, "/b?", 3)) {           // the trackball: r=+-1 rolls, p=1 clicks
        if (strstr(path, "p=1")) nav.press();
        else nav.rotate(arg(path, "r=") < 0 ? -1 : 1);
      } else if (!strncmp(path, "/lock", 5)) {
        if (!(nav.top() && nav.top()->isLock())) { sim::advance(60000); nav.push(makeLockView()); }
      } else if (!strncmp(path, "/home", 5)) {
        clearTo(base);
      } else if (!strncmp(path, "/msg", 4)) {          // a new message from someone
        const auto& m = MSGS[msgN++ % 5];
        const ConvKey k = ConvKey::contact(g_node->contacts[3 + msgN % 2].id.pub_key);
        history.add(k, 0, ST_RECV, m[0], m[1], app::now(), 1, 30);
        nav.banner(m[0], m[1], 4000);
      } else if (!strncmp(path, "/plug", 5)) {
        battery.plugged = !battery.plugged;
      } else if (!strncmp(path, "/theme", 6)) {
        ui_settings.themeId = (ui_settings.themeId + 1) % (THEME_COUNT < 4 ? THEME_COUNT : 4);
        app::applyTheme();
      } else { net_reply(fd, 404, "text/plain", "", 0); continue; }
      net_reply(fd, 204, "text/plain", "", 0);
    }
  }
  // "squatch_sim OUT promo": pictures of the sasquatch for posting - Aurora, each
  // moment as a still (st_*), and one run recorded at 15 frames a second (gif_NNN).
  if (argc > 2 && !strcmp(argv[2], "promo")) {
    ui_settings.themeId = 3;                          // Aurora
    app::applyTheme();
    populateHistory();
    {                                                 // all read: his eyes go amber only for the new one
      ConvKey keys[64];
      const uint16_t n = history.conversations(keys, 64);
      for (uint16_t i = 0; i < n; i++) history.markRead(keys[i]);
    }
    nav.push(makeHomeView());
    int frame = 0;
    auto rec = [&](uint32_t ms) {                     // record ms of it, a frame every 66 ms
      for (uint32_t t = 0; t < ms; t += 66) { run(66); char n[32]; snprintf(n, sizeof(n), "gif_%03d", frame++); shot(n); }
    };
    auto finger = [&](int x, int y) {                 // a tap, recorded
      feedTouch(true, x, y); rec(66); feedTouch(false, x, y);
    };
    const uint32_t morning = sim::epoch;              // 9:41 am
    // The recorded run: he wakes and waves hello, walks a little, gets poked, then
    // poked three times and sees stars.
    sim::advance(20UL * 60UL * 1000UL);
    nav.push(makeLockView());
    rec(5200);
    rec(1500);
    finger(255, 125); rec(3000);
    rec(5600);                                        // past the 8 s window: the next three count afresh
    finger(255, 125); rec(400); finger(255, 125); rec(400); finger(255, 125); rec(3600);
    nav.pop();
    // Stills, each from a fresh wake so he has something to say.
    auto wake = [&]() { sim::advance(20UL * 60UL * 1000UL); nav.push(makeLockView()); };
    wake(); run(1300); shot("st_1_morning_wave"); run(8000);
    run(1000); tap(255, 125); run(200); shot("st_2_poke");
    run(9000); tap(255, 125); tap(255, 125); tap(255, 125); run(600); shot("st_3_dizzy");
    run(9000);
    const ConvKey trail = ConvKey::contact(g_node->contacts[3].id.pub_key);
    history.add(trail, 0, ST_RECV, "Trailhead", "Made it to the top!", app::now(), 1, 30);
    run(1100); shot("st_4_message"); run(9000);
    battery.plugged = true; run(2300); shot("st_5_charger"); run(9000);
    nav.pop();
    battery.plugged = false;
    history.markRead(trail);
    sim::epoch = morning + 14UL * 3600UL;             // 11:41 pm
    wake(); run(700); shot("st_6_late_yawn"); run(1500); shot("st_7_late_said"); run(8000);
    nav.pop();
    sim::epoch = morning + 5UL * 3600UL;              // 2:41 pm, and nearly flat
    battery.pct = 12;
    wake(); run(1500); shot("st_8_low_battery");
    nav.pop();
    battery.pct = 87;
    printf("%d gif frames\n", frame);
    return 0;
  }
  // "squatch_sim OUT squatch": the lock face's sasquatch only - hello on waking, a
  // poke, three pokes (stars), a tap somewhere else - in each theme.
  if (argc > 2 && !strcmp(argv[2], "squatch")) {
    nav.push(makeHomeView());
    for (int t = 0; t < THEME_COUNT && t < 4; t++) {
      ui_settings.themeId = t;
      app::applyTheme();
      const char* tn = THEME_NAMES[t];
      char n[64];
      sim::advance(20UL * 60UL * 1000UL);            // dark a good while: he says hello
      nav.push(makeLockView());
      run(1100);  snprintf(n, sizeof(n), "sq_%s_1_hello", tn); shot(n);
      run(5000);  snprintf(n, sizeof(n), "sq_%s_2_walking", tn); shot(n);
      tap(255, 125); snprintf(n, sizeof(n), "sq_%s_3_poke", tn); shot(n);
      run(900);   snprintf(n, sizeof(n), "sq_%s_4_poke_said", tn); shot(n);
      tap(255, 125); tap(255, 125);
      run(300);   snprintf(n, sizeof(n), "sq_%s_5_dizzy", tn); shot(n);
      run(6000);
      tap(60, 110); snprintf(n, sizeof(n), "sq_%s_6_tap_elsewhere", tn); shot(n);
      run(4000);
      nav.pop();
    }
    return 0;
  }
#endif
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
  ui_settings.themeId = (argc > 2 && !strcmp(argv[2], "aurora")) ? 3 : 0;   // "squatch_sim OUT aurora": the tour in Aurora
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
