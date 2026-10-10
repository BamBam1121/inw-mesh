/* One button. It asks the pager what is on it, picks update or first install,
   saves the pager's data first, writes, checks it came back up, and retries the
   known failures on its own.

   Built on esptool-js (Espressif's own, vendored in assets/vendor) - the same
   engine esp-web-tools uses, so the writing itself is as proven as before.

   Holding the serial port ourselves is what makes the rest possible:
   - the pager is asked to SAVE before the reset, so a flash can't lose recently
     heard contacts (esp-web-tools just pulls the reset line; that is how 65 went
     missing here once);
   - the pager says which firmware and which radio it has, so the right kind of
     install is chosen without asking anyone;
   - its boot output is read straight afterwards on the same port, no second
     permission prompt, so "done" means the pager itself said so.

   Rules, learned the hard way:
   - eraseAll is NEVER set. A full erase takes this board's bootloader with it,
     and ours is the only one that boots our partition table.
   - parts are written in manifest order, boot pointer before app: the erase for
     a small write rounds up into whatever follows it.
   - DTR/RTS are left to esptool-js except for the one restart after writing,
     which esptool-js 0.6.1 gets wrong (see writeIt). */
import { ESPLoader, Transport } from "./vendor/esptool-0.6.1.js";

const panel = document.getElementById("flasher");
/* Which device this page flashes. The pager's page sets nothing; another board's
   page says on the panel: data-device (what the words call it), data-board (what
   its firmware reports as board= in [status]), data-full-name, and its own
   data-install / data-update manifests. Pager firmware has never sent board=. */
const CFG = (panel && panel.dataset) || {};
const DEVICE = CFG.device || "pager";
const BOARD = CFG.board || "t-lora-pager";
const FULL_NAME = CFG.fullName || "LilyGo T-Lora Pager";
const BOARD_NAMES = { "t-lora-pager": "T-Lora Pager", "t-deck": "T-Deck" };
const UPDATE_MANIFEST = CFG.update || "https://bambam1121.github.io/inw-mesh/manifest-update.json";
const INSTALL_MANIFEST = CFG.install || "https://bambam1121.github.io/inw-mesh/manifest-install.json";
// Built-in PSRAM the chip must report (eFuses) before anything is written: "8MB"
// on the T-Deck's page. The pager's page sets nothing.
const NEED_PSRAM = CFG.needPsram || "";
// The other way round, on the pager's page: built-in PSRAM that means it is a
// different board (data-refuse-psram="8MB" is a T-Deck's chip), what to call that
// board, and where its installer is. A T-Deck on other firmware was once given the
// pager's (2026-10-01): it says nothing to "status", so only its chip can tell.
const REFUSE_PSRAM = CFG.refusePsram || "";
const REFUSE_NAME = CFG.refuseName || "T-Deck";
const REFUSE_LINK = CFG.refuseLink || "/t-deck";
// How to put the chip into its USB loader by hand, per board.
const LOADER_HOW = BOARD === "t-deck"
  ? "turn the T-Deck off, hold the trackball down while you switch it back on, then let go"
  : "hold BOOT, tap RESET, let go of BOOT";
const SUPPORTED = !!(navigator.serial && window.isSecureContext);
// The messages are written about the pager; other boards swap in their own name.
// (Not inside a board id such as "t-lora-pager".)
const named = (t) => (DEVICE === "pager" ? t : String(t).replace(/(^|[^-\w])pager\b/g, "$1" + DEVICE));
const startBtn = document.getElementById("qs-start");
// Start is hidden until it has been proven on a pager; ?flashtest shows it for testing.
if (/[?&]flashtest\b/.test(location.search)) { const q = document.getElementById("quickstart"); if (q) q.hidden = false; }

if (panel) {
  const head = panel.querySelector(".fl-head");
  const note = panel.querySelector(".fl-note");
  const bar = panel.querySelector(".fl-bar span");
  const logBox = panel.querySelector(".fl-log");
  const logPre = panel.querySelector(".fl-log pre");
  const again = panel.querySelector(".fl-again");
  /* After a refusal (wrong board by its firmware, or by its chip), the person can
     still say "it really is one" - the way back for a device that got the wrong
     firmware, and for a unit whose chip isn't the one we expect. Made here so the
     pages don't each need it. */
  const anywayBtn = document.createElement("button");
  anywayBtn.type = "button";
  anywayBtn.className = "btn small fl-anyway";
  anywayBtn.hidden = true;
  again.insertAdjacentElement("afterend", anywayBtn);
  let anyway = false;           // true for one run after "install anyway"
  const offerAnyway = () => {
    anywayBtn.textContent = "It's a " + DEVICE + ": install anyway";
    anywayBtn.hidden = false;
  };

  let busy = false;
  let port = null;            // kept between attempts so a retry doesn't re-prompt
  // Watched from page load so a reconnect during a pause can't be missed:
  // last = the port object the pager most recently appeared as; gone = the
  // port that most recently dropped off USB.
  const usb = { last: null, gone: null };
  if (navigator.serial) {
    navigator.serial.addEventListener("connect", (e) => { usb.last = e.target; });
    navigator.serial.addEventListener("disconnect", (e) => { usb.gone = e.target; });
  }
  let lastKind = "auto";
  let reached = false;            // this run: the chip has answered at least once

  /* ---- starting fresh -------------------------------------------------------------
     Two tick boxes under START: "start fresh" (contacts, channels and messages go;
     keys, name and Wi-Fi stay) and "reset everything". Nothing is erased from here:
     once the firmware is on and has answered, it is sent one line and clears itself
     on its next start, before its radio or Wi-Fi are running. Older firmware doesn't
     know the line, so the boxes only show once the firmware this page installs does
     (data-wipe-from on the box; ?wipetest shows them regardless). */
  const optsBox = document.getElementById("qs-opts");
  const freshBox = document.getElementById("qs-fresh");
  const resetBox = document.getElementById("qs-reset");
  let latest = "";              // the version this page installs, read when it loads
  let wipe = "";                // "", "keep-keys" or "everything": this run and its retries
  // 1 if a is newer than b, -1 if older, 0 the same. A beta is older than its release.
  function cmpVersion(a, b) {
    const parse = (v) => {
      const m = String(v).match(/^v?(\d+)\.(\d+)\.(\d+)(?:-\D*(\d+))?/);
      return m ? [+m[1], +m[2], +m[3], m[4] === undefined ? Infinity : +m[4]] : null;
    };
    const x = parse(a), y = parse(b);
    if (!x || !y) return -1;
    for (let i = 0; i < 4; i++) if (x[i] !== y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
  }
  const ticked = () => (!optsBox || optsBox.hidden) ? ""
    : resetBox && resetBox.checked ? "everything" : freshBox && freshBox.checked ? "keep-keys" : "";
  if (optsBox && SUPPORTED) {
    // A browser can bring a tick back with the page; these two never start ticked.
    for (const b of [freshBox, resetBox]) if (b) b.checked = false;
    if (freshBox && resetBox) {
      freshBox.addEventListener("change", () => { if (freshBox.checked) resetBox.checked = false; });
      resetBox.addEventListener("change", () => { if (resetBox.checked) freshBox.checked = false; });
    }
    fetch(new URL(INSTALL_MANIFEST, location.href).href, { cache: "no-store" })
      .then((r) => (r.ok ? r.json() : null))
      .then((m) => {
        latest = (m && m.version) || "";
        if (/[?&]wipetest\b/.test(location.search) || (latest && cmpVersion(latest, optsBox.dataset.wipeFrom || "") >= 0)) optsBox.hidden = false;
      })
      .catch(() => {});
  }
  // Asked before anything is touched. False: they backed out, and nothing starts.
  function askWipe() {
    const want = ticked();
    if (want === "everything") {
      const typed = window.prompt(named(
        "Reset everything?\n\nThis removes the pager's keys, contacts, channels, messages, settings and saved Wi-Fi. " +
        "It starts as a new device with a new identity, so people will have to add you again.\n\n" +
        "Exports and dated backups on the SD card are left alone. It can't be undone.\n\nType RESET to go ahead."));
      if (!typed || typed.trim().toUpperCase() !== "RESET") return false;
    } else if (want === "keep-keys") {
      if (!window.confirm(named(
        "Start fresh?\n\nThis removes every contact, channel and message from the pager, and its own copies of them on the SD card. " +
        "Your keys, name, radio settings and Wi-Fi are kept.\n\nIt can't be undone."))) return false;
    }
    wipe = want;
    return true;
  }
  const wipeWords = () => (wipe === "everything" ? "reset everything" : "start fresh");

  const show = (title, cls) => { panel.hidden = false; head.textContent = named(title); head.className = "fl-head " + cls; };
  const say = (text) => { note.textContent = named(text); };
  const pct = (n) => { bar.style.width = Math.max(0, Math.min(100, n)) + "%"; };
  function log(line) {
    const s = named(String(line).replace(/\r/g, ""));
    logPre.textContent += s + (s.endsWith("\n") ? "" : "\n");
    logPre.scrollTop = logPre.scrollHeight;
  }
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const isFirefox = /Firefox\//.test(navigator.userAgent);
  const isLinux = /Linux/.test(navigator.userAgent) && !/Android/.test(navigator.userAgent);
  const infoOf = (p) => { try { return p.getInfo() || {}; } catch (e) { return {}; } };
  const sameDevice = (p, info) => {
    const i = infoOf(p);
    return !!info.usbVendorId && i.usbVendorId === info.usbVendorId && i.usbProductId === info.usbProductId;
  };
  // The browser couldn't open the port at all (as opposed to the chip not answering).
  const openFailure = (e) => /\bopen\b|already open|InvalidStateError|NetworkError|device (has been )?lost/i
    .test(((e && e.name) || "") + " " + ((e && e.message) || String(e)));

  /* The pager can drop off USB and come back as a different port object: opening
     the port restarts it on some computers (Macs especially), and it re-appears.
     The old object then fails every open - one person got "Failed to open serial
     port" nine times in a row that way, "try again" included. Find it again by
     its USB ids: the reconnect we saw, or another granted port that matches. */
  async function refindPort(waitMs) {
    const info = infoOf(port);
    const until = Date.now() + (waitMs || 0);
    for (;;) {
      if (usb.last && usb.last !== port && sameDevice(usb.last, info)) {
        port = usb.last;
        log("[flasher] the pager came back as a new port - using that");
        return true;
      }
      let ports = [];
      try { ports = await navigator.serial.getPorts(); } catch (e) { ports = []; }
      const other = ports.find((p) => p !== port && p !== usb.gone && sameDevice(p, info));
      if (other) {
        port = other;
        log("[flasher] found the pager on another port - using that");
        return true;
      }
      if (Date.now() >= until) return false;
      await sleep(250);
    }
  }

  // Ours from an earlier step and somehow still open: let go before esptool opens it.
  async function letGo() {
    if (port && (port.readable || port.writable)) {
      try { await Promise.race([port.close(), sleep(1500)]); } catch (e) {}
    }
  }

  /* esptool-js 0.6 takes each image as a Uint8Array. Older versions took a
     "binary string", and handing 0.6 a string is silent and destructive: the
     plain write sends every byte as zero and still reports success, and the
     compressed write dies part way. That wiped a pager's app slot twice.
     md5() lets esptool-js read back what landed and throw if it differs, so a
     bad write can never be reported as a good one again. */
  function md5(bytes) {
    const K = new Int32Array(64), S = [7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21];
    for (let i = 0; i < 64; i++) K[i] = Math.floor(Math.abs(Math.sin(i + 1)) * 4294967296) | 0;
    const n = bytes.length, words = ((n + 8) >>> 6) + 1 << 4, M = new Int32Array(words);
    for (let i = 0; i < n; i++) M[i >> 2] |= bytes[i] << ((i % 4) * 8);
    M[n >> 2] |= 0x80 << ((n % 4) * 8);
    M[words - 2] = (n * 8) | 0;
    M[words - 1] = Math.floor(n / 0x20000000);
    let a0 = 0x67452301, b0 = 0xefcdab89 | 0, c0 = 0x98badcfe | 0, d0 = 0x10325476;
    for (let o = 0; o < words; o += 16) {
      let a = a0, b = b0, c = c0, d = d0;
      for (let i = 0; i < 64; i++) {
        let f, g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
        else { f = c ^ (b | ~d); g = (7 * i) % 16; }
        const t = d; d = c; c = b;
        const x = (a + f + K[i] + M[o + g]) | 0, s = S[(i >> 4) * 4 + (i % 4)];
        b = (b + ((x << s) | (x >>> (32 - s)))) | 0;
        a = t;
      }
      a0 = (a0 + a) | 0; b0 = (b0 + b) | 0; c0 = (c0 + c) | 0; d0 = (d0 + d) | 0;
    }
    let hex = "";
    for (const v of [a0, b0, c0, d0]) for (let i = 0; i < 4; i++) hex += ((v >>> (i * 8)) & 255).toString(16).padStart(2, "0");
    return hex;
  }
  window.__squatchMd5 = md5;

  const terminal = { clean() {}, writeLine(d) { log(d); }, write(d) { logPre.textContent += String(d).replace(/\r/g, ""); } };

  /* ---- talking to the pager while its own firmware is running ------------------- */

  // Opens the port, sends a line, and collects whatever comes back. Used before the
  // flash (to identify the pager and make it save) and after it (to see it boot).
  // every: send the line again this often until the answer comes (for a device
  // that the open itself restarted, which misses the first one while it starts).
  async function converse(send, mark, ms, every) {
    let text = "";
    try {
      await port.open({ baudRate: 115200, bufferSize: 4096 });
    } catch (e) {
      log("[flasher] couldn't open the port to ask it: " + ((e && e.message) || e));
      return text;                       // busy, or already in the ROM loader
    }
    let reader = null, pending = null;
    try {
      let sentAt = 0;
      const sendLine = async () => {
        const w = port.writable.getWriter();
        try { await Promise.race([w.write(new TextEncoder().encode(send)), sleep(1000)]); }
        finally { try { w.releaseLock(); } catch (e) {} }
        sentAt = Date.now();
      };
      if (send) await sendLine();
      const dec = new TextDecoder();
      reader = port.readable.getReader();
      // ONE outstanding read, reused across timeouts, as in listenForBoot. A fresh
      // read per timeout left the old one pending; whatever arrived next went to
      // it and was lost, so an answer slower than the timeout was never seen. A
      // pager's missed answer once made this page install T-Deck firmware on it.
      const next = (t) => {
        if (!pending) pending = reader.read().then((r) => { pending = null; return r; });
        return Promise.race([pending, sleep(t).then(() => ({ timeout: true }))]);
      };
      const deadline = Date.now() + ms;
      while (Date.now() < deadline) {
        if (send && every && Date.now() - sentAt > every) await sendLine();
        const chunk = await next(500);
        if (chunk.timeout) continue;
        if (!chunk || chunk.done) break;
        text += dec.decode(chunk.value, { stream: true });
        if (mark && (mark.test ? mark.test(text) : text.indexOf(mark) >= 0)) break;   // a string, or a pattern
      }
    } catch (e) {
      /* whatever we heard is what we use */
    } finally {
      // Each step on its own: a cancel that throws must not leave the lock held,
      // or close() fails and the port stays open for the write that follows.
      if (reader) {
        try { await Promise.race([reader.cancel(), sleep(1000)]); } catch (e) {}
        try { reader.releaseLock(); } catch (e) {}
      }
      try { await Promise.race([port.close(), sleep(1500)]); } catch (e) {}
    }
    return text;
  }

  /* After the reset, find out whether the pager came up - without getting in its way.
     Learned on the real board, at some cost:
     - while this page holds the port open after a flash, the pager stalls part
       way through booting and only finishes once the port is let go;
     - opening the port resets the pager, and resets part way through boot are
       what corrupted its storage once;
     - it drops off USB at the reset and comes back as a new port.
     So: hold nothing while it boots. Wait for it to reappear, leave it alone
     for BOOT_QUIET_MS so it finishes starting on its own, then open ONCE and
     ask. That open restarts it one more time, from a finished boot, and we
     listen through that start. Never reopen: if the one listen gets nothing,
     the answer is "look at the pager". */
  const BOOT_QUIET_MS = 20000;
  // version: what was just written. If they asked to start fresh, the request goes
  // out on this same open, and only to the firmware we wrote (it is the one known
  // to understand it).
  async function listenForBoot(ms, version) {
    const until = Date.now() + ms;
    const info = (() => { try { return port.getInfo(); } catch (e) { return {}; } })();
    const same = (p) => { try { const i = p.getInfo(); return i.usbVendorId === info.usbVendorId && i.usbProductId === info.usbProductId; } catch (e) { return false; } };
    const before = usb.last;

    // 1. wait (port closed) for the pager to come back on USB
    say("Waiting for the pager to restart. Don't unplug it.");
    const back = Date.now() + 30000;
    while (Date.now() < back && !(usb.last && usb.last !== before && same(usb.last))) await sleep(250);
    if (usb.last && same(usb.last)) port = usb.last;
    log(usb.last !== before ? "[flasher] pager is back on USB" : "[flasher] didn't see the pager reconnect, carrying on");

    // 2. leave it alone while it starts
    const quietEnd = Date.now() + BOOT_QUIET_MS;
    while (Date.now() < quietEnd) {
      say("Letting it start up on its own - " + Math.ceil((quietEnd - Date.now()) / 1000) + " s.");
      await sleep(1000);
    }
    say("Asking the pager how it is.");

    // 3. one open, one listen
    let opened = false;
    while (Date.now() < until && !opened) {
      try { await port.open({ baudRate: 115200, bufferSize: 4096 }); opened = true; }
      catch (e) {
        try { const again = (await navigator.serial.getPorts()).find(same); if (again) port = again; } catch (x) {}
        await sleep(500);                  // a failed open doesn't touch the pager
      }
    }
    if (!opened) { log("[flasher] couldn't open the port to ask"); return ""; }
    log("[flasher] asking the pager");
    let text = "", reader = null, lastAsk = 0, pending = null;
    try {
      const dec = new TextDecoder();
      reader = port.readable.getReader();
      // ONE outstanding read, reused across timeouts; a fresh read per timeout
      // left the old one pending and whatever arrived went to it and was lost.
      const next = (t) => {
        if (!pending) pending = reader.read().then((r) => { pending = null; return r; });
        return Promise.race([pending, sleep(t).then(() => ({ timeout: true }))]);
      };
      const ask = async () => {
        lastAsk = Date.now();
        const w = port.writable.getWriter();
        try { await Promise.race([w.write(new TextEncoder().encode("\nstatus\n")), sleep(1000)]); }
        finally { try { w.releaseLock(); } catch (e) {} }
      };
      while (Date.now() < until && !/\[status\][^\n]*\n/.test(text)) {
        if (Date.now() - lastAsk > 3000) await ask();
        const chunk = await next(500);
        if (chunk.done) break;
        if (chunk.value) text += dec.decode(chunk.value, { stream: true });
      }
      const st = text.match(/\[status\]\s+fw=(\S+)/);
      if (wipe && st && (!version || st[1] === version)) {
        log("[flasher] asking it to " + wipeWords());
        const w = port.writable.getWriter();
        try { await Promise.race([w.write(new TextEncoder().encode("\nwipe-" + wipe + "\n")), sleep(1000)]); }
        finally { try { w.releaseLock(); } catch (e) {} }
        const stop = Date.now() + 8000;
        while (Date.now() < stop && !/\[wipe\] (ok|failed)[^\n]*\n/.test(text)) {
          const chunk = await next(500);
          if (chunk.done) break;
          if (chunk.value) text += dec.decode(chunk.value, { stream: true });
        }
      }
    } catch (e) {
      log("[flasher] lost the port (" + ((e && e.message) || e) + ")");
    } finally {
      try { if (reader) { await Promise.race([reader.cancel(), sleep(1000)]); reader.releaseLock(); } } catch (e) {}
      try { await Promise.race([port.close(), sleep(1000)]); } catch (e) {}
    }
    if (!/\[status\]/.test(text)) log("[flasher] no answer to status");
    return text;
  }

  // What is on this pager? Squatch Mesh answers "status"; anything else stays quiet,
  // which is itself the answer - a pager on other firmware needs a first install.
  async function identify(ms) {
    const text = await converse("\nstatus\n", "[status]", ms || 6000, 2500);
    if (text.trim()) log(text.trim());
    const m = text.match(/\[status\]\s+fw=(\S+)\s+radio=(\S+)\s+radio_ok=(\d)\s+contacts=(-?\d+)/);
    if (!m) return { squatch: false, raw: text };
    const b = text.match(/\[status\][^\n]*\bboard=(\S+)/);
    return { squatch: true, version: m[1], radio: m[2], radioOk: m[3] === "1", contacts: parseInt(m[4], 10),
             board: b ? b[1] : "t-lora-pager", raw: text };
  }

  // Ask it to put contacts, channels and settings on flash before we reset it.
  // Firmware from 1.1.21 answers "[save]"; older builds ignore the line and we
  // carry on - no worse off than the old installer, which never asked at all.
  //
  // It answers "[save] ok" once everything is on flash, which it waits up to 10 s for.
  // With a long contact list the write takes half a minute, so it answers "[save] slow"
  // and carries on writing; that write reports "[save] /contacts3 ok" when it lands.
  // Resetting before then loses whatever changed since the last save, so wait for it.
  // (This used to stop listening after 8 s, before even the "ok" could arrive.)
  async function saveFirst() {
    const t0 = Date.now();
    const done = { test: (text) => {
      if (/\[save\] ok contacts=/.test(text)) return true;
      const slow = text.indexOf("[save] slow");
      if (slow >= 0) return /\[save\] \/contacts3 (ok|FAILED)/.test(text.slice(slow));
      // Talking, but nothing about saving after 15 s: firmware from before "save".
      return Date.now() - t0 > 15000 && text.indexOf("[save]") < 0;
    } };
    const text = await converse("\nsave\n", done, 75000);
    if (text.trim()) log(text.trim().split("\n").filter((l) => l.indexOf("[save]") >= 0).join("\n") || text.trim().slice(-300));
    return text.indexOf("[save]") >= 0;
  }

  /* ---- the flash itself ---------------------------------------------------------- */

  async function fetchParts(manifestUrl) {
    manifestUrl = new URL(manifestUrl, location.href).href;   // a page may give it relative to itself
    const res = await fetch(manifestUrl, { cache: "no-store" });
    if (!res.ok) throw new Error("couldn't fetch the file list (" + res.status + ")");
    const manifest = await res.json();
    const build = (manifest.builds || [])[0];
    if (!build || !build.parts || !build.parts.length) throw new Error("the file list is empty");
    const parts = [];
    for (const p of build.parts) {
      const url = new URL(p.path, manifestUrl).href;
      const r = await fetch(url, { cache: "no-store" });
      if (!r.ok) throw new Error("couldn't download " + p.path + " (" + r.status + ")");
      parts.push({ data: new Uint8Array(await r.arrayBuffer()), address: p.offset, name: p.path.split("/").pop() });
    }
    return { version: manifest.version || "", parts };
  }

  /* Restart the chip into whatever firmware it has. esptool-js 0.6.1's
     after("hard_reset") only RELEASES the reset line (RTS low) without ever
     pulling it, so the chip never restarts: it sits silent in the flasher
     stub until something else toggles the USB lines - which is why the
     pager only ever booted once this page closed the port. This is the
     command-line esptool's hard reset: pull EN low via RTS, wait, release,
     with DTR (the boot-mode pin) left high-level-off throughout. */
  async function restart(transport) {
    await transport.setDTR(false);
    await transport.setRTS(true);
    await sleep(200);
    await transport.setRTS(false);
    await sleep(200);
  }

  async function writeIt(parts, baud, compress) {
    const transport = new Transport(port, false);
    const loader = new ESPLoader({ transport, baudrate: baud, romBaudrate: 115200, terminal, debugLogging: false });
    try {
      const chip = await loader.main();
      reached = true;
      log("[flasher] " + chip + " @ " + baud + (compress ? " compressed" : " uncompressed"));
      /* What the chip itself is, whatever firmware it has (or none): its
         eFuses. A page can require built-in PSRAM (the T-Deck's chip has 8 MB;
         the pager's has none). A T-Lora Pager that didn't answer "status" was
         once taken for a T-Deck on other firmware and given T-Deck firmware. */
      if (NEED_PSRAM && !anyway) {
        let feats = "";
        try { feats = String(await loader.chip.getChipFeatures(loader)); } catch (e) { feats = "unreadable"; }
        if (feats.indexOf("Embedded PSRAM " + NEED_PSRAM) < 0) {
          log("[flasher] chip: " + feats + " - no built-in " + NEED_PSRAM + " PSRAM, so not a " + DEVICE + ". Nothing written.");
          await restart(transport);        // back to the firmware it had
          const e = new Error("wrong hardware");
          e.wrongHardware = feats;
          throw e;
        }
      }
      if (REFUSE_PSRAM && !anyway) {
        let feats = "";
        try { feats = String(await loader.chip.getChipFeatures(loader)); } catch (e) { feats = ""; }   // unreadable: carry on
        if (feats.indexOf("Embedded PSRAM " + REFUSE_PSRAM) >= 0) {
          log("[flasher] chip: " + feats + " - built-in " + REFUSE_PSRAM + " PSRAM, so a " + REFUSE_NAME + ", not a " + DEVICE + ". Nothing written.");
          await restart(transport);        // back to the firmware it had
          const e = new Error("wrong hardware");
          e.wrongHardware = feats;
          e.looksLike = REFUSE_NAME;
          throw e;
        }
      }
      const total = parts.reduce((n, p) => n + p.data.length, 0);
      await loader.writeFlash({
        fileArray: parts.map((p) => ({ data: p.data, address: p.address })),
        flashSize: "keep",
        flashMode: "keep",
        flashFreq: "keep",
        eraseAll: false,               // never: a full erase takes the bootloader with it
        compress: compress,
        calculateMD5Hash: md5,         // read back each part and throw on any mismatch
        reportProgress: (idx, written) => {
          const before = parts.slice(0, idx).reduce((n, p) => n + p.data.length, 0);
          const done = before + written;
          pct((done / total) * 100);
          say("Writing " + parts[idx].name + " — " + Math.round((done / total) * 100) + "%. Keep the cable in.");
        },
      });
      pct(100);
      log("Restarting the pager into the new firmware...");
      await restart(transport);
      return chip;
    } finally {
      try { await transport.disconnect(); } catch (e) {}
    }
  }

  /* ---- the run ------------------------------------------------------------------- */

  async function run(kind) {
    if (busy) return;
    busy = true;
    lastKind = kind;
    again.hidden = true;
    anywayBtn.hidden = true;
    if (startBtn) { startBtn.disabled = true; startBtn.classList.add("working"); startBtn.textContent = "WORKING…"; }
    for (const b of [freshBox, resetBox]) if (b) b.disabled = true;
    logPre.textContent = "";
    pct(0);
    let writing = false;            // until then, a failure has changed nothing on the device
    reached = false;

    try {
      if (!port) {
        show("Pick the pager", "busy");
        say("Your browser is asking which USB device to use. Choose the pager.");
        port = await navigator.serial.requestPort();
      }

      // 1. what is on it
      show("Checking the pager…", "busy");
      say("Asking what firmware it is running.");
      let found = kind === "install" ? { squatch: false } : await identify();
      // Silence isn't proof of other firmware: opening the port restarts some
      // devices, and a Squatch pager that is still starting says nothing. On
      // another board's page, where taking silence at its word puts the wrong
      // firmware on, ask again for longer before deciding.
      if (kind === "auto" && !found.squatch && BOARD !== "t-lora-pager") {
        say("No answer yet. Giving it time to finish starting, then asking again.");
        log("[flasher] no answer to status; asking again");
        if (usb.gone === port) await refindPort(8000);
        found = await identify(20000);
      }
      // Squatch Mesh for another board: its firmware would start on this one's
      // pins and do nothing useful. Stop before anything is written.
      if (found.squatch && found.board !== BOARD && !anyway) {
        const other = BOARD_NAMES[found.board] || found.board;
        log("[flasher] this is a " + other + " (board=" + found.board + "), not a " + (BOARD_NAMES[BOARD] || BOARD) + " - stopped");
        show("That's a " + other + ", not a " + DEVICE, "bad");
        say("It's running Squatch Mesh for the " + other + ". Nothing was written. Use the " + other +
            " installer for it, or plug in the " + DEVICE + " and press try again. If it really is a " + DEVICE +
            " that was given the " + other + " firmware by mistake, press install anyway to put it right.");
        again.hidden = false;
        offerAnyway();
        return;
      }
      let wanted = kind;
      if (kind === "auto") {
        wanted = found.squatch ? "update" : "install";
        // Already on the version this page installs, and asked to clear it: there is
        // nothing to write. Send the request and stop there.
        if (wipe && found.squatch && latest && found.version === latest) {
          show(wipe === "everything" ? "Resetting the pager…" : "Clearing the pager…", "busy");
          say("It's already on v" + latest + ", so nothing needs writing.");
          log("[flasher] already on v" + latest + ": asking it to " + wipeWords() + ", no write");
          if (usb.gone === port) await refindPort(8000);
          const text = await converse("\nwipe-" + wipe + "\n", /\[wipe\] (ok|failed)[^\n]*\n/, 12000, 4000);
          if (text.trim()) log(text.trim());
          wipeOutcome(/\[wipe\] ok/.test(text), "v" + latest + " is on it and nothing was written.");
          return;
        }
        say(found.squatch
          ? "Squatch Mesh v" + found.version + " with the " + found.radio + " radio, " + found.contacts +
            " contacts. Updating, " + (wipe === "everything" ? "then resetting everything."
              : wipe ? "then clearing its contacts, channels and messages." : "so everything is kept.")
          : "No Squatch Mesh on it (or it isn't running). Doing a full install.");
        log("[flasher] chose " + wanted + (found.squatch ? " (found v" + found.version + ")" : " (no answer to status)"));
        await sleep(1400);              // let them read it
      }

      // 2. protect what is on it
      if (found.squatch) {
        show("Saving your data first…", "busy");
        say("Telling the pager to write its contacts and settings to storage before anything is flashed. With a long contact list this can take up to a minute.");
        log(await saveFirst() ? "[flasher] pager saved its data" : "[flasher] no answer to save (older firmware)");
      }

      // 3. write
      show("Downloading the firmware…", "busy");
      say("About 90 seconds from here. Don't unplug the pager or close this tab.");
      const { version, parts } = await fetchParts(wanted === "install" ? INSTALL_MANIFEST : UPDATE_MANIFEST);
      log("[flasher] " + wanted + " v" + version + ": " + parts.map((p) => p.name).join(", "));

      show("Writing… keep the cable in", "busy");
      // Asking it what it was may have restarted it (see refindPort): if it
      // dropped off USB meanwhile, wait for it to come back and use that.
      if (usb.gone === port) await refindPort(8000);
      let chip = null, lastErr = null, openFails = 0;
      writing = true;
      /* The ladder: compressed (what esptool itself uses, and quicker), then
         slower for cables and hubs that can't hold 921600, then a plain write.
         Every attempt is read back and checked (md5 above), so a rung only
         counts as done if the chip holds exactly what was sent. */
      const ladder = [{ baud: 921600, compress: true },
                      { baud: 460800, compress: true, wait: 1500 },
                      { baud: 115200, compress: true, wait: 3000 },
                      { baud: 460800, compress: false, wait: 1500 }];
      for (const attempt of ladder) {
        try {
          if (attempt.wait) { say("That didn't take — trying a slower, simpler write. This one takes longer."); await sleep(attempt.wait); }
          await letGo();
          chip = await writeIt(parts, attempt.baud, attempt.compress);
          lastErr = null;
          break;
        } catch (e) {
          if (e && e.wrongHardware) throw e;    // not a retry matter: nothing was written
          lastErr = e;
          log("[flasher] attempt at " + attempt.baud + " failed: " + ((e && e.message) || e));
          if (openFailure(e)) { openFails++; await letGo(); await refindPort(3000); }
        }
      }
      // Not one attempt could even open the port: nothing was written.
      if (lastErr && openFails === ladder.length) lastErr.portWouldNotOpen = true;
      if (lastErr) throw lastErr;

      // 4. did it come back up
      show("Written. Asking the pager how it is…", "busy");
      say("");
      const boot = await listenForBoot(90000, version);
      if (boot.trim()) log(boot.trim());
      finish(wanted, version, boot, found);
    } catch (e) {
      const msg = (e && e.message) || String(e);
      if (e && e.looksLike) {
        show("That looks like a " + e.looksLike + ", not a " + DEVICE, "bad");
        note.textContent = "";
        note.append("Nothing was written, and it's back on the firmware it had. Its chip is the one a " + e.looksLike +
                    " has. Use the ");
        const a = document.createElement("a");
        a.href = REFUSE_LINK; a.textContent = e.looksLike + " installer";
        note.append(a, " for it. If you're sure it's a " + DEVICE + ", press install anyway.");
        logBox.open = true;
        again.hidden = false;
        offerAnyway();
        count(lastKind, "", "wrong-hardware");   // the page doing its job: counted, nothing for the developer to read
      } else if (e && e.wrongHardware) {
        show("This doesn't look like a " + DEVICE, "bad");
        say("Nothing was written, and it's back on the firmware it had. Every " + DEVICE + " has " + NEED_PSRAM +
            " of memory (PSRAM) built into its chip; this one's chip has none" +
            " (a T-Lora Pager's doesn't). If you're sure it's a " + DEVICE + ", press install anyway.");
        logBox.open = true;
        again.hidden = false;
        offerAnyway();
        count(lastKind, "", "wrong-hardware");   // the page doing its job: counted, nothing for the developer to read
      } else if (/No port selected|cancelled|The port is already open/i.test(msg) && !port) {
        show("Nothing was written", "bad");
        say("No pager was picked, so nothing happened. Press start when you're ready.");
      } else if (e && e.portWouldNotOpen) {
        log("[flasher] failed: " + msg);
        show("Couldn't open the pager's USB port", "bad");
        say("Nothing was written, so the pager is just as it was. Unplug it, plug it back in, then press " +
            "try again and pick the pager when your browser asks. " +
            (isLinux
              ? "On Linux this is usually a permission: run  sudo usermod -aG dialout $USER  (the group is uucp on Arch), " +
                "log out and back in, and use a Chrome or Chromium that isn't a snap or flatpak. "
              : "") +
            (isFirefox
              ? "Firefox's USB support is new and doesn't work on every computer yet: if it still won't open, use Chrome or Edge."
              : "If it still won't open, close anything else that could be using it: Arduino IDE, a serial monitor, " +
                "or this page open in another tab."));
        logBox.open = true;
        again.hidden = false;
        port = null;                     // a fresh pick hands us a port object that works
        count(lastKind, "", "port-not-open");
      } else if (writing && !reached) {
        // Not one attempt got an answer from the chip, so nothing was written. Either
        // the wrong thing was picked, or the device isn't listening for a write.
        log("[flasher] failed: " + msg);
        const pi = infoOf(port);
        const stranger = !pi.usbVendorId ? "isn't a USB device"
          : pi.usbVendorId !== 0x303a ? "looks like some other USB device (an adapter cable, perhaps)" : "";
        show("Couldn't reach the pager", "bad");
        say("Nothing was written, so the pager is just as it was. " +
            (stranger
              ? "What you picked " + stranger + ". Press try again and choose the entry called " +
                "\"USB JTAG/serial debug unit\". If there is no such entry: " + LOADER_HOW + ", and look again."
              : "It didn't answer. " + LOADER_HOW.charAt(0).toUpperCase() + LOADER_HOW.slice(1) +
                ", then press try again and pick it when your browser asks. A cable that only charges " +
                "does the same, so try another cable if that doesn't help."));
        logBox.open = true;
        again.hidden = false;
        port = null;                     // the next try asks which device again
        count(lastKind, "", "never-reached");
      } else if (!writing) {
        log("[flasher] failed: " + msg);
        show("Nothing was written", "bad");
        say(msg + " — it stopped before writing anything, so the pager is just as it was. Check the internet " +
            "connection and press try again.");
        logBox.open = true;
        again.hidden = false;
        report("failed-before-write", { kind: lastKind, error: msg, log: logPre.textContent.split("\n").slice(-30).join("\n") });
      } else {
        log("[flasher] failed: " + msg);
        show("That didn't finish", "bad");
        say(msg + " — the firmware was only part written, so the pager may not start until this " +
            "finishes. Press try again: it picks up from scratch and repairs it. If it still won't take, " +
            (BOARD === "t-deck" ? LOADER_HOW + ", and press try again."
                           : "unplug the pager, plug it back in, and hold the BOOT button while you press try again."));
        logBox.open = true;
        again.hidden = false;
        report("failed", { kind: lastKind, error: msg, log: logPre.textContent.split("\n").slice(-30).join("\n") });
      }
    } finally {
      busy = false;
      anyway = false;               // "install anyway" covers one run, never the next
      if (startBtn) { startBtn.disabled = false; startBtn.classList.remove("working"); startBtn.textContent = "START"; }
      for (const b of [freshBox, resetBox]) if (b) b.disabled = false;
    }
  }

  // The pager took the request to clear itself, or it didn't. lead: what is true
  // about the firmware either way.
  function wipeOutcome(ok, lead) {
    if (ok) {
      const all = wipe === "everything";
      show(all ? "Done — reset, starting as new" : "Done — starting fresh", "ok");
      say("The pager restarts twice while it clears, which can take a few minutes. Leave it switched on. " +
          (all ? "It comes up as a new device with a new identity, and asks for your region again."
               : "Its contacts, channels and messages are gone; your keys, name, radio settings and Wi-Fi are kept."));
      wipe = "";
      for (const b of [freshBox, resetBox]) if (b) b.checked = false;
      return "ok";
    }
    show("Not cleared", "bad");
    say((lead ? lead + " " : "") + "The pager didn't take the request to " + wipeWords() +
        ", so nothing was removed. Press try again.");
    logBox.open = true;
    again.hidden = false;
    return "not-cleared";
  }

  function finish(kind, version, boot, before) {
    const res = finishInner(kind, version, boot, before);
    count(kind, version, res);
  }

  /* One line to the help desk per finished install, so we know how many people
     use it: board, version, and how it went. Nothing about the device or person. */
  function count(kind, version, result) {
    try {
      fetch("/api/help/count", {
        method: "POST", keepalive: true,
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ event: "install", board: BOARD, version: version || "", kind: kind || "", result: result })
      }).catch(() => {});
    } catch (e) { /* counting never gets in the way */ }
  }

  function finishInner(kind, version, boot, before) {
    const m = boot.match(/\[status\]\s+fw=(\S+)\s+radio=(\S+)\s+radio_ok=(\d)\s+contacts=(-?\d+)/);
    // Writing the app is not the same as running it: this board has two app
    // slots, and it has been seen coming back up on the old one. Trust what the
    // pager says it is running, not what we sent it.
    if (m && version && m[1] !== version) {
      show("It came back on the old firmware", "bad");
      say("v" + version + " was written, but the pager started v" + m[1] + " instead. Nothing is lost - " +
          "press try again, and tell the developer if it keeps happening.");
      logBox.open = true;
      again.hidden = false;
      report("wrong-version", { wrote: version, running: m[1], kind: kind, log: boot.split("\n").slice(-25).join("\n") });
      return "wrong-version";
    }
    if (m && m[3] === "1" && wipe) return wipeOutcome(/\[wipe\] ok/.test(boot), "v" + m[1] + " is installed and running.");
    if (m && m[3] === "1") {
      show("Done — your pager is up", "ok");
      say("It answered: v" + m[1] + " running, " + m[2] + " radio working, " + m[4] + " contacts." +
          (kind === "install" ? " A first install sets up storage on the first start, so give it a few minutes."
                              : " Your contacts, channels and messages are untouched."));
      if (startBtn) startBtn.textContent = "DONE";
      return "ok";
    }
    if (m && m[2] === "none" && BOARD === "t-lora-pager") {
      // Neither radio the firmware drives answered. Two things look like this and the
      // browser can't tell them apart: a radio that didn't start this time (one pager was
      // seen failing several times and then coming up), and a pager sold with a radio the
      // mesh can't use. So: what to try first, then what to check. Never "it's broken".
      show("It started, but its radio didn't answer", "bad");
      say("v" + m[1] + " is installed and running, but the radio did not answer" +
          (before && before.radio === "none" ? " (the same as before this install)" : "") + ". " +
          "Installing again won't change that. First: take the SD card out if there is one, switch the pager " +
          "fully off, wait ten seconds, and switch it on; this has brought a radio up before. " +
          "If it still has no radio, check what your order or the box says: LilyGo sells this pager with an " +
          "SX1262 or LR1121 radio (both work), and also with a CC1101 or SX1280, which can't join a MeshCore " +
          "mesh. Then tell the developer in the help box which radio it is and whether an SD card was in.");
      logBox.open = true;
      again.hidden = false;
      report("radio", { kind: kind, version: version, before: before && before.radio, log: boot.split("\n").slice(-25).join("\n") });
      return "radio";
    }
    if (m) {
      show("It started, but the radio didn't", "bad");
      say("The pager is running v" + m[1] + " but its radio did not come up" +
          (m[2] && m[2] !== "none" ? " (" + m[2] + ")" : "") + ". I've sent the details to the developer.");
      logBox.open = true;
      again.hidden = false;
      // One message to the developer, not two: asking the help desk hands it off with the
      // same log, so the separate report is only for when the help box isn't on the page.
      if (!window.__squatchHelp) report("radio", { kind: kind, version: version, before: before && before.radio, log: boot.split("\n").slice(-25).join("\n") });
      if (window.__squatchHelp) {
        window.__squatchHelp.ask("A " + FULL_NAME + " was just flashed from the browser (" + kind + ", v" + version +
          "). It booted but reports its radio did not come up. Its own USB output:\n" +
          boot.split("\n").slice(-25).join("\n") +
          "\nSay what is wrong and the single most useful thing to do next. Be brief.");
      }
      return "radio";
    }
    // Restarting over and over before the firmware says anything means there is no
    // runnable app in flash. Never call that a success.
    if ((boot.match(/rst:0x/g) || []).length > 2 || (boot.match(/ESP-ROM:/g) || []).length > 2) {
      show("It's restarting over and over", "bad");
      say("The pager isn't starting the new firmware. " + LOADER_HOW.charAt(0).toUpperCase() + LOADER_HOW.slice(1) +
          ", then press try again - your contacts and settings are kept in a part of the chip this doesn't touch.");
      logBox.open = true;
      again.hidden = false;
      report("boot-loop", { kind: kind, version: version, log: boot.split("\n").slice(-25).join("\n") });
      return "boot-loop";
    }
    // Silence is not failure: it has usually finished starting before we can listen.
    // But a pager that didn't answer was never asked to clear itself.
    if (wipe) {
      show("Written, but not cleared yet", "bad");
      say("That wrote cleanly, but the pager didn't answer afterwards, so it wasn't asked to " + wipeWords() +
          " and nothing was removed. Wait until it has finished starting (a first install can take a few minutes), " +
          "then press try again: the firmware won't need writing a second time.");
      again.hidden = false;
      return "written";
    }
    show("Written successfully", "ok");
    say("That wrote cleanly. The pager didn't answer afterwards, which is normal — it usually finishes " +
        "starting before the browser can listen. Look at the pager: if the screen is on and it isn't " +
        "restarting, you're done.");
    return "written";
  }

  /* Tell the developer, not the person standing there. Failures are worth knowing
     about even when nobody writes in; this is the same endpoint the help chat uses. */
  function report(what, detail) {
    detail.browser = navigator.userAgent.slice(0, 160);
    detail.board = BOARD;
    try {
      fetch("/api/help/chat", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ messages: [{ role: "user", content:
          "INSTALLER REPORT (" + what + "). Not a question from a person - summarise for the developer and " +
          "hand off if it looks like a firmware fault.\n" + JSON.stringify(detail, null, 1).slice(0, 3000) }] }),
      }).catch(() => {});
    } catch (e) { /* never let reporting break the install */ }
  }

  again.addEventListener("click", () => {
    // Refused as the wrong device: "try again" is most likely with another one
    // plugged in, so let the browser ask which.
    if (!anywayBtn.hidden) port = null;
    // A retry keeps what was agreed to at START, unless the boxes were changed since.
    if (!busy && wipe !== ticked() && !askWipe()) return;
    run(lastKind === "auto" ? "auto" : lastKind);
  });
  anywayBtn.addEventListener("click", () => {
    if (busy) return;
    if (wipe !== ticked() && !askWipe()) return;
    anyway = true;
    run(lastKind === "auto" ? "auto" : lastKind);
  });

  document.querySelectorAll("[data-flash]").forEach((b) => {
    if (!SUPPORTED) {
      b.disabled = true;
      b.title = "Needs Chrome or Edge on a desktop";
      return;
    }
    b.addEventListener("click", () => { if (!busy && askWipe()) run(b.dataset.kind || "auto"); });
  });

  if (!SUPPORTED) {
    const warn = document.getElementById("flash-unsupported");
    if (warn) warn.hidden = false;
  }
}
