// "Show my QR code": your contact as a QR code, for the MeshCore phone app (or any
// client that reads MeshCore links) to scan and add you. The same link the pager
// writes over NFC (nfcapp.cpp myContactLink):
//   meshcore://contact/add?name=<url-encoded>&public_key=<64 hex>&type=1
// LovyanGFX draws it (its built-in encoder picks the smallest version that fits,
// error correction low), black on white with the four-module quiet zone phones need,
// whatever the theme's colours.

#pragma once
#include <Arduino.h>
#include "ui.h"
#include "node.h"

namespace qr {

inline String urlEncodeName(const char* s) {
  String o;
  for (; s && *s; s++) {
    const uint8_t c = *s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { char h[4]; snprintf(h, sizeof(h), "%%%02X", c); o += h; }
  }
  return o;
}

inline String contactLink() {
  if (!g_node) return "";
  char hex[65];
  mesh::Utils::toHex(hex, g_node->self_id.pub_key, 32);
  return "meshcore://contact/add?name=" + urlEncodeName(g_node->name()) + "&public_key=" + hex + "&type=1";
}

class QrView : public View {
public:
  explicit QrView(const String& link) : _link(link) {}
  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    drawHeader(g, "My QR code");
    const int top = L::BODY_Y + 2, h = L::H - top - 2;
    // The code as big as the body allows, on the left; the words beside it.
    const int side = h;
    const int x = 8;
    g.fillRect(x, top, side, side, TFT_WHITE);
    g.qrcode(_link.c_str(), x, top, side, 1, true);
    const int tx = x + side + 12, tw = L::W - tx - 6;
    g.setFont(&fonts::Font2);
    g.setTextColor(t.txt, t.bg);
    int y = top + 6;
    g.setTextColor(t.green, t.bg);
    drawUtf8(g, g_node ? g_node->name() : "", tx, y, tw);     // names can carry emoji
    y += g.fontHeight() + 8;
    y = words(g, "Scan this with the MeshCore app to add you as a contact.", tx, y, tw, t.txt);
    y += 6;
    words(g, "Won't scan? Turn the brightness up.", tx, y, tw, t.dim);
  }
  void press() override { nav.pop(); }
private:
  String _link;
  // Wraps text into the column at word breaks; returns the next free line's y.
  static int words(Canvas& g, const char* s, int x, int y, int w, uint16_t col) {
    g.setTextColor(col, nav.theme().bg);
    const int lh = g.fontHeight() + 2;
    String line, word;
    auto flush = [&]() { if (line.length()) { g.drawString(line, x, y); y += lh; line = ""; } };
    for (const char* p = s; ; p++) {
      if (*p && *p != ' ') { word += *p; continue; }
      const String trial = line.length() ? line + " " + word : word;
      if (line.length() && g.textWidth(trial) > w) { flush(); line = word; }
      else line = trial;
      word = "";
      if (!*p) break;
    }
    flush();
    return y;
  }
};

inline void showMine() {
  const String link = contactLink();
  if (!link.length()) { nav.toast("not ready yet"); return; }
  nav.push(new QrView(link));
}

}  // namespace qr
