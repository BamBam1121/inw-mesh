// Slippy map from SD tiles (/tiles/{z}/{x}/{y}.jpg|png, /tiles/topo for the
// second layer) with positioned contacts on top.
//
// Keys: wheel zooms, wasd/ijkl pan, n/b step through nodes, c centres on you,
// m toggles labels, t toggles topo.

#include "app.h"
#include "node.h"
#include "dataio.h"
#include "netwifi.h"
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

class TileCache {
public:
  static constexpr int N = 12;
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

  // A tile just landed on the card: forget that we thought it was missing.
  void forget(int z, int x, int y) {
    for (auto& s : slots) if (s.z == z && s.x == x && s.y == y && s.layer == 0) { s.z = -1; s.used = 0; }
  }

private:
  bool load(TileSlot& s) {
    if (!sdMount()) return false;
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
      case 'm': ui_settings.mapLabels = !ui_settings.mapLabels; dirty = true; return;
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

  void tick() override {
    // Finished downloads are written HERE, on the loop task: the SD card shares
    // the SPI bus with the panel and radio, so no other task may touch it.
    wifi::TileDone d;
    while (wifi::pollTile(d)) {
      if (sdMount()) {
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
      }
      free(d.data);
    }
    if (g_node && g_node->contactsGen() != _gen && millis() - _collected > 5000) { collectNodes(); dirty = true; }
    if (millis() - _last > 5000) { _last = millis(); dirty = true; }   // my position moves
  }

  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    const double cx = lon2x(_lon, _z), cy = lat2y(_lat, _z);
    const double left = cx - L::W / 2.0, top = cy - MAP_H / 2.0;
    const int tx0 = (int)floor(left / TILE), ty0 = (int)floor(top / TILE);
    const int tx1 = (int)floor((left + L::W) / TILE), ty1 = (int)floor((top + MAP_H) / TILE);
    g.setClipRect(0, MAP_Y, L::W, MAP_H);
    g.fillRect(0, MAP_Y, L::W, MAP_H, t.panel);
    bool any = false;
    for (int ty = ty0; ty <= ty1; ty++) {
      for (int tx = tx0; tx <= tx1; tx++) {
        const int sx = (int)lround(tx * TILE - left), sy = (int)lround(ty * TILE - top) + MAP_Y;
        lgfx::LGFX_Sprite* s = _cache->get(_z, tx, ty, _layer);
        if (s) { s->pushSprite(&g, sx, sy); any = true; }
        else {
          // No tile: a faint grid so panning still reads as movement.
          for (int k = 0; k < TILE; k += 64) {
            g.drawFastHLine(sx, sy + k, TILE, t.line);
            g.drawFastVLine(sx + k, sy, TILE, t.line);
          }
        }
      }
    }
    // Nodes
    g.setFont(&fonts::Font2);
    for (int i = 0; i < (int)_nodes.size(); i++) {
      const MapNode& n = _nodes[i];
      const int x = (int)lround(lon2x(n.lon, _z) - left);
      const int y = (int)lround(lat2y(n.lat, _z) - top) + MAP_Y;
      if (x < -20 || x > L::W + 20 || y < MAP_Y - 20 || y > L::H + 20) continue;
      const bool f = i == _focus;
      const uint16_t col = n.type == ADV_TYPE_REPEATER ? t.green : n.type == ADV_TYPE_ROOM ? t.amber : t.blue;
      if (n.type == ADV_TYPE_REPEATER) { g.fillTriangle(x, y - 6, x - 5, y + 4, x + 5, y + 4, col); g.drawTriangle(x, y - 7, x - 6, y + 5, x + 6, y + 5, t.bg); }
      else { g.fillCircle(x, y, 4, col); g.drawCircle(x, y, 5, t.bg); }
      if (f) g.drawCircle(x, y, 9, t.white);
      if ((ui_settings.mapLabels && _z >= 11) || f) {
        char nm[40];
        sanitize(n.name, nm, sizeof(nm) - 4);
        richFit(g, nm, 130);
        const int w = richWidth(g, nm) + 6;
        g.fillRoundRect(x + 8, y - 8, w, 16, 3, f ? t.greenDim : t.bg);
        g.setTextColor(f ? t.white : t.txt, f ? t.greenDim : t.bg);
        drawRich(g, nm, x + 11, y - 8);
      }
    }
    // Me
    double la, lo;
    if (app::myPosition(la, lo)) {
      const int x = (int)lround(lon2x(lo, _z) - left);
      const int y = (int)lround(lat2y(la, _z) - top) + MAP_Y;
      g.fillCircle(x, y, 7, t.white);
      g.fillCircle(x, y, 5, t.blue);
    }
    g.clearClipRect();

    // Chrome: zoom chip, scale bar, focus panel.
    char z[40];
    if (wifi::connected() && ui_settings.tileFetch && !_layer)
      snprintf(z, sizeof(z), "z%d  wifi %u tiles", _z, wifi::tilesFetched());
    else
      snprintf(z, sizeof(z), "z%d%s%s", _z, _layer ? " topo" : "", any ? "" : "  no tiles here");
    drawPill(g, 6, MAP_Y + 4, g.textWidth(z) + 16, 18, t.panel, t.txt, z);
    const double mpp = 156543.03392 * cos(_lat * M_PI / 180.0) / (1 << _z);
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
      const int w = min(L::W - 20, widthUtf8(g, info) + 20);
      drawPill(g, L::W - w - 6, L::H - 24, w, 20, t.panel, t.green, info);
    } else {
      const char* hint = "turn zoom  wasd pan  n next node  c me";
      g.setTextColor(t.txt, t.bg);
      g.fillRect(L::W - g.textWidth(hint) - 12, L::H - 22, g.textWidth(hint) + 12, 20, t.bg);
      g.drawString(hint, L::W - g.textWidth(hint) - 6, L::H - 20);
    }
    char cnt[24];
    snprintf(cnt, sizeof(cnt), "%d nodes", (int)_nodes.size());
    drawPill(g, L::W - g.textWidth(cnt) - 22, MAP_Y + 4, g.textWidth(cnt) + 16, 18, t.panel, t.txt, cnt);
  }

private:
  void collectNodes() {
    _nodes.clear();
    _collected = millis();
    if (!g_node) return;
    _gen = g_node->contactsGen();
    double mla = 0, mlo = 0;
    const bool me = app::myPosition(mla, mlo);
    ContactsIterator it = g_node->startContactsIterator();
    ContactInfo c;
    while (it.hasNext(g_node, c)) {
      if (!c.type || (!c.gps_lat && !c.gps_lon)) continue;
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
  }

  void jump(int d) {
    if (_nodes.empty()) { nav.toast("no contacts have shared a position"); return; }
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
  int _z = 12, _focus = -1;
  uint8_t _layer = 0;
  uint32_t _gen = 0, _collected = 0, _last = 0;
};

void app::openMap(double lat, double lon, const char* focusName) {
  nav.push(new MapView(lat, lon, focusName));
}
