// Slippy map from SD tiles (/tiles/{z}/{x}/{y}.jpg|png, /tiles/topo for the
// second layer) with positioned contacts on top.
//
// Keys: wheel zooms, wasd/ijkl pan, n/b step through nodes, c centres on you,
// m toggles labels, t toggles topo.

#include "app.h"
#include "fieldtools.h"
#include "node.h"
#include "dataio.h"
#include "netwifi.h"
#include "fx.h"
#include <SD.h>
#include <math.h>
#include <algorithm>
#include <esp_heap_caps.h>

namespace {

constexpr int TILE = 256;
constexpr int MAP_Y = L::STATUS_H;           // map fills everything under the status bar
constexpr int MAP_H = L::H - L::STATUS_H;

double lon2x(double lon, int z) { return (lon + 180.0) / 360.0 * (1 << z) * TILE; }
double lat2y(double lat, int z) {
  const double r = lat * M_PI / 180.0;
  return (1.0 - log(tan(r) + 1.0 / cos(r)) / M_PI) / 2.0 * (1 << z) * TILE;
}
double x2lon(double x, int z) { return x / ((1 << z) * TILE) * 360.0 - 180.0; }
double y2lat(double y, int z) {
  const double n = M_PI - 2.0 * M_PI * y / ((1 << z) * TILE);
  return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

struct TileSlot {
  int z = -1, x = 0, y = 0;
  uint8_t layer = 0;
  bool missing = false;
  uint32_t used = 0;
  lgfx::LGFX_Sprite* spr = nullptr;
};

// The card, for the map: tried at most every 20 s while it isn't there. Each try is
// a whole SD.begin - allocations, a file system registration, SPI traffic on the bus
// the screen and radio share - and with no card the map used to make one for every
// tile it looked at and every tile that came in, inside the draw.
static bool mapCard() {
  static uint32_t triedAt = 0;
  static bool tried = false;
  if (sdMounted()) return true;
  if (tried && millis() - triedAt < 20000) return false;
  tried = true;
  triedAt = millis();
  return sdMount();
}

class TileCache {
public:
  static constexpr int N = 24;            // ~3 MB of PSRAM: the level shown, the one out, and a margin
  TileSlot slots[N];
  uint8_t* fileBuf = nullptr;
  static constexpr size_t FILE_MAX = 96 * 1024;

  ~TileCache() {
    for (auto& s : slots) if (s.spr) { s.spr->deleteSprite(); delete s.spr; }
    free(fileBuf);
  }

  // Returns the sprite, or null when there is no tile for this spot.
  lgfx::LGFX_Sprite* get(int z, int x, int y, uint8_t layer) {
    const int n = 1 << z;
    if (y < 0 || y >= n) return nullptr;
    x = ((x % n) + n) % n;
    TileSlot* victim = &slots[0];
    for (auto& s : slots) {
      if (s.z == z && s.x == x && s.y == y && s.layer == layer) {
        s.used = millis();
        return s.missing ? nullptr : s.spr;
      }
      if (s.used < victim->used) victim = &s;
    }
    victim->z = z; victim->x = x; victim->y = y; victim->layer = layer;
    victim->used = millis();
    victim->missing = !load(*victim);
    if (victim->missing && layer == 0) wifi::requestTile((uint8_t)z, x, y);
    return victim->missing ? nullptr : victim->spr;
  }

  // What is known of a tile without reading anything: 0 not tried yet, 1 in memory,
  // 2 there is none (not on the card, not downloadable now).
  int state(int z, int x, int y, uint8_t layer) const {
    const int n = 1 << z;
    if (y < 0 || y >= n) return 2;
    x = ((x % n) + n) % n;
    for (auto& s : slots) if (s.z == z && s.x == x && s.y == y && s.layer == layer) return s.missing ? 2 : 1;
    return 0;
  }
  // The tile if it is in memory; never reads the card.
  lgfx::LGFX_Sprite* peek(int z, int x, int y, uint8_t layer) {
    const int n = 1 << z;
    if (y < 0 || y >= n) return nullptr;
    x = ((x % n) + n) % n;
    for (auto& s : slots)
      if (s.z == z && s.x == x && s.y == y && s.layer == layer) { s.used = millis(); return s.missing ? nullptr : s.spr; }
    return nullptr;
  }

  // No card: a downloaded tile goes straight into a slot, decoded, and shows from there.
  bool put(int z, int x, int y, const uint8_t* data, size_t len, bool png) {
    TileSlot* victim = &slots[0];
    for (auto& s : slots) {
      if (s.z == z && s.x == x && s.y == y && s.layer == 0) { victim = &s; break; }
      if (s.used < victim->used) victim = &s;
    }
    if (!victim->spr) {
      victim->spr = new lgfx::LGFX_Sprite();
      victim->spr->setPsram(true);
      victim->spr->setColorDepth(16);
      if (!victim->spr->createSprite(TILE, TILE)) { delete victim->spr; victim->spr = nullptr; return false; }
    }
    victim->spr->fillScreen(0);
    const bool ok = png ? victim->spr->drawPng(data, len, 0, 0) : victim->spr->drawJpg(data, len, 0, 0);
    victim->z = z; victim->x = x; victim->y = y; victim->layer = 0;
    victim->used = millis();
    victim->missing = !ok;
    return ok;
  }

  // A tile just landed on the card: forget that we thought it was missing.
  void forget(int z, int x, int y) {
    for (auto& s : slots) if (s.z == z && s.x == x && s.y == y && s.layer == 0) { s.z = -1; s.used = 0; }
  }

private:
  bool load(TileSlot& s) {
    if (!mapCard()) return false;
    if (!fileBuf) fileBuf = (uint8_t*)heap_caps_malloc(FILE_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fileBuf) return false;
    char path[64];
    const char* base = s.layer ? "/tiles/topo" : "/tiles";
    bool png = false;
    snprintf(path, sizeof(path), "%s/%d/%d/%d.jpg", base, s.z, s.x, s.y);
    if (!SD.exists(path)) {
      snprintf(path, sizeof(path), "%s/%d/%d/%d.png", base, s.z, s.x, s.y);
      if (!SD.exists(path)) return false;
      png = true;
    }
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    const size_t len = f.size();
    if (!len || len > FILE_MAX) { f.close(); return false; }
    const size_t got = f.read(fileBuf, len);
    f.close();
    if (got != len) return false;
    if (!s.spr) {
      s.spr = new lgfx::LGFX_Sprite();
      s.spr->setPsram(true);
      s.spr->setColorDepth(16);
      if (!s.spr->createSprite(TILE, TILE)) { delete s.spr; s.spr = nullptr; return false; }
    }
    s.spr->fillScreen(0);
    return png ? s.spr->drawPng(fileBuf, len, 0, 0) : s.spr->drawJpg(fileBuf, len, 0, 0);
  }
};

struct MapNode {
  uint8_t pub[32];
  char name[32];
  uint8_t type;
  double lat, lon;
  float dist;
};

// What the map shows of the nodes (GitHub #10): every node with names, every node as
// dots only, the favourites, or none. Kept as two settings (which nodes, and names on
// or off) that Settings > Display shows separately; the map's own switch steps
// through these four.
enum NodeShow : uint8_t { SHOW_ALL, SHOW_DOTS, SHOW_FAVS, SHOW_NONE };
static NodeShow nodeShow() {
  if (ui_settings.mapNodes >= 2) return SHOW_NONE;
  if (ui_settings.mapNodes == 1) return SHOW_FAVS;
  return ui_settings.mapLabels ? SHOW_ALL : SHOW_DOTS;
}
static void setNodeShow(NodeShow s) {
  ui_settings.mapNodes = s == SHOW_NONE ? 2 : s == SHOW_FAVS ? 1 : 0;
  if (s != SHOW_NONE) ui_settings.mapLabels = s != SHOW_DOTS;
}

} // namespace

class MapView : public View {
public:
  MapView(double lat, double lon, const char* focus) {
    _cache = new TileCache();
    _z = constrain((int)ui_settings.mapZoom, 3, 17);
    double mla, mlo;
    if (lat != 0 || lon != 0) { _lat = lat; _lon = lon; }
    else if (app::myPosition(mla, mlo)) { _lat = mla; _lon = mlo; }
    else { _lat = ui_settings.mapLat; _lon = ui_settings.mapLon; }
    // Opened on one node ("show on map"): that one is on the map whatever is hidden.
    if (focus) strlcpy(_pin, focus, sizeof(_pin));
    collectNodes();
    if (focus) {
      for (int i = 0; i < (int)_nodes.size(); i++) if (!strcmp(_nodes[i].name, focus)) _focus = i;
    }
  }
  ~MapView() override {
    ui_settings.mapZoom = _z;
    ui_settings.mapLat = _lat;
    ui_settings.mapLon = _lon;
    ui_settings.save();
    delete _cache;
  }
  bool wantsAllKeys() override { return true; }

  void rotate(int d) override { _z = constrain(_z + (d > 0 ? 1 : -1), 3, 17); dirty = true; }

  void key(char c) override {
    const double step = 0.25;
    double px = lon2x(_lon, _z), py = lat2y(_lat, _z);
    switch (tolower(c)) {
      case 'w': case 'i': py -= MAP_H * step; break;
      case 's': case 'k': py += MAP_H * step; break;
      case 'a': case 'j': px -= L::W * step; break;
      case 'd': case 'l': px += L::W * step; break;
      case 'c': {
        double la, lo;
        if (app::myPosition(la, lo)) { _lat = la; _lon = lo; _focus = -1; }
        else nav.toast("no gps fix yet");
        dirty = true;
        return;
      }
      case 'n': jump(1); return;
      case 'b': jump(-1); return;
      case 'm': stepNodeShow(); return;
      case 't': _layer = !_layer; dirty = true; return;
      case '=': case '+': rotate(1); return;
      case '-': rotate(-1); return;
      case '\n': press(); return;
      default: return;
    }
    _lon = x2lon(px, _z);
    _lat = constrain(y2lat(py, _z), -85.0, 85.0);
    dirty = true;
  }

  void press() override {
    if (_focus >= 0 && _focus < (int)_nodes.size()) app::openContactDetail(_nodes[_focus].pub);
    else key('c');
  }

  // A finger: drag to move the map, the buttons on the right to zoom and find
  // yourself, tap a node to pick it and its label to open it. The "<" at the top
  // left goes back (Nav's header rule: the map has no header of its own).
  bool coasts() override { return false; }
  bool touch(const TouchEvent& e) override {
    switch (e.type) {
      case TouchEvent::Drag: {
        if (buttonAt(e.x0, e.y0) >= 0) return false;   // a press that began on a button isn't a pan
        const double px = lon2x(_lon, _z) - e.dx, py = lat2y(_lat, _z) - e.dy;
        const double world = (double)(1 << _z) * TILE;
        _lon = x2lon(fmod(fmod(px, world) + world, world), _z);
        _lat = constrain(y2lat(py, _z), -85.0, 85.0);
        return true;
      }
      case TouchEvent::Tap: {
        switch (buttonAt(e.x, e.y)) {
          case 0: rotate(1); return true;
          case 1: rotate(-1); return true;
          case 2: key('c'); return true;
          default: break;
        }
        // The node count at the top right switches what's shown of the nodes.
        if (e.x >= _cntX - 6 && e.y < MAP_Y + 28) { stepNodeShow(); return true; }
        if (_focus >= 0 && e.x >= _pillX && e.y >= L::H - 26) { press(); return true; }
        // A second tap on the same spot, quickly: zoom in there (one finger is all
        // the touch chip reports, so no pinch).
        const uint32_t now = millis();
        if (_lastTap && now - _lastTap < 400 && abs(e.x - _tapX) < 30 && abs(e.y - _tapY) < 30) {
          const double left = lon2x(_lon, _z) - L::W / 2.0, top = lat2y(_lat, _z) - MAP_H / 2.0;
          _lon = x2lon(left + e.x, _z);
          _lat = constrain(y2lat(top + (e.y - MAP_Y), _z), -85.0, 85.0);
          _lastTap = 0;
          rotate(1);
          return true;
        }
        // Several nodes under the finger (sharing a spot, or close together): another
        // tap on the same place, slower than a double tap, steps on to the next of them.
        const bool again = _lastTap && abs(e.x - _tapX) < 30 && abs(e.y - _tapY) < 30;
        _lastTap = now; _tapX = e.x; _tapY = e.y;
        int cand[STACK_MAX];
        const int nc = nodesAt(e.x, e.y, cand);
        int pick = nc ? cand[0] : -1;    // nothing there: let go of the one picked
        if (again && nc > 1)
          for (int i = 0; i < nc; i++) if (cand[i] == _focus) { pick = cand[(i + 1) % nc]; break; }
        _focus = pick;
        return true;
      }
      case TouchEvent::Down: _held = true; return false;      // no card reads while a finger is down
      case TouchEvent::Up:   _held = false; dirty = _pending || dirty; return false;
      case TouchEvent::Pinch: {
        _held = false;
        if (!_pinch) beginPinch(e.x0, e.y0);
        // No further than the zoom levels there are (3 to 17).
        _pscale = constrain(e.scale, ldexpf(1.0f, 3 - _pz0), ldexpf(1.0f, 17 - _pz0));
        _pcx = e.x; _pcy = e.y;
        dirty = true;
        return true;
      }
      case TouchEvent::PinchEnd:
        if (_pinch) endPinch();
        return true;
      default: return false;
    }
  }

  // A pinch begins: keep the picture on the screen, and the place under the point
  // between the fingers.
  void beginPinch(int cx, int cy) {
    _pinch = true;
    _pz0 = _z;
    _pcx0 = _pcx = cx; _pcy0 = _pcy = cy;
    _pscale = 1.0f;
    const double left = lon2x(_lon, _z) - L::W / 2.0, top = lat2y(_lat, _z) - MAP_H / 2.0;
    _plon = x2lon(left + cx, _z);
    _plat = y2lat(top + (cy - MAP_Y), _z);
  }
  // The fingers lift: the nearest zoom level to how far they spread, with the place
  // that was between them where the point between them is now.
  void endPinch() {
    _pinch = false;
    const int z1 = constrain(_pz0 + (int)lroundf(log2f(_pscale)), 3, 17);
    const double left = lon2x(_plon, z1) - _pcx, top = lat2y(_plat, z1) - (_pcy - MAP_Y);
    const double world = (double)(1 << z1) * TILE, px = left + L::W / 2.0;
    _z = z1;
    _lon = x2lon(fmod(fmod(px, world) + world, world), z1);
    _lat = constrain(y2lat(top + MAP_H / 2.0, z1), -85.0, 85.0);
    ui_settings.mapZoom = _z;
    dirty = true;
  }

  void tick() override {
    // Finished downloads are written HERE, on the loop task: the SD card shares
    // the SPI bus with the panel and radio, so no other task may touch it.
    wifi::TileDone d;
    while (wifi::pollTile(d)) {
      if (mapCard()) {
        char dir[40], path[64];
        snprintf(dir, sizeof(dir), "/tiles/%u", d.z);
        if (!SD.exists("/tiles")) SD.mkdir("/tiles");
        if (!SD.exists(dir)) SD.mkdir(dir);
        snprintf(dir, sizeof(dir), "/tiles/%u/%ld", d.z, (long)d.x);
        if (!SD.exists(dir)) SD.mkdir(dir);
        snprintf(path, sizeof(path), "%s/%ld.%s", dir, (long)d.y, d.png ? "png" : "jpg");
        File f = SD.open(path, FILE_WRITE);
        if (f) { f.write(d.data, d.len); f.close(); }
        _cache->forget(d.z, d.x, d.y);
        dirty = true;
      } else if (_cache->put(d.z, d.x, d.y, d.data, d.len, d.png)) {
        dirty = true;                     // no card: shown from memory until it's pushed out
      }
      free(d.data);
    }
    if (g_node && g_node->contactsGen() != _gen && millis() - _collected > 5000) { collectNodes(); dirty = true; }
    if (millis() - _last > 5000) { _last = millis(); dirty = true; }   // my position moves
    if (_pending && !_held && !_pinch) dirty = true;    // the next tile of the view, next frame
    if (dirty || _held || _pinch) { _stillAt = millis(); return; }
    // Still for half a second: read one tile of the level out around the view, so a
    // pinch out has real map to show straight away. One a pass, never with a finger down.
    if (millis() - _stillAt < 500 || _z <= 3) return;
    const Proj p = proj();
    // One level out and then two: what the screen would show pinched out 2x and 4x.
    const double cx = p.ox + L::W / 2.0, cy = p.oy + MAP_H / 2.0;
    for (int k = 1; k <= 2 && _z - k >= 3; k++) {
      const int lv = _z - k;
      const double m = (double)(1 << k), mx = cx / m, my = cy / m;
      for (int ty = (int)floor((my - MAP_H / 2.0) / TILE); ty <= (int)floor((my + MAP_H / 2.0) / TILE); ty++)
        for (int tx = (int)floor((mx - L::W / 2.0) / TILE); tx <= (int)floor((mx + L::W / 2.0) / TILE); tx++)
          if (_cache->state(lv, tx, ty, _layer) == 0) { _cache->get(lv, tx, ty, _layer); return; }
    }
  }

  // Where things go on the screen: world pixels at zoom level z, scaled by k (1, except
  // mid-pinch): screen x = (wx - ox) * k, screen y = (wy - oy) * k + MAP_Y. Drawing, the
  // labels and what a tap hits all use this, so they agree in every state.
  struct Proj { int z; double k, ox, oy; };
  Proj proj() const {
    if (_pinch) {
      const float zf = _pz0 + log2f(_pscale);
      const int zb = constrain((int)lroundf(zf), 3, 17);
      const double k = _pscale * ldexp(1.0, _pz0 - zb);
      return {zb, k, lon2x(_plon, zb) - _pcx / k, lat2y(_plat, zb) - (_pcy - MAP_Y) / k};
    }
    return {_z, 1.0, lon2x(_lon, _z) - L::W / 2.0, lat2y(_lat, _z) - MAP_H / 2.0};
  }
  static int scrX(const Proj& p, double lon) { return (int)lround((lon2x(lon, p.z) - p.ox) * p.k); }
  static int scrY(const Proj& p, double lat) { return (int)lround((lat2y(lat, p.z) - p.oy) * p.k) + MAP_Y; }

  // The tiles of level lv that the screen covers, (f = how many level-p.z pixels one of
  // its pixels is). fn(tx, ty, screen x, screen y, scale).
  template <typename F> void eachTile(const Proj& p, int lv, F fn) const {
    const double f = ldexp(1.0, p.z - lv), sc = p.k * f;
    const double wx0 = p.ox / f, wy0 = p.oy / f, wx1 = (p.ox + L::W / p.k) / f, wy1 = (p.oy + MAP_H / p.k) / f;
    for (int ty = (int)floor(wy0 / TILE); ty <= (int)floor(wy1 / TILE); ty++)
      for (int tx = (int)floor(wx0 / TILE); tx <= (int)floor(wx1 / TILE); tx++)
        fn(tx, ty, (tx * TILE * f - p.ox) * p.k, (ty * TILE * f - p.oy) * p.k + MAP_Y, sc);
  }

  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    const Proj p = proj();
    const double left = p.ox, top = p.oy;      // (level p.z, unscaled: what the rest used)
    g.setClipRect(0, MAP_Y, L::W, MAP_H);
    g.fillRect(0, MAP_Y, L::W, MAP_H, t.panel);
    // Tiles. Where the level shown has nothing in memory yet, the level out is drawn
    // under it, twice the size, so there is always a map rather than a blank. Nothing is
    // read from the card while a finger is on the glass or a pinch is under way (a tile
    // takes ~0.1 s to read and decode): the map follows the finger at the full frame
    // rate, then fills in a tile a frame once it is let go (tick() asks for the frames).
    // Mid-pinch the level the pinch began at is drawn as well - its tiles are the ones
    // in memory - finer ones over coarser ones.
    bool complete = true;
    eachTile(p, p.z, [&](int tx, int ty, double, double, double) { if (_cache->state(p.z, tx, ty, _layer) != 1) complete = false; });
    int lv[16], nl = 0;
    auto addLevel = [&](int v) { if (v < 3 || v > 17) return; for (int i = 0; i < nl; i++) if (lv[i] == v) return; lv[nl++] = v; };
    if (!complete) addLevel(p.z - 1);
    addLevel(p.z);
    if (_pinch)                          // and every level between it and where the pinch began
      for (int v = min(p.z, _pz0); v <= max(p.z, _pz0); v++) addLevel(v);
    std::sort(lv, lv + nl);
    int budget = (_pinch || _held) ? 0 : 1, drawn = 0;
    bool waiting = false;
    for (int i = 0; i < nl; i++) {
      eachTile(p, lv[i], [&](int tx, int ty, double dx, double dy, double sc) {
        lgfx::LGFX_Sprite* s = nullptr;
        const int st = _cache->state(lv[i], tx, ty, _layer);
        if (st == 1) s = _cache->peek(lv[i], tx, ty, _layer);
        else if (st == 0 && lv[i] == p.z) {
          if (budget > 0) { budget--; s = _cache->get(lv[i], tx, ty, _layer); }
          else waiting = true;
        }
        if (!s) return;
        if (sc == 1.0) s->pushSprite(&g, (int)lround(dx), (int)lround(dy));
        else {
          const float z = (float)(sc + 1.0 / TILE);     // a hair over, so no seams between tiles
          s->setPivot(0, 0);
          s->pushRotateZoom(&g, (float)dx, (float)dy, 0.0f, z, z);
        }
        drawn++;
      });
    }
    _pending = waiting;
    const bool any = drawn > 0;
    if (!any && !_pinch) {
      // Nothing at all: a faint grid so panning still reads as movement.
      const int gx0 = (int)(64 - fmod(left, 64.0)), gy0 = (int)(64 - fmod(top, 64.0));
      for (int x = gx0 % 64; x < L::W; x += 64) g.drawFastVLine(x, MAP_Y, MAP_H, t.line);
      for (int y = gy0 % 64; y < MAP_H; y += 64) g.drawFastHLine(0, MAP_Y + y, L::W, t.line);
    }
    // Nodes. Their names are tap targets as well as their dots (_labels, nodesAt).
    const float zShown = _pinch ? _pz0 + log2f(_pscale) : (float)_z;
    if (!_pinch) _labels.clear();
    g.setFont(&fonts::Font2);
    for (int i = 0; i < (int)_nodes.size(); i++) {
      const MapNode& n = _nodes[i];
      const int x = scrX(p, n.lon), y = scrY(p, n.lat);
      if (x < -20 || x > L::W + 20 || y < MAP_Y - 20 || y > L::H + 20) continue;
      const bool f = i == _focus;
      const uint16_t col = n.type == ADV_TYPE_REPEATER ? t.green : n.type == ADV_TYPE_ROOM ? t.amber : t.blue;
      if (n.type == ADV_TYPE_REPEATER) { g.fillTriangle(x, y - 6, x - 5, y + 4, x + 5, y + 4, col); g.drawTriangle(x, y - 7, x - 6, y + 5, x + 6, y + 5, t.bg); }
      else { g.fillCircle(x, y, 4, col); g.drawCircle(x, y, 5, t.bg); }
      if (f) g.drawCircle(x, y, 9, t.white);
      if ((ui_settings.mapLabels && zShown >= 10.5f) || f) {
        char nm[40];
        sanitize(n.name, nm, sizeof(nm) - 4);
        richFit(g, nm, 130);
        const int w = richWidth(g, nm) + 6;
        g.fillRoundRect(x + 8, y - 8, w, 16, 3, f ? t.greenDim : t.bg);
        g.setTextColor(f ? t.white : t.txt, f ? t.greenDim : t.bg);
        drawRich(g, nm, x + 11, y - 8);
        if (!_pinch) _labels.push_back({i, (int16_t)(x + 8), (int16_t)(y - 8), (int16_t)w, (int16_t)16});
      }
    }
    // Breadcrumb trail (field tools): the path walked, and a flag at the start.
    size_t tn = 0;
    const field::TrailPt* tp = field::trail(tn);
    if (tn) {
      int lx = 0, ly = 0;
      for (size_t i = 0; i < tn; i++) {
        const int x = scrX(p, tp[i].lon), y = scrY(p, tp[i].lat);
        if (i) { g.drawLine(lx, ly, x, y, t.amber); g.drawLine(lx, ly + 1, x, y + 1, t.amber); }
        lx = x; ly = y;
      }
      const int sx = scrX(p, tp[0].lon), sy = scrY(p, tp[0].lat);
      g.drawFastVLine(sx, sy - 14, 14, t.white);
      g.fillTriangle(sx, sy - 14, sx, sy - 7, sx + 9, sy - 10, t.green);
      g.fillCircle(sx, sy, 3, t.green);
    }
    // Me
    double la, lo;
    if (app::myPosition(la, lo)) {
      const int x = scrX(p, lo), y = scrY(p, la);
      g.fillCircle(x, y, 7, t.white);
      g.fillCircle(x, y, 5, t.blue);
    }
    g.clearClipRect();

    // Chrome: zoom chip, scale bar, focus panel. Full size and sharp in every state.
    char z[40];
    if (wifi::connected() && ui_settings.tileFetch && !_layer)
      snprintf(z, sizeof(z), TOUCH ? "z%d  wifi %u" : "z%d  wifi %u tiles", p.z, wifi::tilesFetched());
    else if (!any && !wifi::connected() && !sdMounted())
      // Nothing to draw streets from: say where they come from.
      snprintf(z, sizeof(z), "z%d  no tiles: wi-fi or sd", p.z);
    else
      snprintf(z, sizeof(z), "z%d%s%s", p.z, _layer ? " topo" : "", any ? "" : TOUCH ? "  no tiles" : "  no tiles here");
    int zx = 6;
    if (TOUCH) {
      // Back, where Nav looks for a tap on a header's "<".
      drawPill(g, 6, MAP_Y + 4, 30, 18, t.panel, t.green, "<");
      zx = 68;
      for (int b = 0; b < 3; b++) {
        const int bx = L::W - BTN - 6, by = BTN_Y + b * (BTN + 6);
        g.fillRoundRect(bx, by, BTN, BTN, 8, t.panel);
        g.drawRoundRect(bx, by, BTN, BTN, 8, t.line);
        const int mx = bx + BTN / 2, my = by + BTN / 2;
        if (b < 2) g.fillRect(mx - 8, my - 1, 17, 3, t.txt);             // minus, and the plus's bar
        if (b == 0) g.fillRect(mx - 1, my - 8, 3, 17, t.txt);
        if (b == 2) { g.drawCircle(mx, my, 7, t.txt); g.fillCircle(mx, my, 3, t.blue); }   // me
      }
    }
    drawPill(g, zx, MAP_Y + 4, g.textWidth(z) + 16, 18, t.panel, t.txt, z);
    const double mpp = 156543.03392 * cos(_lat * M_PI / 180.0) / ((1 << p.z) * p.k);
    static const double STEPS_M[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000};
    static const double STEPS_MI[] = {0.02, 0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10, 20, 50, 100};
    double meters = 0; char label[16] = "";
    if (ui_settings.miles) {
      for (double mi : STEPS_MI) { if (mi * 1609.34 / mpp > 90) break; meters = mi * 1609.34;
        if (mi < 0.5) snprintf(label, sizeof(label), "%d ft", (int)lround(mi * 5280)); else snprintf(label, sizeof(label), "%g mi", mi); }
    } else {
      for (double m : STEPS_M) { if (m / mpp > 90) break; meters = m;
        if (m < 1000) snprintf(label, sizeof(label), "%d m", (int)m); else snprintf(label, sizeof(label), "%g km", m / 1000); }
    }
    if (meters > 0) {
      const int px = (int)(meters / mpp);
      const int y = L::H - 10;
      g.fillRect(8, y - 14, px + 50, 20, t.bg);
      g.drawFastHLine(10, y, px, t.white);
      g.drawFastVLine(10, y - 4, 5, t.white);
      g.drawFastVLine(10 + px, y - 4, 5, t.white);
      g.setTextColor(t.white, t.bg);
      g.drawString(label, 14 + px, y - 12);
    }
    if (_focus >= 0 && _focus < (int)_nodes.size()) {
      const MapNode& n = _nodes[_focus];
      char info[96];
      if (n.dist >= 0) snprintf(info, sizeof(info), "%s  %s", n.name, app::fmtDistance(n.dist));
      else snprintf(info, sizeof(info), "%s", n.name);
      if (TOUCH && !_pinch) {          // one of several on this spot: which, and that a tap steps on
        int cand[STACK_MAX];
        const int nc = nodesAt(scrX(p, n.lon), scrY(p, n.lat), cand);
        for (int i = 0; nc > 1 && i < nc; i++)
          if (cand[i] == _focus) {
            const size_t len = strlen(info);
            snprintf(info + len, sizeof(info) - len, "  %d/%d", i + 1, nc);
            break;
          }
      }
      const int w = min(TOUCH ? L::W / 2 : L::W - 20, widthUtf8(g, info) + 20);
      _pillX = L::W - w - 6;
      drawPill(g, _pillX, L::H - 24, w, 20, t.panel, t.green, info);
    } else {
      const char* hint = TOUCH ? "drag: move  pinch: zoom" : "turn zoom  wasd pan  n next node  c me";
      g.setTextColor(t.txt, t.bg);
      g.fillRect(L::W - g.textWidth(hint) - 12, L::H - 22, g.textWidth(hint) + 12, 20, t.bg);
      g.drawString(hint, L::W - g.textWidth(hint) - 6, L::H - 20);
    }
    char cnt[32];
    switch (nodeShow()) {
      case SHOW_NONE: strlcpy(cnt, "nodes hidden", sizeof(cnt)); break;
      case SHOW_FAVS: snprintf(cnt, sizeof(cnt), "%d favourite%s", (int)_nodes.size(), _nodes.size() == 1 ? "" : "s"); break;
      case SHOW_DOTS: snprintf(cnt, sizeof(cnt), "%d nodes, dots", (int)_nodes.size()); break;
      default:        snprintf(cnt, sizeof(cnt), "%d nodes", (int)_nodes.size()); break;
    }
    _cntX = L::W - g.textWidth(cnt) - 22;
    drawPill(g, _cntX, MAP_Y + 4, g.textWidth(cnt) + 16, 18, t.panel, t.txt, cnt);
  }

private:
  void collectNodes() {
    // The picked node is a place in this list, and the list is rebuilt and sorted
    // again whenever the contacts change - every few seconds on a busy mesh. Kept by
    // its key, or the label at the bottom right jumped to whatever node took its place.
    uint8_t keep[32];
    const bool had = _focus >= 0 && _focus < (int)_nodes.size();
    if (had) memcpy(keep, _nodes[_focus].pub, sizeof(keep));
    _focus = -1;
    _nodes.clear();
    _collected = millis();
    if (!g_node) return;
    _gen = g_node->contactsGen();
    double mla = 0, mlo = 0;
    const bool me = app::myPosition(mla, mlo);
    ContactsIterator it = g_node->startContactsIterator();
    ContactInfo c;
    const NodeShow show = nodeShow();
    while (it.hasNext(g_node, c)) {
      if (!c.type || (!c.gps_lat && !c.gps_lon)) continue;
      const bool pinned = _pin[0] && !strcmp(c.name, _pin);
      if (!pinned && (show == SHOW_NONE || (show == SHOW_FAVS && !(c.flags & 1)))) continue;
      MapNode n;
      memcpy(n.pub, c.id.pub_key, 32);
      strlcpy(n.name, c.name, sizeof(n.name));
      n.type = c.type;
      n.lat = c.gps_lat / 1e6; n.lon = c.gps_lon / 1e6;
      if (fabs(n.lat) > 85 || fabs(n.lon) > 180) continue;
      n.dist = me ? (float)app::distanceKm(mla, mlo, n.lat, n.lon) : -1.0f;
      _nodes.push_back(n);
    }
    std::sort(_nodes.begin(), _nodes.end(), [](const MapNode& a, const MapNode& b) {
      if ((a.dist < 0) != (b.dist < 0)) return b.dist < 0;
      return a.dist < b.dist;
    });
    if (had)
      for (int i = 0; i < (int)_nodes.size(); i++)
        if (!memcmp(_nodes[i].pub, keep, sizeof(keep))) { _focus = i; break; }
  }

  // The touch buttons down the right edge: 0 zoom in, 1 zoom out, 2 me; -1 none.
  static constexpr bool TOUCH = BOARD_HAS_TOUCH;
  static constexpr int BTN = 36, BTN_Y = MAP_Y + 30;
  int buttonAt(int x, int y) const {
    if (!TOUCH || x < L::W - BTN - 12) return -1;
    for (int b = 0; b < 3; b++) {
      const int by = BTN_Y + b * (BTN + 6);
      if (y >= by - 3 && y < by + BTN + 3) return b;
    }
    return -1;
  }

  // What a tap at (x, y) is on: the node names drawn there (the one on top first), then
  // the dots within a fingertip, nearest first. Up to STACK_MAX.
  static constexpr int STACK_MAX = 12, REACH = 22;
  int nodesAt(int x, int y, int* out) const {
    int n = 0;
    auto have = [&](int i) { for (int j = 0; j < n; j++) if (out[j] == i) return true; return false; };
    for (int j = (int)_labels.size() - 1; j >= 0 && n < STACK_MAX; j--) {
      const LabelBox& b = _labels[j];
      if (x >= b.x - 3 && x < b.x + b.w + 3 && y >= b.y - 3 && y < b.y + b.h + 3 && !have(b.i)) out[n++] = b.i;
    }
    const Proj p = proj();
    const int first = n;
    int dd[STACK_MAX];
    for (int i = 0; i < (int)_nodes.size(); i++) {
      const int nx = scrX(p, _nodes[i].lon) - x, ny = scrY(p, _nodes[i].lat) - y;
      const int d = nx * nx + ny * ny;
      if (d >= REACH * REACH || have(i)) continue;
      int j;
      if (n < STACK_MAX) j = n++;
      else if (d >= dd[STACK_MAX - 1]) continue;     // full, and further than all of them
      else j = STACK_MAX - 1;
      while (j > first && dd[j - 1] > d) { dd[j] = dd[j - 1]; out[j] = out[j - 1]; j--; }
      dd[j] = d; out[j] = i;
    }
    return n;
  }

  // The map's own switch for the nodes: names, dots only, favourites, none, and round.
  void stepNodeShow() {
    static const char* const SAY[] = {"nodes with names", "nodes as dots only", "favourites only", "nodes hidden"};
    const NodeShow s = (NodeShow)((nodeShow() + 1) % 4);
    setNodeShow(s);
    collectNodes();
    nav.toast(SAY[s]);
    dirty = true;
  }

  void jump(int d) {
    if (_nodes.empty()) {
      const NodeShow s = nodeShow();
      nav.toast(s == SHOW_NONE ? (TOUCH ? "nodes are hidden: tap the top right to show them" : "nodes are hidden: m shows them")
                : s == SHOW_FAVS ? "no favourites have shared a position" : "no contacts have shared a position");
      return;
    }
    const int n = _nodes.size();
    _focus = _focus < 0 ? (d > 0 ? 0 : n - 1) : ((_focus + d) % n + n) % n;
    _lat = _nodes[_focus].lat;
    _lon = _nodes[_focus].lon;
    if (_z < 11) _z = 12;
    dirty = true;
  }

  TileCache* _cache;
  std::vector<MapNode> _nodes;
  double _lat = 0, _lon = 0;
  int _z = 12, _focus = -1, _pillX = L::W;
  bool _pinch = false;                 // two fingers are zooming it
  int _pz0 = 12, _pcx0 = 0, _pcy0 = 0, _pcx = 0, _pcy = 0;
  float _pscale = 1.0f;
  double _plat = 0, _plon = 0;          // the place that was between the fingers
  bool _held = false;                  // a finger is on the glass
  bool _pending = false;               // tiles of the level shown still to read
  uint32_t _stillAt = 0;               // when the map last moved or was touched
  struct LabelBox { int i; int16_t x, y, w, h; };
  std::vector<LabelBox> _labels;       // node names as drawn, for taps
  uint32_t _lastTap = 0;              // double tap to zoom
  int _tapX = 0, _tapY = 0;
  uint8_t _layer = 0;
  char _pin[32] = "";                  // the node the map was opened on, always shown
  int _cntX = L::W;                    // the node count at the top right: the nodes switch (touch)
  uint32_t _gen = 0, _collected = 0, _last = 0;
};

void app::openMap(double lat, double lon, const char* focusName) {
  nav.push(new MapView(lat, lon, focusName));
}
