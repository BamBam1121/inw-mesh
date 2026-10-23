// The worm on the card (sdguard.h): find it and delete it, straight through FatFS so
// the worm's read-only / hidden / system marks can be taken off first.
#include "sdguard.h"
#include <ff.h>
#include <vector>

namespace sdguard {

static bool endsWith(const char* s, const char* ext) {
  const size_t n = strlen(s), e = strlen(ext);
  return n >= e && !strcasecmp(s + n - e, ext);
}

// What Windows would run from a card. Only these are ever deleted, and only when the
// autorun.inf names them (or a .pif, which nothing legitimate uses any more).
static bool runnable(const char* name) {
  static const char* EXT[] = {".exe", ".pif", ".scr", ".com", ".bat", ".cmd", ".vbs", ".vbe", ".js", ".jse", ".wsf", ".lnk"};
  for (const char* e : EXT) if (endsWith(name, e)) return true;
  return false;
}

// The card's FatFS drive: the only FAT volume this firmware mounts (the flash store is SPIFFS).
static int drive() {
  for (int d = 0; d < FF_VOLUMES; d++) {
    char p[4] = {(char)('0' + d), ':', '/', 0};
    FF_DIR dir;
    if (f_opendir(&dir, p) == FR_OK) { f_closedir(&dir); return d; }
  }
  return -1;
}

static void addName(Found& f, const char* name) {
  const size_t n = strlen(f.names);
  if (n + strlen(name) + 3 >= sizeof(f.names)) {          // no room: say there were more
    if (n && !endsWith(f.names, "...")) strlcat(f.names, ", ...", sizeof(f.names));
    return;
  }
  if (n) strlcat(f.names, ", ", sizeof(f.names));
  strlcat(f.names, name, sizeof(f.names));
}

static void deleteFile(int d, const String& name, Found& f) {
  char p[300];
  snprintf(p, sizeof(p), "%d:/%s", d, name.c_str());
  FILINFO fi;
  if (f_stat(p, &fi) != FR_OK || (fi.fattrib & AM_DIR)) return;   // gone, or a folder: leave it
  f_chmod(p, 0, AM_RDO | AM_HID | AM_SYS);                         // the worm hides its files and locks them
  const FRESULT r = f_unlink(p);
  if (r == FR_OK) { f.removed++; addName(f, fi.fname); }
  else { f.failed++; Serial.printf("[sdguard] couldn't delete %s (%d)\n", name.c_str(), (int)r); }
}

static void addOnce(std::vector<String>& v, const String& s) {
  for (const String& x : v) if (x.equalsIgnoreCase(s)) return;
  v.push_back(s);
}

// The programs an autorun.inf starts: open=, shellexecute= and shell\...\command=,
// whatever their case (the worm mixes it) and among whatever junk lines it pads them with.
// Returns false if it starts nothing (a harmless one, a label or an icon, is kept).
static bool readAutorun(int d, const char* name, std::vector<String>& doomed) {
  char p[300];
  snprintf(p, sizeof(p), "%d:/%s", d, name);
  FIL fp;
  if (f_open(&fp, p, FA_READ) != FR_OK) return false;
  constexpr UINT MAX = 32 * 1024;
  const UINT size = min((UINT)f_size(&fp), MAX);
  char* buf = (char*)malloc(size + 1);
  UINT got = 0;
  if (buf) f_read(&fp, buf, size, &got);
  f_close(&fp);
  if (!buf) return false;
  buf[got] = 0;
  bool starts = false;
  for (char* line = strtok(buf, "\r\n"); line; line = strtok(nullptr, "\r\n")) {
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    String key(line), val(eq + 1);
    key.trim(); key.toLowerCase(); val.trim();
    if (!(key == "open" || key == "shellexecute" || (key.startsWith("shell") && key.endsWith("command")))) continue;
    starts = true;
    // The program: up to a space or the closing quote, and from the card's top.
    if (val.startsWith("\"")) { val = val.substring(1); const int q = val.indexOf('"'); if (q >= 0) val = val.substring(0, q); }
    else { const int sp = val.indexOf(' '); if (sp >= 0) val = val.substring(0, sp); }
    val.replace('\\', '/');
    while (val.startsWith("./") || val.startsWith("/")) val = val.substring(val.startsWith("/") ? 1 : 2);
    if (!val.length() || val.indexOf("..") >= 0 || val.indexOf(':') >= 0) continue;   // only files on this card
    if (runnable(val.c_str())) addOnce(doomed, val);
  }
  free(buf);
  return starts;
}

Found sweep() {
  Found f;
  const int d = drive();
  if (d < 0) return f;
  char root[4] = {(char)('0' + d), ':', '/', 0};
  std::vector<String> doomed;
  String autorun;
  FF_DIR dir;
  FILINFO fi;
  if (f_opendir(&dir, root) != FR_OK) return f;
  while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
    if (fi.fattrib & AM_DIR) continue;                    // a folder called autorun.inf is a PC's vaccine: keep it
    if (!strcasecmp(fi.fname, "autorun.inf")) autorun = fi.fname;
    else if (endsWith(fi.fname, ".pif")) addOnce(doomed, fi.fname);
  }
  f_closedir(&dir);
  if (autorun.length() && readAutorun(d, autorun.c_str(), doomed)) addOnce(doomed, autorun);
  for (const String& n : doomed) deleteFile(d, n, f);
  return f;
}

}  // namespace sdguard
