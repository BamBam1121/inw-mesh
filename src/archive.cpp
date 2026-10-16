#include "archive.h"
#include <SD.h>
#include <time.h>
#include "dataio.h"
#include "regional.h"

namespace archive {

static constexpr int QMAX = 48;              // ~10 kB of PSRAM
static constexpr int FLUSH_AT = 40;          // this full: write now, screen on or not
static HistMsg* s_q = nullptr;
static int s_n = 0;
static const char* DIR = "/inw/msgs";

static void pathFor(const ConvKey& k, char* out, size_t cap) {
  snprintf(out, cap, "%s/%c%02x%02x%02x%02x%02x%02x.txt", DIR, k.type == CONV_CHANNEL ? 'g' : 'c',
           k.id[0], k.id[1], k.id[2], k.id[3], k.id[4], k.id[5]);
}

// Tabs and line breaks would split a record: they become spaces.
static void flat(const char* in, char* out, size_t cap) {
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 1 < cap; i++) out[j++] = (in[i] == '\t' || in[i] == '\n' || in[i] == '\r') ? ' ' : in[i];
  out[j] = 0;
}

void push(const HistMsg& m) {
  if (!m.conv.type) return;                  // a cleared chat's message: let it go
  if (!s_q) s_q = (HistMsg*)ps_malloc(sizeof(HistMsg) * QMAX);
  if (!s_q) return;
  if (s_n == QMAX) {                         // the card hasn't taken the last lot: drop the oldest
    memmove(s_q, s_q + 1, sizeof(HistMsg) * (QMAX - 1));
    s_n--;
  }
  s_q[s_n++] = m;
}

// One line a message: time, 'o' ours / 'i' received, sender, text.
static void writeAll() {
  if (!s_n || !sdMount()) return;
  if (!SD.exists(DIR)) { SD.mkdir("/inw"); SD.mkdir(DIR); }
  char path[40], sender[sizeof(HistMsg::sender)], text[sizeof(HistMsg::text)];
  int done = 0;
  while (done < s_n) {
    const ConvKey k = s_q[done].conv;
    pathFor(k, path, sizeof(path));
    File f = SD.open(path, FILE_APPEND);
    if (!f) break;                           // the card went: try again next time
    // Every queued message of this chat in one open, in order.
    for (int i = done; i < s_n; i++) {
      if (!(s_q[i].conv == k)) continue;
      flat(s_q[i].sender, sender, sizeof(sender));
      flat(s_q[i].text, text, sizeof(text));
      f.printf("%lu\t%c\t%s\t%s\n", (unsigned long)s_q[i].ts, (s_q[i].flags & HF_OUT) ? 'o' : 'i', sender, text);
      s_q[i].conv.type = 0;                  // written
    }
    f.close();
    // Close up what has been written.
    int w = 0;
    for (int i = 0; i < s_n; i++) if (s_q[i].conv.type) s_q[w++] = s_q[i];
    s_n = w;
    done = 0;
  }
}

void tick(bool screenDark) {
  if (!s_n) return;
  if (screenDark || s_n >= FLUSH_AT) writeAll();
}

bool has(const ConvKey& k) {
  for (int i = 0; i < s_n; i++) if (s_q[i].conv == k) return true;
  if (!sdMount()) return false;
  char path[40];
  pathFor(k, path, sizeof(path));
  return SD.exists(path);
}

static void wrap(const char* text, int cols, std::vector<String>& out) {
  const int n = strlen(text);
  int i = 0;
  while (i < n) {
    int end = min(n, i + cols);
    if (end < n) {                           // break at the last space that fits
      int sp = end;
      while (sp > i && text[sp] != ' ') sp--;
      if (sp > i) end = sp;
    }
    out.push_back(String("  ") + String(text + i).substring(0, end - i));
    i = end;
    while (i < n && text[i] == ' ') i++;
  }
}

void read(const ConvKey& k, std::vector<String>& out, int max, int cols) {
  writeAll();                                // whatever is still queued, so it shows too
  out.clear();
  char path[40];
  pathFor(k, path, sizeof(path));
  if (!sdMount()) { out.push_back("#no sd card"); return; }
  File f = SD.open(path, FILE_READ);
  if (!f) return;
  // Only the end of the file: a chat can hold years of messages.
  const size_t TAIL = 96 * 1024;
  const size_t size = f.size();
  if (size > TAIL) { f.seek(size - TAIL); f.readStringUntil('\n'); }   // a partial first line
  std::vector<String> lines;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    if (l.length()) lines.push_back(l);
  }
  f.close();
  int shown = 0;
  for (int i = (int)lines.size() - 1; i >= 0 && shown < max; i--, shown++) {
    const String& l = lines[i];
    const int a = l.indexOf('\t'), b = l.indexOf('\t', a + 1), c = l.indexOf('\t', b + 1);
    if (a < 0 || b < 0 || c < 0) continue;
    const time_t ts = (time_t)strtoul(l.substring(0, a).c_str(), nullptr, 10);
    const bool ours = l[a + 1] == 'o';
    char when[24] = "";
    if (ts > 1735689600) {                   // local time, as everywhere else on the device
      const time_t lt = ts + (time_t)regional::offsetMin((uint32_t)ts) * 60;
      struct tm tm;
      gmtime_r(&lt, &tm);
      strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &tm);
    }
    out.push_back(String("#") + when + "  " + (ours ? String("you") : l.substring(b + 1, c)));
    wrap(l.substring(c + 1).c_str(), cols, out);
  }
  if (!shown) out.push_back("#nothing on the card for this chat");
}

void forget(const ConvKey& k) {
  for (int i = 0; i < s_n; i++) if (s_q[i].conv == k) s_q[i].conv.type = 0;
  int w = 0;
  for (int i = 0; i < s_n; i++) if (s_q[i].conv.type) s_q[w++] = s_q[i];
  s_n = w;
  if (!sdMount()) return;
  char path[40];
  pathFor(k, path, sizeof(path));
  SD.remove(path);
}

void forgetAll() {
  s_n = 0;
  if (!sdMount()) return;
  File d = SD.open(DIR);
  if (!d) return;
  std::vector<String> names;
  for (File e = d.openNextFile(); e; e = d.openNextFile()) { names.push_back(String(e.name())); e.close(); }
  d.close();
  for (auto& n : names) SD.remove(String(DIR) + "/" + (n.lastIndexOf('/') >= 0 ? n.substring(n.lastIndexOf('/') + 1) : n));
}

}  // namespace archive
