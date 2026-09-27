#include "extport.h"
#include "app.h"
#include "board_pins.h"
#include "bringup.h"
#include "settings.h"
#include "ui.h"
#include "node.h"
#include <Wire.h>
#include <CayenneLPP.h>
#include <helpers/SensorManager.h>
#include <algorithm>
#include <math.h>
#include <soc/gpio_reg.h>
#include <vector>

void markUiDirty();

#if !BOARD_HAS_EXT_HEADER
// No header on this board: nothing to find, nothing to drive.
namespace ext {
void begin() {}
void tick() {}
void alert() {}
void applyPin() {}
void telemetry(CayenneLPP&) {}
void openPage() {}
void report() { Serial.println("[ext] no header on this board"); }
}  // namespace ext
#else

namespace ext {
namespace {

// Cheap breakout boards people actually have. All of them answer on the bus with
// nothing to set up, and none clash with the chips already on the board.
enum Kind : uint8_t { BME280, BMP280, SHT3X, SHT4X, AHT2X, BH1750 };
const char* const NAMES[] = {"BME280", "BMP280", "SHT3x", "SHT4x", "AHT20", "BH1750"};

struct Sensor {
  Kind     kind;
  uint8_t  addr;
  bool     armed = false;      // a measurement was started last tick (SHT, AHT)
  uint8_t  fails = 0;
  uint32_t at = 0;             // millis() of the last good reading, 0 = none yet
  float    t = NAN, h = NAN, p = NAN, lux = NAN;   // C, %, hPa, lux
  // BME280/BMP280 factory calibration
  uint16_t T1 = 0, P1 = 0;
  int16_t  T2 = 0, T3 = 0, P2 = 0, P3 = 0, P4 = 0, P5 = 0, P6 = 0, P7 = 0, P8 = 0, P9 = 0;
  uint8_t  H1 = 0, H3 = 0;
  int16_t  H2 = 0, H4 = 0, H5 = 0;
  int8_t   H6 = 0;
};

std::vector<Sensor>  s_sensors;
std::vector<uint8_t> s_unknown;      // answered on the bus, not something we can read
uint32_t s_gen = 0;                  // bumps when the list changes (the page rebuilds)
uint32_t s_readAt = 0, s_scanAt = 0;
uint32_t s_blinkFrom = 0;            // IO9 blink pattern start, 0 = idle
bool     s_pinHigh = false;

constexpr uint32_t READ_MS = 5000, RESCAN_MS = 60000, STALE_MS = 60000;

// ---- bus -----------------------------------------------------------------------------
bool ack(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }
bool wr(uint8_t a, std::initializer_list<uint8_t> d) {
  Wire.beginTransmission(a);
  for (uint8_t b : d) Wire.write(b);
  return Wire.endTransmission() == 0;
}
bool rd(uint8_t a, uint8_t* d, uint8_t n) {
  if (Wire.requestFrom(a, n) != n) { while (Wire.available()) Wire.read(); return false; }
  for (uint8_t i = 0; i < n; i++) d[i] = Wire.read();
  return true;
}
bool rdReg(uint8_t a, uint8_t reg, uint8_t* d, uint8_t n) {
  Wire.beginTransmission(a);
  Wire.write(reg);
  return Wire.endTransmission(false) == 0 && rd(a, d, n);
}
// Sensirion's CRC-8 (poly 0x31, init 0xFF), on every 2-byte word they send.
uint8_t crc8(const uint8_t* d, int n) {
  uint8_t c = 0xFF;
  for (int i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = c & 0x80 ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
  }
  return c;
}
bool words2(const uint8_t* d) { return crc8(d, 2) == d[2] && crc8(d + 3, 2) == d[5]; }

bool onboard(uint8_t a) {
  if (a == 0x29) return true;                     // the IMU on a board strapped the other way
  for (const auto& c : bringup::EXPECTED) if (c.addr == a) return true;
  return false;
}

// ---- detect --------------------------------------------------------------------------
bool setupBme(Sensor& s) {
  uint8_t c[26], e[7];
  if (!rdReg(s.addr, 0x88, c, 26)) return false;
  auto u16 = [&](int i) { return (uint16_t)(c[i] | (c[i + 1] << 8)); };
  s.T1 = u16(0);  s.T2 = (int16_t)u16(2);  s.T3 = (int16_t)u16(4);
  s.P1 = u16(6);  s.P2 = (int16_t)u16(8);  s.P3 = (int16_t)u16(10); s.P4 = (int16_t)u16(12);
  s.P5 = (int16_t)u16(14); s.P6 = (int16_t)u16(16); s.P7 = (int16_t)u16(18);
  s.P8 = (int16_t)u16(20); s.P9 = (int16_t)u16(22);
  if (s.kind == BME280) {
    s.H1 = c[25];
    if (!rdReg(s.addr, 0xE1, e, 7)) return false;
    s.H2 = (int16_t)(e[0] | (e[1] << 8));
    s.H3 = e[2];
    s.H4 = (int16_t)(((int16_t)(int8_t)e[3] * 16) | (e[4] & 0x0F));
    s.H5 = (int16_t)(((int16_t)(int8_t)e[5] * 16) | (e[4] >> 4));
    s.H6 = (int8_t)e[6];
  }
  // Sleep, then: humidity x1, a reading every second, no filter, temp and pressure x1,
  // running on its own. Measuring once a second at x1 is too little to warm it.
  bool ok = wr(s.addr, {0xF4, 0x00});
  if (s.kind == BME280) ok &= wr(s.addr, {0xF2, 0x01});
  ok &= wr(s.addr, {0xF5, 0xA0});
  ok &= wr(s.addr, {0xF4, 0x27});
  return ok;
}

// The whole bus when the page opens; just the sensor addresses when it is
// the minutely look for something plugged in while running.
const uint8_t CANDIDATES[] = {0x23, 0x38, 0x44, 0x45, 0x46, 0x5C, 0x76, 0x77};

void probe(bool whole = true) {
  std::vector<Sensor> found;
  std::vector<uint8_t> unknown;
  auto add = [&](Kind k, uint8_t a) { Sensor s; s.kind = k; s.addr = a; found.push_back(s); return &found.back(); };

  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (!whole && std::find(std::begin(CANDIDATES), std::end(CANDIDATES), a) == std::end(CANDIDATES)) continue;
    if (onboard(a) || !ack(a)) continue;
    uint8_t d[6];
    bool known = false;
    if (a == 0x76 || a == 0x77) {
      if (rdReg(a, 0xD0, d, 1) && (d[0] == 0x60 || d[0] == 0x58 || d[0] == 0x56 || d[0] == 0x57)) {
        Sensor* s = add(d[0] == 0x60 ? BME280 : BMP280, a);
        known = setupBme(*s);
        if (!known) found.pop_back();
      }
    } else if (a == 0x44 || a == 0x45 || a == 0x46) {
      // SHT3x has 2-byte commands and answers its status register straight away;
      // SHT4x has 1-byte ones, so it doesn't, and is asked for its serial instead.
      if (wr(a, {0xF3, 0x2D}) && rd(a, d, 3) && crc8(d, 2) == d[2]) { add(SHT3X, a); known = true; }
      else if (wr(a, {0x89})) { delay(10); if (rd(a, d, 6) && words2(d)) { add(SHT4X, a); known = true; } }
    } else if (a == 0x38) {
      if (rd(a, d, 1)) {
        if (!(d[0] & 0x08)) {                   // not calibrated yet: the AHT20 init (AHT10: 0xE1)
          if (!wr(a, {0xBE, 0x08, 0x00})) wr(a, {0xE1, 0x08, 0x00});
          delay(10);
        }
        add(AHT2X, a); known = true;
      }
    } else if (a == 0x23 || a == 0x5C) {
      if (wr(a, {0x01}) && wr(a, {0x10})) { add(BH1750, a); known = true; }   // on, 1 lux steps, continuous
    }
    if (!known && unknown.size() < 6) unknown.push_back(a);
  }
  if (!whole) unknown = s_unknown;

  // A sensor that was already there keeps its last reading.
  for (Sensor& n : found)
    for (const Sensor& o : s_sensors)
      if (o.addr == n.addr && o.kind == n.kind) { n.at = o.at; n.t = o.t; n.h = o.h; n.p = o.p; n.lux = o.lux; }
  const bool changed = found.size() != s_sensors.size() || unknown != s_unknown ||
                       !std::equal(found.begin(), found.end(), s_sensors.begin(),
                                   [](const Sensor& x, const Sensor& y) { return x.addr == y.addr && x.kind == y.kind; });
  if (changed) {
    for (const Sensor& s : found)
      if (std::none_of(s_sensors.begin(), s_sensors.end(), [&](const Sensor& o) { return o.addr == s.addr; }))
        Serial.printf("[ext] %s at 0x%02X\n", NAMES[s.kind], s.addr);
    s_gen++;
  }
  s_sensors.swap(found);
  s_unknown.swap(unknown);
  s_scanAt = millis();
}

// ---- measure -------------------------------------------------------------------------
bool readBme(Sensor& s) {
  uint8_t d[8];
  const uint8_t n = s.kind == BME280 ? 8 : 6;
  if (!rdReg(s.addr, 0xF7, d, n)) return false;
  const int32_t adcP = (d[0] << 12) | (d[1] << 4) | (d[2] >> 4);
  const int32_t adcT = (d[3] << 12) | (d[4] << 4) | (d[5] >> 4);
  if (adcT == 0x80000) return true;            // no measurement finished yet
  // Bosch's floating-point compensation (BME280 datasheet 8.1).
  double v1 = (adcT / 16384.0 - s.T1 / 1024.0) * s.T2;
  double v2 = (adcT / 131072.0 - s.T1 / 8192.0) * (adcT / 131072.0 - s.T1 / 8192.0) * s.T3;
  const double tFine = v1 + v2;
  s.t = (float)(tFine / 5120.0);

  v1 = tFine / 2.0 - 64000.0;
  v2 = v1 * v1 * s.P6 / 32768.0;
  v2 = v2 + v1 * s.P5 * 2.0;
  v2 = v2 / 4.0 + s.P4 * 65536.0;
  v1 = (s.P3 * v1 * v1 / 524288.0 + s.P2 * v1) / 524288.0;
  v1 = (1.0 + v1 / 32768.0) * s.P1;
  if (v1 != 0 && adcP != 0x80000) {
    double p = 1048576.0 - adcP;
    p = (p - v2 / 4096.0) * 6250.0 / v1;
    v1 = s.P9 * p * p / 2147483648.0;
    v2 = p * s.P8 / 32768.0;
    s.p = (float)((p + (v1 + v2 + s.P7) / 16.0) / 100.0);
  }
  if (s.kind == BME280) {
    const int32_t adcH = (d[6] << 8) | d[7];
    if (adcH != 0x8000) {
      double h = tFine - 76800.0;
      h = (adcH - (s.H4 * 64.0 + s.H5 / 16384.0 * h)) *
          (s.H2 / 65536.0 * (1.0 + s.H6 / 67108864.0 * h * (1.0 + s.H3 / 67108864.0 * h)));
      h = h * (1.0 - s.H1 * h / 524288.0);
      s.h = (float)constrain(h, 0.0, 100.0);
    }
  }
  return true;
}

// The Sensirion and Aosong parts measure on request: each tick collects what the
// last one started and starts the next, so nothing here waits on them.
bool readSht(Sensor& s) {
  uint8_t d[6];
  if (s.armed) {
    if (!rd(s.addr, d, 6) || !words2(d)) { s.armed = false; return false; }
    const float rt = (d[0] << 8) | d[1], rh = (d[3] << 8) | d[4];
    s.t = -45.0f + 175.0f * rt / 65535.0f;
    s.h = s.kind == SHT4X ? constrain(-6.0f + 125.0f * rh / 65535.0f, 0.0f, 100.0f) : 100.0f * rh / 65535.0f;
  }
  s.armed = s.kind == SHT4X ? wr(s.addr, {0xFD}) : wr(s.addr, {0x24, 0x00});
  return s.armed;
}

bool readAht(Sensor& s) {
  uint8_t d[6];
  if (s.armed) {
    if (!rd(s.addr, d, 6)) { s.armed = false; return false; }
    if (!(d[0] & 0x80)) {                      // not still busy
      const uint32_t rh = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
      const uint32_t rt = ((uint32_t)(d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
      s.h = rh * 100.0f / 1048576.0f;
      s.t = rt * 200.0f / 1048576.0f - 50.0f;
    }
  }
  s.armed = wr(s.addr, {0xAC, 0x33, 0x00});
  return s.armed;
}

bool readLux(Sensor& s) {
  uint8_t d[2];
  if (!rd(s.addr, d, 2)) return false;
  s.lux = ((d[0] << 8) | d[1]) / 1.2f;
  return true;
}

void measure() {
  bool lost = false;
  for (Sensor& s : s_sensors) {
    bool ok = false;
    switch (s.kind) {
      case BME280: case BMP280: ok = readBme(s); break;
      case SHT3X: case SHT4X:   ok = readSht(s); break;
      case AHT2X:               ok = readAht(s); break;
      case BH1750:              ok = readLux(s); break;
    }
    if (ok) { s.fails = 0; if (!isnan(s.t) || !isnan(s.lux)) s.at = millis() | 1; }
    else if (++s.fails >= 3) lost = true;      // unplugged: look again
  }
  if (lost) probe(false);
}

// Signed: `at` is millis()|1, a millisecond ahead of millis() on an even one.
bool fresh(const Sensor& s) { return s.at && (int32_t)(millis() - s.at) < (int32_t)STALE_MS; }

// ---- IO9 -----------------------------------------------------------------------------
void pin(bool high) {
  if (high == s_pinHigh) return;
  s_pinHigh = high;
  digitalWrite(PIN_EXT_IO0, high ? HIGH : LOW);
}

void pinTick() {
  switch (ui_settings.io9Mode) {
    case 1: {                                   // six short flashes (or beeps) per alert
      if (!s_blinkFrom) { pin(false); return; }
      const int32_t since = (int32_t)(millis() - s_blinkFrom);   // -1 in the millisecond it began
      const uint32_t step = since > 0 ? since / 200 : 0;
      if (step >= 12) { s_blinkFrom = 0; pin(false); return; }
      pin(!(step & 1));
      return;
    }
    case 2: {                                   // lit while anything is unread
      static uint32_t at = 0;
      if (at && millis() - at < 250) return;
      at = millis() | 1;
      pin(app::unread() > 0);
      return;
    }
    default: return;
  }
}

// ---- page ----------------------------------------------------------------------------
const char* const PIN_MODES[] = {"off", "flash on new messages", "on while unread"};

String reading(const Sensor& s) {
  if (!s.at) return "reading...";
  char b[64];
  size_t w = 0;
  auto put = [&](const char* fmt, double v) { if (w < sizeof(b)) w += snprintf(b + w, sizeof(b) - w, fmt, v); };
  if (!isnan(s.t)) put(ui_settings.miles ? "%.1f F  " : "%.1f C  ", ui_settings.miles ? s.t * 1.8 + 32 : s.t);
  if (!isnan(s.h)) put("%.0f%%  ", s.h);
  if (!isnan(s.p)) put(ui_settings.miles ? "%.2f inHg  " : "%.1f hPa  ", ui_settings.miles ? s.p * 0.0295300 : s.p);
  if (!isnan(s.lux)) put("%.0f lux  ", s.lux);
  if (!fresh(s)) put("(%.0f min old)", (millis() - s.at) / 60000.0);
  return String(b);
}

// Numbered as on LilyGo's schematic (J3).
void pinLines(std::vector<String>& out) {
  out.push_back(" 1  GND                    2  3.3 V");
  out.push_back(" 3  TX  (IO43)             4  RX  (IO44)");
  out.push_back(" 5  SCK  (IO35)            6  MOSI  (IO34)");
  out.push_back(" 7  MISO  (IO33)           8  IO9");
  out.push_back(" 9  SDA  (IO3)            10  NRF_CE (expander)");
  out.push_back("11  SCL  (IO2)            12  5 V, only on USB");
  out.push_back("");
  out.push_back("sensor: its VCC to 3.3 V, GND, SDA, SCL");
  out.push_back("LED: IO9 > 330 ohm > LED > GND");
  out.push_back("buzzer: a small active 3.3 V one, IO9 and GND");
  out.push_back("SCK/MOSI/MISO are the radio's bus: leave them be");
}

class HeaderPage : public MenuView {
public:
  HeaderPage() : MenuView("Top header") { build(); }
  void tick() override {
    if (_gen != s_gen) { const int f = _focus; build(); _focus = min(f, (int)_rows.size() - 1); dirty = true; }
    MenuView::tick();
  }
private:
  uint32_t _gen = 0;
  void build() {
    _gen = s_gen;
    clear();
    header("sensors plugged in");
    if (s_sensors.empty() && s_unknown.empty())
      info("none yet", [] { return String("3.3 V, GND, SDA, SCL"); });
    for (size_t i = 0; i < s_sensors.size(); i++) {
      char lab[24];
      snprintf(lab, sizeof(lab), "%s  %02X", NAMES[s_sensors[i].kind], s_sensors[i].addr);
      const Kind k = s_sensors[i].kind;
      const uint8_t a = s_sensors[i].addr;
      info(lab, [k, a] {
        for (const Sensor& s : s_sensors) if (s.addr == a && s.kind == k) return reading(s);
        return String("gone");
      });
    }
    for (uint8_t a : s_unknown) {
      char lab[24];
      snprintf(lab, sizeof(lab), "device  %02X", a);
      info(lab, [] { return String("not a sensor I can read"); });
    }
    action("look again", [] {
      probe();
      nav.toast(s_sensors.empty() ? "no sensors found" : (String((int)s_sensors.size()) + " sensor(s)").c_str());
    });
    info("shared on the mesh", [] {
      // Readings go out only with the basic telemetry allowed as well.
      if (!g_node) return String("");
      const uint8_t who = min(g_node->prefs().telemetry_mode_base, g_node->prefs().telemetry_mode_env);
      return String(who >= 2 ? "if anyone asks" : who == 1 ? "if a favourite asks" : "no, see Settings > Telemetry");
    });
    header("IO9 pin (LED or buzzer)");
    value("IO9", [] { return String(PIN_MODES[ui_settings.io9Mode % 3]); }, [] {
      ui_settings.io9Mode = (ui_settings.io9Mode + 1) % 3;
      applyPin();
      markUiDirty();
      if (ui_settings.io9Mode == 1) alert();     // show what it does
    });
    action("which pin is which", [] { pinout(); });
  }
  static void pinout() { nav.push(new TextPageView("Top header pins", pinLines, 60000)); }
};

}  // namespace

// ---- public --------------------------------------------------------------------------
void begin() {
  applyPin();
  probe();
  s_readAt = millis();
}

void tick() {
  pinTick();
  const uint32_t now = millis();
  if (now - s_readAt >= READ_MS) {
    s_readAt = now;
    if (!s_sensors.empty()) measure();
  }
  // Plugged in while running: look again now and then, only while there's nothing.
  if (s_sensors.empty() && now - s_scanAt >= RESCAN_MS) probe(false);
}

void alert() {
  if (ui_settings.io9Mode == 1) s_blinkFrom = millis() | 1;
}

void applyPin() {
  if (ui_settings.io9Mode) {
    pinMode(PIN_EXT_IO0, OUTPUT);
    s_pinHigh = true;                           // force the write below
    pin(false);
  } else {
    pinMode(PIN_EXT_IO0, INPUT);               // left floating, as before this existed
    s_pinHigh = false;
    s_blinkFrom = 0;
  }
}

void telemetry(CayenneLPP& lpp) {
  // Channel 1 is the pager itself (battery, the chip's own temperature); plug-in
  // sensors follow, one channel each, the way MeshCore's own sensor code does it.
  uint8_t ch = TELEM_CHANNEL_SELF + 1;
  for (const Sensor& s : s_sensors) {
    if (!fresh(s)) continue;
    if (!isnan(s.t))   lpp.addTemperature(ch, s.t);
    if (!isnan(s.h))   lpp.addRelativeHumidity(ch, s.h);
    if (!isnan(s.p))   lpp.addBarometricPressure(ch, s.p);
    if (!isnan(s.lux)) lpp.addLuminosity(ch, s.lux);
    ch++;
  }
}

void report() {
  const uint32_t t0 = micros();
  probe();
  const uint32_t whole = micros() - t0;
  probe(false);
  Serial.printf("[ext] scan %lu us, sensor addresses only %lu us\n", (unsigned long)whole, (unsigned long)(micros() - t0 - whole));
  // What the pin is really doing, from the GPIO registers.
  const bool drive = (REG_READ(GPIO_ENABLE_REG) >> PIN_EXT_IO0) & 1, level = (REG_READ(GPIO_OUT_REG) >> PIN_EXT_IO0) & 1;
  Serial.printf("[ext] io9 %s\n", !drive ? "not driven" : level ? "driven high" : "driven low");
  Serial.printf("[ext] %u sensor(s), io9 mode %u (%s)\n", (unsigned)s_sensors.size(), ui_settings.io9Mode,
                s_pinHigh ? "high" : "low");
  for (const Sensor& s : s_sensors)
    Serial.printf("[ext] %s 0x%02X: %s\n", NAMES[s.kind], s.addr, reading(s).c_str());
  for (uint8_t a : s_unknown) Serial.printf("[ext] unknown device 0x%02X\n", a);
}

#if INW_DEV
// "extfake": made-up readings, to see the page with sensors on it (gone again
// once the next reading finds nothing at those addresses).
void fake() {
  Sensor a; a.kind = BME280; a.addr = 0x76; a.at = millis() | 1; a.t = 21.4f; a.h = 38.0f; a.p = 1012.6f;
  Sensor b; b.kind = BH1750; b.addr = 0x23; b.at = millis() | 1; b.lux = 312.0f;
  s_sensors = {a, b};
  s_unknown = {0x3C};
  s_gen++;
}

void drawPage(Canvas& g, int down) {
  if (down < 0) {                              // the pin list
    TextPageView t("Top header pins", pinLines);
    t.tick();
    t.draw(g);
    return;
  }
  HeaderPage p;
  if (down) p.rotate(down);
  p.draw(g);
}
#endif

void openPage() {
  probe();
  nav.push(new HeaderPage());
}

}  // namespace ext

#endif  // BOARD_HAS_EXT_HEADER
