# PlatformIO pre-script: fixes to MeshCore's companion store, applied to copies
# in the build dir so the library itself stays untouched.
#
# DataStore.cpp
#   saveContacts/saveChannels truncated the live file and rewrote it in place, a
#   field at a time. On this 12 MB SPIFFS that is ~7500 tiny writes for 600
#   contacts (over 20 s, all of it on the UI loop), and a freeze or power loss
#   mid-way left a short file. Now the file is built in PSRAM, written to
#   "<name>.tmp" in 4 kB chunks with a progress screen, and only then swapped in.
#   src/dataio.cpp resolves a leftover .tmp at boot.
#
# MyMesh.cpp
#   A contact change scheduled a save 5 s later and every further change pushed
#   it back. Now the first change schedules one save 15 minutes out and later
#   changes join it. app::reboot() flushes a pending save.

import os
import re

Import("env")  # noqa: F821

SAVE_BATCH_MS = 15 * 60 * 1000

HELPER = r'''
// --- INW: buffered, atomic store writes (added by tools/patch_meshcore.py) ---
void inwProgress(const char* what, uint32_t done, uint32_t total);

struct InwBufFile {
  File f;
  const char* what;
  uint8_t* buf = nullptr;
  size_t len = 0, cap = 0;
  bool bad = false;
  InwBufFile(File file, const char* label) : f(file), what(label) {}
  explicit operator bool() { return (bool)f; }
  size_t write(const uint8_t* p, size_t n) {
    if (len + n > cap) {
      size_t nc = cap ? cap * 2 : 32768;
      while (nc < len + n) nc *= 2;
      uint8_t* nb = (uint8_t*)ps_realloc(buf, nc);
      if (!nb) { bad = true; return 0; }
      buf = nb; cap = nc;
    }
    memcpy(buf + len, p, n);
    len += n;
    return n;
  }
  bool close() {
    bool ok = !bad;
    const bool show = len > 16384;
    for (size_t off = 0; ok && off < len; off += 4096) {
      const size_t n = len - off < 4096 ? len - off : 4096;
      ok = f.write(buf + off, n) == n;
      if (show) inwProgress(what, off + n, len);
    }
    f.close();
    free(buf); buf = nullptr;
    return ok;
  }
};

static uint32_t inw_t0;
static void inwCommit(FILESYSTEM* fs, const char* path, bool ok) {
  char tmp[48];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  const uint32_t t1 = millis();
  if (ok) {
    fs->remove(path);
    fs->rename(tmp, path);
  } else {
    fs->remove(tmp);
  }
  inwProgress(nullptr, 0, 0);
  Serial.printf("[save] %s %s: write %lums, swap %lums\n", path, ok ? "ok" : "FAILED",
                (unsigned long)(t1 - inw_t0), (unsigned long)(millis() - t1));
}
'''


def patch_save(src, name, path, label):
    start = src.index("void DataStore::%s(" % name)
    end = src.index("\n}\n", start) + 3
    body = src[start:end]
    body = body.replace('File file = openWrite(_getContactsChannelsFS(), "%s");' % path,
                        'inw_t0 = millis();\n'
                        '  InwBufFile file(openWrite(_getContactsChannelsFS(), "%s.tmp"), "%s");\n'
                        '  bool inw_ok = true;' % (path, label), 1)
    body = body.replace("if (!success) break; // write failed",
                        "if (!success) { inw_ok = false; break; } // write failed")
    body = re.sub(r"file\.close\(\);\n  \}\n\}\n$",
                  'inw_ok = file.close() && inw_ok;\n'
                  '    inwCommit(_getContactsChannelsFS(), "%s", inw_ok);\n  }\n}\n' % path, body)
    return src[:start] + body + src[end:]


def patch_datastore(src):
    anchor = src.index("static File openWrite(")
    anchor = src.index("\n}\n", anchor) + 3
    src = src[:anchor] + HELPER + src[anchor:]
    src = patch_save(src, "saveContacts", "/contacts3", "saving contacts")
    src = patch_save(src, "saveChannels", "/channels2", "saving channels")
    if src.count("inwCommit(") != 3 or src.count("InwBufFile file(") != 2 or src.count("inw_ok = file.close()") != 2:
        raise SystemExit("patch_meshcore.py: DataStore.cpp changed upstream, patch did not apply")
    return src


def patch_mymesh(src):
    src = re.sub(r"#define LAZY_CONTACTS_WRITE_DELAY\s+\d+",
                 "#define LAZY_CONTACTS_WRITE_DELAY       %d" % SAVE_BATCH_MS, src)
    old = "dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);"
    n = src.count(old)
    src = src.replace(old, "if (!dirty_contacts_expiry) " + old)
    if n < 5 or str(SAVE_BATCH_MS) not in src:
        raise SystemExit("patch_meshcore.py: MyMesh.cpp changed upstream, patch did not apply")
    return src


def make_middleware(fname, fn):
    def middleware(env, node):
        src_path = node.srcnode().get_abspath()
        out_dir = os.path.join(env.subst("$BUILD_DIR"), "inw_patched")
        out_path = os.path.join(out_dir, fname)
        src = open(src_path, encoding="utf-8").read().replace("\r\n", "\n")
        src = fn(src)
        os.makedirs(out_dir, exist_ok=True)
        if not os.path.exists(out_path) or open(out_path, encoding="utf-8").read() != src:
            open(out_path, "w", encoding="utf-8").write(src)
        print("patch_meshcore.py: patched %s" % fname)
        return env.File(out_path)
    return middleware


env.AddBuildMiddleware(make_middleware("DataStore.cpp", patch_datastore), "*/companion_radio/DataStore.cpp")  # noqa: F821
env.AddBuildMiddleware(make_middleware("MyMesh.cpp", patch_mymesh), "*/companion_radio/MyMesh.cpp")  # noqa: F821
