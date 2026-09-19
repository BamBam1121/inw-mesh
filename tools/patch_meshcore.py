# PlatformIO pre-script: fixes to MeshCore's companion store, applied to copies
# in the build dir so the library itself stays untouched.
#
# DataStore.cpp
#   saveContacts/saveChannels truncated the live file and rewrote it in place, a
#   field at a time. On this 12 MB SPIFFS that is ~7500 tiny writes for 600
#   contacts (over 20 s, all of it on the UI loop), and a freeze or power loss
#   mid-way left a short file. Now the file is built in PSRAM (milliseconds) and
#   a background task writes "<name>.tmp" and swaps it in, so the UI never waits
#   on flash. src/dataio.cpp resolves a leftover .tmp at boot.
#
# MyMesh.cpp
#   A contact change scheduled a save 5 s later and every further change pushed
#   it back. Now the first change schedules one save SAVE_BATCH_MS out and later
#   changes join it. It was 6 h while saves froze the UI, which is how contacts
#   heard since the last save got lost to resets; with background writes it is
#   2 minutes. app::reboot(), flash mode and the USB "save" flush it.

import os
import re

Import("env")  # noqa: F821

SAVE_BATCH_MS = 2 * 60 * 1000

HELPER = r'''
// --- INW: buffered, atomic store writes, off the UI loop (tools/patch_meshcore.py) ---
// The save builds the whole file in PSRAM on the caller's thread (a memcpy per
// record, milliseconds even for 2000 contacts) and hands the buffer to a
// background task that writes "<name>.tmp", then swaps it in. The UI never waits
// on flash, so contacts can be saved minutes after a change instead of hours.
// Latest wins: a newer save of the same file replaces one still queued.
// inwStoreFlush() waits for everything to land (reboot, flash mode, backups);
// inwStoreTick() runs on the loop and reports finished saves (logs aren't
// thread-safe, so the task never touches them).
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
void inwStoreSaved(const char* path, size_t bytes);

struct InwJob { FILESYSTEM* fs; char path[24]; uint8_t* buf; size_t len; };
static InwJob inw_q[2];                 // one queued save per file
static volatile bool inw_busy = false;  // a save is being written right now
static SemaphoreHandle_t inw_mx = nullptr;
static TaskHandle_t inw_task = nullptr;
struct InwDone { char path[24]; size_t bytes; bool ok; };
static InwDone inw_done[4];
static volatile uint8_t inw_done_n = 0;

// Flash writes stall BOTH cores for a moment (the cache is off while flash is
// written), so even a background write is felt as small hitches. The loop tells
// us when someone is actively using the pager, and writes wait for a lull -
// never longer than a minute, so a save can't be held off forever.
static volatile bool inw_user_busy = false;
void inwSetUserBusy(bool busy) { inw_user_busy = busy; }
static void inwWaitForLull() {
  for (uint32_t t0 = millis(); inw_user_busy && millis() - t0 < 60000;) vTaskDelay(pdMS_TO_TICKS(100));
}

// Advert blobs (MeshCore keeps each contact's last raw advert in /bl/<key> to
// share or export it). It rewrote that file on the UI loop for EVERY advert
// heard, and on this SPIFFS a file open by name scans the whole partition: the
// 200-300 ms "msh" stalls in the slow log on a busy mesh. Queue them here
// instead, latest wins per contact; reads check the queue first.
struct InwBlob { char path[24]; uint8_t len; uint8_t data[255]; bool used; };
static InwBlob* inw_blobs = nullptr;
static const int INW_BLOBS = 24;
static FILESYSTEM* inw_blob_fs = nullptr;

static bool inwQueueBlob(FILESYSTEM* fs, const char* path, const uint8_t* src, uint8_t len);
static int inwQueuedBlob(const char* path, uint8_t* dest);

static void inwWriteJob(InwJob& j) {
  inwWaitForLull();
  char tmp[32];
  snprintf(tmp, sizeof(tmp), "%s.tmp", j.path);
  const uint32_t t0 = millis();
  File f = j.fs->open(tmp, "w", true);
  bool ok = (bool)f;
  for (size_t off = 0; ok && off < j.len; off += 4096) {
    const size_t n = j.len - off < 4096 ? j.len - off : 4096;
    ok = f.write(j.buf + off, n) == n;
    vTaskDelay(pdMS_TO_TICKS(8));       // let the UI run between chunks
    inwWaitForLull();                   // and hold off while someone is using it
  }
  if (f) f.close();
  const uint32_t t1 = millis();
  if (ok) { j.fs->remove(j.path); j.fs->rename(tmp, j.path); }
  else j.fs->remove(tmp);
  Serial.printf("[save] %s %s: %u bytes, write %lums, swap %lums (background)\n", j.path, ok ? "ok" : "FAILED",
                (unsigned)j.len, (unsigned long)(t1 - t0), (unsigned long)(millis() - t1));
  xSemaphoreTake(inw_mx, portMAX_DELAY);
  if (inw_done_n < 4) { InwDone& d = inw_done[inw_done_n++]; strlcpy(d.path, j.path, sizeof(d.path)); d.bytes = j.len; d.ok = ok; }
  xSemaphoreGive(inw_mx);
  free(j.buf);
}

static void inwStoreTask(void*) {
  static InwBlob blob;                  // one being written, copied out of the queue
  for (;;) {
    InwJob job = {};
    bool haveBlob = false;
    xSemaphoreTake(inw_mx, portMAX_DELAY);
    for (auto& q : inw_q) if (q.buf) { job = q; q = InwJob{}; break; }
    if (!job.buf && inw_blobs)
      for (int i = 0; i < INW_BLOBS; i++) if (inw_blobs[i].used) { blob = inw_blobs[i]; haveBlob = true; break; }
    inw_busy = job.buf != nullptr || haveBlob;
    xSemaphoreGive(inw_mx);
    if (job.buf) { inwWriteJob(job); inw_busy = false; continue; }
    if (haveBlob) {
      inwWaitForLull();
      File f = inw_blob_fs->open(blob.path, "w", true);
      const bool ok = f && f.write(blob.data, blob.len) == blob.len;
      if (f) f.close();
      if (!ok) inw_blob_fs->remove(blob.path);
      // Clear the slot only if nothing newer replaced it while we wrote.
      xSemaphoreTake(inw_mx, portMAX_DELAY);
      for (int i = 0; i < INW_BLOBS; i++)
        if (inw_blobs[i].used && !strcmp(inw_blobs[i].path, blob.path) &&
            inw_blobs[i].len == blob.len && !memcmp(inw_blobs[i].data, blob.data, blob.len)) inw_blobs[i].used = false;
      inw_busy = false;
      xSemaphoreGive(inw_mx);
      continue;
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
  }
}

static void inwStartTask() {
  if (!inw_mx) inw_mx = xSemaphoreCreateMutex();
  if (!inw_task) xTaskCreatePinnedToCore(inwStoreTask, "inw_store", 6144, nullptr, 1, &inw_task, 0);
}

static bool inwQueueBlob(FILESYSTEM* fs, const char* path, const uint8_t* src, uint8_t len) {
  inwStartTask();
  xSemaphoreTake(inw_mx, portMAX_DELAY);
  if (!inw_blobs) inw_blobs = (InwBlob*)ps_calloc(INW_BLOBS, sizeof(InwBlob));
  InwBlob* slot = nullptr;
  if (inw_blobs) {
    for (int i = 0; i < INW_BLOBS && !slot; i++) if (inw_blobs[i].used && !strcmp(inw_blobs[i].path, path)) slot = &inw_blobs[i];
    for (int i = 0; i < INW_BLOBS && !slot; i++) if (!inw_blobs[i].used) slot = &inw_blobs[i];
    if (slot) {
      inw_blob_fs = fs;
      strlcpy(slot->path, path, sizeof(slot->path));
      memcpy(slot->data, src, len); slot->len = len; slot->used = true;
    }
  }
  xSemaphoreGive(inw_mx);
  if (!slot) return false;              // queue full: caller writes it directly, as before
  xTaskNotifyGive(inw_task);
  return true;
}

// Forgetting a contact deletes its blob; a queued write must not bring it back.
static void inwDropBlob(const char* path) {
  if (!inw_mx || !inw_blobs) return;
  xSemaphoreTake(inw_mx, portMAX_DELAY);
  for (int i = 0; i < INW_BLOBS; i++) if (inw_blobs[i].used && !strcmp(inw_blobs[i].path, path)) inw_blobs[i].used = false;
  xSemaphoreGive(inw_mx);
}

static int inwQueuedBlob(const char* path, uint8_t* dest) {
  if (!inw_mx || !inw_blobs) return -1;
  int len = -1;
  xSemaphoreTake(inw_mx, portMAX_DELAY);
  for (int i = 0; i < INW_BLOBS; i++)
    if (inw_blobs[i].used && !strcmp(inw_blobs[i].path, path)) { memcpy(dest, inw_blobs[i].data, inw_blobs[i].len); len = inw_blobs[i].len; break; }
  xSemaphoreGive(inw_mx);
  return len;
}

static void inwSubmit(FILESYSTEM* fs, const char* path, uint8_t* buf, size_t len) {
  inwStartTask();
  xSemaphoreTake(inw_mx, portMAX_DELAY);
  InwJob* slot = nullptr;
  for (auto& q : inw_q) if (q.buf && !strcmp(q.path, path)) slot = &q;       // replace a queued one
  if (!slot) for (auto& q : inw_q) if (!q.buf) { slot = &q; break; }
  if (slot) {
    if (slot->buf) free(slot->buf);
    slot->fs = fs; strlcpy(slot->path, path, sizeof(slot->path)); slot->buf = buf; slot->len = len;
  }
  xSemaphoreGive(inw_mx);
  if (!slot) { free(buf); Serial.printf("[save] %s dropped: queue full\n", path); return; }
  xTaskNotifyGive(inw_task);
}

// Everything queued is on flash when this returns true.
bool inwStoreFlush(uint32_t ms) {
  if (!inw_mx) return true;
  const uint32_t t0 = millis();
  for (;;) {
    xSemaphoreTake(inw_mx, portMAX_DELAY);
    bool idle = !inw_busy && !inw_q[0].buf && !inw_q[1].buf;
    if (inw_blobs) for (int i = 0; i < INW_BLOBS; i++) if (inw_blobs[i].used) idle = false;
    xSemaphoreGive(inw_mx);
    inw_user_busy = false;               // a flush means "now": don't wait for a lull
    if (idle) return true;
    if (millis() - t0 > ms) return false;
    delay(20);
  }
}

// Called from the loop: reports saves the task has finished.
void inwStoreTick() {
  if (!inw_mx || !inw_done_n) return;
  InwDone done[4]; uint8_t n;
  xSemaphoreTake(inw_mx, portMAX_DELAY);
  n = inw_done_n; memcpy(done, inw_done, sizeof(InwDone) * n); inw_done_n = 0;
  xSemaphoreGive(inw_mx);
  for (uint8_t i = 0; i < n; i++) if (done[i].ok) inwStoreSaved(done[i].path, done[i].bytes);
}

// Reads a store file into PSRAM in one go and serves the loader's many small
// field reads from memory: 1093 contacts are ~13,000 reads, each a trip through
// SPIFFS, which made loading contacts a large part of the boot. If PSRAM is
// short it falls back to reading the file directly, exactly as before.
struct InwReadFile {
  File f;
  uint8_t* buf = nullptr;
  size_t len = 0, pos = 0;
  explicit InwReadFile(File file) : f(file) {
    if (!f) return;
    len = f.size();
    buf = len ? (uint8_t*)ps_malloc(len) : nullptr;
    if (buf && f.read(buf, len) == len) { f.close(); return; }
    free(buf); buf = nullptr; f.seek(0);          // couldn't: read it the old way
  }
  ~InwReadFile() { free(buf); }
  explicit operator bool() { return buf || (bool)f; }
  size_t read(uint8_t* p, size_t n) {
    if (!buf) return f.read(p, n);
    if (pos >= len) return 0;
    if (n > len - pos) n = len - pos;
    memcpy(p, buf + pos, n); pos += n;
    return n;
  }
  size_t position() { return buf ? pos : f.position(); }
  bool seek(size_t p) { if (!buf) return f.seek(p); if (p > len) return false; pos = p; return true; }
  size_t size() { return buf ? len : f.size(); }
  void close() { if (buf) { free(buf); buf = nullptr; } else f.close(); }
};

struct InwBufFile {
  const char* path;
  uint8_t* buf = nullptr;
  size_t len = 0, cap = 0;
  bool bad = false;
  explicit InwBufFile(const char* p) : path(p) {}
  explicit operator bool() { return true; }
  size_t write(const uint8_t* p, size_t n) {
    if (bad) return 0;
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
  // Hands the finished file to the background writer, or throws it away.
  bool submit(FILESYSTEM* fs, bool ok) {
    if (ok && !bad && !buf) buf = (uint8_t*)malloc(1);   // nothing to save is still a save: an empty file
    ok = ok && !bad && buf;
    if (ok) inwSubmit(fs, path, buf, len);
    else { free(buf); Serial.printf("[save] %s not written: building it failed\n", path); }
    buf = nullptr;
    return ok;
  }
};
'''


def patch_save(src, name, path, label):
    start = src.index("void DataStore::%s(" % name)
    end = src.index("\n}\n", start) + 3
    body = src[start:end]
    body = body.replace('File file = openWrite(_getContactsChannelsFS(), "%s");' % path,
                        'InwBufFile file("%s");\n'
                        '  bool inw_ok = true;' % path, 1)
    body = body.replace("if (!success) break; // write failed",
                        "if (!success) { inw_ok = false; break; } // write failed")
    body = re.sub(r"file\.close\(\);\n  \}\n\}\n$",
                  'file.submit(_getContactsChannelsFS(), inw_ok);\n  }\n}\n', body)
    return src[:start] + body + src[end:]


def patch_load(src):
    """A short read before the end of /contacts3 used to end the load as if it were
    EOF. The shortened list then got saved back, losing every contact after the
    hiccup. Retry the record, then skip only that record, and stop at the real end."""
    start = src.index("void DataStore::loadContacts(")
    end = src.index("\n}\n", start) + 3
    body = src[start:end]
    old_loop = "      bool full = false;\n      while (!full) {\n"
    old_eof = "        if (!success) break; // EOF\n"
    old_open = 'File file = openRead(_getContactsChannelsFS(), "/contacts3");'
    if body.count(old_loop) != 1 or body.count(old_eof) != 1 or body.count(old_open) != 1:
        raise SystemExit("patch_meshcore.py: DataStore::loadContacts changed upstream, patch did not apply")
    body = body.replace(old_open, 'InwReadFile file(openRead(_getContactsChannelsFS(), "/contacts3"));')
    body = body.replace(old_loop,
                        "      bool full = false;\n"
                        "      uint8_t inw_retries = 0;\n"
                        "      while (!full) {\n"
                        "        const size_t inw_rec = file.position();\n")
    body = body.replace(old_eof,
                        "        if (!success) {\n"
                        "          if (inw_rec + 152 <= file.size() && inw_retries++ < 3) { file.seek(inw_rec); continue; }\n"
                        "          if (inw_rec + 152 <= file.size()) {\n"
                        "            Serial.printf(\"[store] unreadable contact record at %u, skipped\\n\", (unsigned)inw_rec);\n"
                        "            inw_retries = 0; file.seek(inw_rec + 152); continue;\n"
                        "          }\n"
                        "          break; // the real end of the file\n"
                        "        }\n"
                        "        inw_retries = 0;\n")
    return src[:start] + body + src[end:]


def patch_datastore(src):
    anchor = src.index("static File openWrite(")
    anchor = src.index("\n}\n", anchor) + 3
    src = src[:anchor] + HELPER + src[anchor:]
    src = patch_save(src, "saveContacts", "/contacts3", "saving contacts")
    src = patch_save(src, "saveChannels", "/channels2", "saving channels")
    src = patch_load(src)
    # Advert blobs (ESP32 branch: one file per contact under /bl): writes go to
    # the background writer; reads see a queued write before the file.
    old_put = "  File f = openWrite(_fs, path);\n  if (f) {\n    int n = f.write(src_buf, len);"
    old_get = "  if (_fs->exists(path)) {\n    File f = openRead(_fs, path);"
    if src.count(old_put) != 1 or src.count(old_get) != 1:
        raise SystemExit("patch_meshcore.py: DataStore blob functions changed upstream, patch did not apply")
    src = src.replace(old_put, "  if (inwQueueBlob(_fs, path, src_buf, len)) return true;   // INW: written in the background\n" + old_put)
    src = src.replace(old_get, "  { const int q = inwQueuedBlob(path, dest_buf); if (q >= 0) return (uint8_t)q; }   // INW: not on flash yet\n" + old_get)
    d = src.index("bool DataStore::deleteBlobByKey(", src.index(old_put))
    old_del = "  _fs->remove(path);"
    e = src.index(old_del, d)
    src = src[:e] + "  inwDropBlob(path);   // INW: a queued write would recreate it\n" + src[e:]
    if src.count("file.submit(") != 2 or src.count("InwBufFile file(") != 2 or src.count("inw_ok = false; break;") != 2:
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
    # (Saves used to be held until the screen was off, because they froze the UI.
    # They are written in the background now, so they go when they come due.)
    if "if (dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry)) {" not in src:
        raise SystemExit("patch_meshcore.py: MyMesh.cpp lazy save changed upstream, patch did not apply")
    # The device-info frame carries MAX_CONTACTS / 2 in one byte; past 510 it
    # wraps (1000 read as 488, 2000 as 464). Cap it instead.
    old = "out_frame[i++] = MAX_CONTACTS / 2;"
    if old in src:
        src = src.replace(old, "out_frame[i++] = (MAX_CONTACTS / 2) > 255 ? 255 : (MAX_CONTACTS / 2);")
    # Bluetooth pairing PIN. Without MeshCore's DISPLAY_CLASS (our UI is our own),
    # every pager fell back to the well-known 123456, so anyone in radio range could
    # pair and read or send messages. Give each pager its own random PIN the first
    # time, saved so a paired phone keeps working; Settings > Bluetooth shows it.
    old = "_active_ble_pin = BLE_PIN_CODE; // otherwise static pin"
    if src.count(old) != 2:
        raise SystemExit("patch_meshcore.py: MyMesh.cpp BLE pin code changed upstream, patch did not apply")
    src = src.replace(old, "_prefs.ble_pin = 100000 + (esp_random() % 900000); savePrefs(); "
                           "_active_ble_pin = _prefs.ble_pin; // INW: per-pager random PIN, not 123456")
    # Path hash size defaults to 2 bytes (mode 1). MeshCore's default is 1 byte,
    # which collides on a mesh this size. Only the default: a saved choice, or one
    # made from the phone app, still wins when prefs load over it.
    old = "_prefs.tx_power_dbm = LORA_TX_POWER;"
    if src.count(old) != 1:
        raise SystemExit("patch_meshcore.py: MyMesh.cpp prefs defaults changed upstream, patch did not apply")
    src = src.replace(old, old + " _prefs.path_hash_mode = 1; // INW: 2-byte path hashes")
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
