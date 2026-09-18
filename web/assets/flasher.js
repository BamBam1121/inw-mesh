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
   - nothing here touches DTR/RTS by hand; esptool-js owns the reset. */
import { ESPLoader, Transport } from "./vendor/esptool-0.6.1.js";

const UPDATE_MANIFEST = "https://bambam1121.github.io/inw-mesh/manifest-update.json";
const INSTALL_MANIFEST = "https://bambam1121.github.io/inw-mesh/manifest-install.json";
const SUPPORTED = !!(navigator.serial && window.isSecureContext);

const panel = document.getElementById("flasher");
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

  let busy = false;
  let port = null;            // kept between attempts so a retry doesn't re-prompt
  let lastKind = "auto";

  const show = (title, cls) => { panel.hidden = false; head.textContent = title; head.className = "fl-head " + cls; };
  const say = (text) => { note.textContent = text; };
  const pct = (n) => { bar.style.width = Math.max(0, Math.min(100, n)) + "%"; };
  function log(line) {
    const s = String(line).replace(/\r/g, "");
    logPre.textContent += s + (s.endsWith("\n") ? "" : "\n");
    logPre.scrollTop = logPre.scrollHeight;
  }
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

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
  async function converse(send, mark, ms) {
    let text = "";
    try {
      await port.open({ baudRate: 115200, bufferSize: 4096 });
    } catch (e) {
      return text;                       // busy, or already in the ROM loader
    }
    let reader = null;
    try {
      if (send) {
        const w = port.writable.getWriter();
        await w.write(new TextEncoder().encode(send));
        w.releaseLock();
      }
      const dec = new TextDecoder();
      reader = port.readable.getReader();
      const deadline = Date.now() + ms;
      while (Date.now() < deadline) {
        const chunk = await Promise.race([reader.read(), sleep(1200).then(() => ({ timeout: true }))]);
        if (!chunk || chunk.done) break;
        if (chunk.timeout) continue;
        text += dec.decode(chunk.value, { stream: true });
        if (mark && text.indexOf(mark) >= 0) break;
      }
    } catch (e) {
      /* whatever we heard is what we use */
    } finally {
      try { if (reader) { await reader.cancel(); reader.releaseLock(); } } catch (e) {}
      try { await port.close(); } catch (e) {}
    }
    return text;
  }

  // What is on this pager? Squatch Mesh answers "status"; anything else stays quiet,
  // which is itself the answer - a pager on other firmware needs a first install.
  async function identify() {
    const text = await converse("\nstatus\n", "[status]", 6000);
    if (text.trim()) log(text.trim());
    const m = text.match(/\[status\]\s+fw=(\S+)\s+radio=(\S+)\s+radio_ok=(\d)\s+contacts=(-?\d+)/);
    if (!m) return { squatch: false, raw: text };
    return { squatch: true, version: m[1], radio: m[2], radioOk: m[3] === "1", contacts: parseInt(m[4], 10), raw: text };
  }

  // Ask it to put contacts, channels and settings on flash before we reset it.
  // Firmware from 1.1.21 answers "[save]"; older builds ignore the line and we
  // carry on - no worse off than the old installer, which never asked at all.
  async function saveFirst() {
    const text = await converse("\nsave\n", "[save]", 8000);
    if (text.trim()) log(text.trim());
    return text.indexOf("[save]") >= 0;
  }

  /* ---- the flash itself ---------------------------------------------------------- */

  async function fetchParts(manifestUrl) {
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

  async function writeIt(parts, baud, compress) {
    const transport = new Transport(port, false);
    const loader = new ESPLoader({ transport, baudrate: baud, romBaudrate: 115200, terminal, debugLogging: false });
    try {
      const chip = await loader.main();
      log("[flasher] " + chip + " @ " + baud + (compress ? " compressed" : " uncompressed"));
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
      await loader.after("hard_reset");
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
    if (startBtn) { startBtn.disabled = true; startBtn.classList.add("working"); startBtn.textContent = "WORKING…"; }
    logPre.textContent = "";
    pct(0);

    try {
      if (!port) {
        show("Pick the pager", "busy");
        say("Your browser is asking which USB device to use. Choose the pager.");
        port = await navigator.serial.requestPort();
      }

      // 1. what is on it
      show("Checking the pager…", "busy");
      say("Asking what firmware it is running.");
      const found = kind === "install" ? { squatch: false } : await identify();
      let wanted = kind;
      if (kind === "auto") {
        wanted = found.squatch ? "update" : "install";
        say(found.squatch
          ? "Squatch Mesh v" + found.version + " with the " + found.radio + " radio, " + found.contacts +
            " contacts. Updating, so everything is kept."
          : "No Squatch Mesh on it (or it isn't running). Doing a full install.");
        log("[flasher] chose " + wanted + (found.squatch ? " (found v" + found.version + ")" : " (no answer to status)"));
        await sleep(1400);              // let them read it
      }

      // 2. protect what is on it
      if (found.squatch) {
        show("Saving your data first…", "busy");
        say("Telling the pager to write its contacts and settings to storage before anything is flashed.");
        log(await saveFirst() ? "[flasher] pager saved its data" : "[flasher] no answer to save (older firmware)");
      }

      // 3. write
      show("Downloading the firmware…", "busy");
      say("About 90 seconds from here. Don't unplug the pager or close this tab.");
      const { version, parts } = await fetchParts(wanted === "install" ? INSTALL_MANIFEST : UPDATE_MANIFEST);
      log("[flasher] " + wanted + " v" + version + ": " + parts.map((p) => p.name).join(", "));

      show("Writing… keep the cable in", "busy");
      let chip = null, lastErr = null;
      /* The ladder: compressed (what esptool itself uses, and quicker), then
         slower for cables and hubs that can't hold 921600, then a plain write.
         Every attempt is read back and checked (md5 above), so a rung only
         counts as done if the chip holds exactly what was sent. */
      for (const attempt of [{ baud: 921600, compress: true },
                             { baud: 460800, compress: true, wait: 1500 },
                             { baud: 115200, compress: true, wait: 3000 },
                             { baud: 460800, compress: false, wait: 1500 }]) {
        try {
          if (attempt.wait) { say("That didn't take — trying a slower, simpler write. This one takes longer."); await sleep(attempt.wait); }
          chip = await writeIt(parts, attempt.baud, attempt.compress);
          lastErr = null;
          break;
        } catch (e) {
          lastErr = e;
          log("[flasher] attempt at " + attempt.baud + " failed: " + ((e && e.message) || e));
        }
      }
      if (lastErr) throw lastErr;

      // 4. did it come back up
      show("Written. Asking the pager how it is…", "busy");
      say("");
      // After the reset the USB port vanishes and comes back, then the pager
      // takes several seconds to boot. Keep reopening and re-asking until it
      // answers - one early failed open used to end the check before it rebooted.
      const until = Date.now() + 30000;
      let boot = "";
      await sleep(2000);
      while (Date.now() < until && boot.indexOf("[status]") < 0) {
        boot += await converse("\nstatus\n", "[status]", Math.min(4000, Math.max(0, until - Date.now())));
        if (boot.indexOf("[status]") < 0) await sleep(1000);
      }
      if (boot.trim()) log(boot.trim());
      finish(wanted, version, boot, found);
    } catch (e) {
      const msg = (e && e.message) || String(e);
      if (/No port selected|cancelled|The port is already open/i.test(msg) && !port) {
        show("Nothing was written", "bad");
        say("No pager was picked, so nothing happened. Press start when you're ready.");
      } else {
        log("[flasher] failed: " + msg);
        show("That didn't finish", "bad");
        say(msg + " — the firmware was only part written, so the pager may not start until this " +
            "finishes. Press try again: it picks up from scratch and repairs it. If it still won't take, " +
            "unplug the pager, plug it back in, and hold the BOOT button while you press try again.");
        logBox.open = true;
        again.hidden = false;
        report("failed", { kind: lastKind, error: msg, log: logPre.textContent.split("\n").slice(-30).join("\n") });
      }
    } finally {
      busy = false;
      if (startBtn) { startBtn.disabled = false; startBtn.classList.remove("working"); startBtn.textContent = "START"; }
    }
  }

  function finish(kind, version, boot, before) {
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
      return;
    }
    if (m && m[3] === "1") {
      show("Done — your pager is up", "ok");
      say("It answered: v" + m[1] + " running, " + m[2] + " radio working, " + m[4] + " contacts." +
          (kind === "install" ? " A first install sets up storage on the first start, so give it a few minutes."
                              : " Your contacts, channels and messages are untouched."));
      if (startBtn) startBtn.textContent = "DONE";
      return;
    }
    if (m) {
      show("It started, but the radio didn't", "bad");
      say("The pager is running v" + m[1] + " but its radio did not come up" +
          (m[2] && m[2] !== "none" ? " (" + m[2] + ")" : "") + ". I've sent the details to the developer.");
      logBox.open = true;
      again.hidden = false;
      report("radio", { kind: kind, version: version, before: before && before.radio, log: boot.split("\n").slice(-25).join("\n") });
      if (window.__squatchHelp) {
        window.__squatchHelp.ask("A LilyGo T-Lora Pager was just flashed from the browser (" + kind + ", v" + version +
          "). It booted but reports its radio did not come up. Its own USB output:\n" +
          boot.split("\n").slice(-25).join("\n") +
          "\nSay what is wrong and the single most useful thing to do next. Be brief.");
      }
      return;
    }
    // Restarting over and over before the firmware says anything means there is no
    // runnable app in flash. Never call that a success.
    if ((boot.match(/rst:0x/g) || []).length > 2 || (boot.match(/ESP-ROM:/g) || []).length > 2) {
      show("It's restarting over and over", "bad");
      say("The pager isn't starting the new firmware. Hold BOOT, tap RESET, let go of BOOT, then press " +
          "try again - your contacts and settings are kept in a part of the chip this doesn't touch.");
      logBox.open = true;
      again.hidden = false;
      report("boot-loop", { kind: kind, version: version, log: boot.split("\n").slice(-25).join("\n") });
      return;
    }
    // Silence is not failure: it has usually finished starting before we can listen.
    show("Written successfully", "ok");
    say("That wrote cleanly. The pager didn't answer afterwards, which is normal — it usually finishes " +
        "starting before the browser can listen. Look at the pager: if the screen is on and it isn't " +
        "restarting, you're done.");
  }

  /* Tell the developer, not the person standing there. Failures are worth knowing
     about even when nobody writes in; this is the same endpoint the help chat uses. */
  function report(what, detail) {
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

  again.addEventListener("click", () => run(lastKind === "auto" ? "auto" : lastKind));

  document.querySelectorAll("[data-flash]").forEach((b) => {
    if (!SUPPORTED) {
      b.disabled = true;
      b.title = "Needs Chrome or Edge on a desktop";
      return;
    }
    b.addEventListener("click", () => run(b.dataset.kind || "auto"));
  });

  if (!SUPPORTED) {
    const warn = document.getElementById("flash-unsupported");
    if (warn) warn.hidden = false;
  }
}
