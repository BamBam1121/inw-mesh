/* Watches the browser installer, then listens to the pager itself.
   The installer only knows the bytes were written. Only the pager knows whether
   it came up, and it says so over USB at every boot ("[boot] radio ok",
   "[boot] ready ..."). Saying nothing at all is an answer too.
   If it didn't come up, this reinstalls it once on its own before bothering
   anyone, because a bad write is the common cause and rewriting fixes it. */
(function () {
  const box = document.getElementById("flash-help");
  if (!box) return;
  const head = box.querySelector(".flash-head");
  const log = box.querySelector(".chat-log");
  const form = box.querySelector(".chat-form");
  const input = form.querySelector("textarea");
  const again = box.querySelector(".try-again");

  let messages = [];      // conversation with the helper
  let trail = [];         // installer states seen this attempt
  let kind = "install";   // which button was pressed
  let button = null;      // so a retry can re-run the same one
  let asking = false;
  let selfFixed = false;  // only ever reinstall by ourselves once

  const esc = (s) => String(s).replace(/[&<>"']/g, (c) =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

  function format(text) {
    let h = esc(text);
    h = h.replace(/\bhttps:\/\/[^\s<)]+[^\s<).,;:!?]/g, (u) => '<a href="' + u + '" rel="noopener">' + u + "</a>");
    h = h.replace(/\*\*([^*\n]+)\*\*/g, "<b>$1</b>");
    h = h.replace(/`([^`\n]+)`/g, "<code>$1</code>");
    return h.split(/\n{2,}/).map((p) => "<p>" + p.replace(/\n/g, "<br>") + "</p>").join("");
  }

  function bubble(role, text, extra) {
    const div = document.createElement("div");
    div.className = "msg " + role + (extra ? " " + extra : "");
    div.innerHTML = role === "user" ? "<p>" + esc(text).replace(/\n/g, "<br>") + "</p>" : format(text);
    log.appendChild(div);
    log.scrollTop = log.scrollHeight;
    return div;
  }

  function show(title, cls) {
    box.hidden = false;
    head.textContent = title;
    head.className = "flash-head " + cls;
  }

  document.querySelectorAll("esp-web-install-button").forEach((b) => {
    b.addEventListener("click", () => {
      button = b;
      kind = (b.getAttribute("manifest") || "").includes("manifest-install") ? "first install" : "update";
    }, true);
  });

  // The installer builds its dialog on the body when it opens, and closes the
  // serial port when that dialog goes away.
  new MutationObserver((muts) => {
    muts.forEach((m) => m.addedNodes.forEach((n) => {
      if (n.nodeName && n.nodeName.toLowerCase() === "ewt-install-dialog") {
        trail = [];
        n.addEventListener("state-changed", (e) => onState(e.detail));
        n.addEventListener("closed", () => { if (trail.includes("finished")) checkItCameUp(); }, { once: true });
      }
    }));
  }).observe(document.body, { childList: true });

  function onState(s) {
    if (!s || !s.state) return;
    if (trail[trail.length - 1] !== s.state) trail.push(s.state);
    if (s.state === "finished") {
      show("Written. Waiting for the pager to start…", "busy");
      again.hidden = true;
      log.innerHTML = "";
    } else if (s.state === "error") {
      show("That didn't finish", "bad");
      again.hidden = false;
      form.hidden = false;
      report(s.message || "unknown error");
    } else {
      show("Flashing… keep the cable in", "busy");
      again.hidden = true;
    }
  }

  // ---- listening to the pager -------------------------------------------------
  // Flashing already granted this page access to the port, so reopening it asks
  // nothing. The pager reboots and re-enumerates first, so keep trying for a while.
  async function readBootLog(totalMs) {
    if (!navigator.serial || !navigator.serial.getPorts) return null;
    const deadline = Date.now() + totalMs;
    let text = "";
    while (Date.now() < deadline) {
      let ports = [];
      try { ports = await navigator.serial.getPorts(); } catch (e) { ports = []; }
      for (const port of ports) {
        let reader = null;
        try {
          await port.open({ baudRate: 115200, bufferSize: 4096 });
        } catch (e) {
          continue;                       // still re-enumerating, or someone else has it
        }
        try {
          const dec = new TextDecoder();
          reader = port.readable.getReader();
          while (Date.now() < deadline) {
            const chunk = await Promise.race([
              reader.read(),
              new Promise((r) => setTimeout(() => r({ timeout: true }), 1500)),
            ]);
            if (chunk && chunk.timeout) { if (text) break; else continue; }
            if (!chunk || chunk.done) break;
            text += dec.decode(chunk.value, { stream: true });
            if (/\[boot\]\s+ready/.test(text)) break;     // it finished booting
          }
        } catch (e) {
          /* fall through and report whatever we heard */
        } finally {
          try { if (reader) { await reader.cancel(); reader.releaseLock(); } } catch (e) { /* ignore */ }
          try { await port.close(); } catch (e) { /* ignore */ }
        }
        if (text) return text;
      }
      await new Promise((r) => setTimeout(r, 800));
    }
    return text || null;
  }

  // What the pager's own output means. Exposed for testing.
  function classify(text) {
    if (!text || !text.trim()) return "silent";
    const resets = (text.match(/rst:0x|Guru Meditation|assert failed|Backtrace:/g) || []).length;
    const boots = (text.match(/\[boot\]\s+power rails/g) || []).length;
    if (resets > 1 || boots > 1) return "loop";
    if (/\[boot\]\s+radio\s+FAILED|radio init failed/.test(text)) return "radio";
    if (/\[boot\]\s+radio\s+ok/.test(text) || /\[boot\]\s+ready/.test(text)) return "ok";
    return "unknown";
  }
  window.__squatchClassify = classify;

  async function checkItCameUp() {
    show("Written. Waiting for the pager to start…", "busy");
    const text = await readBootLog(30000);
    const verdict = classify(text);

    if (verdict === "ok") {
      show("Done — your pager is up", "ok");
      const chip = (text.match(/\[boot\]\s+radio\s+ok\s+(\S+)/) || [])[1];
      bubble("assistant",
        "The pager started and its radio is running" + (chip ? " (" + chip + ")" : "") + "." +
        (kind === "first install"
          ? " A first install formats storage on that first start, so give it a few minutes before it settles."
          : " Your contacts, channels and messages are untouched.") +
        "\n\nAnything not right? Ask me here.", "note");
      form.hidden = false;
      return;
    }

    // It didn't come up. A bad write is much the most likely cause, and
    // rewriting fixes that, so do it rather than describe it.
    if ((verdict === "loop" || verdict === "silent" || verdict === "unknown") && !selfFixed && button) {
      selfFixed = true;
      show("It didn't start. Reinstalling it now…", "busy");
      bubble("assistant",
        verdict === "silent"
          ? "The pager isn't saying anything over USB, which usually means the write didn't take. I'm writing it again — this fixes it most of the time."
          : "The pager is restarting over and over, which means it didn't get a clean write. I'm writing it again.",
        "note");
      const installer = document.querySelector('esp-web-install-button[manifest*="manifest-install"]') || button;
      installer.click();      // needs the browser to allow it; if not, the button below does
      setTimeout(() => {
        if (!document.querySelector("ewt-install-dialog")) {
          show("It didn't start — one tap to fix it", "bad");
          again.hidden = false;
          again.textContent = "Reinstall now";
          bubble("assistant", "Your browser wants a click before it can open the USB port again. " +
            "Press **Reinstall now** and it will rewrite the pager.", "note");
        }
      }, 1200);
      return;
    }

    // Either we already tried rewriting, or the radio itself answered badly.
    show("It started, but not cleanly", "bad");
    again.hidden = false;
    form.hidden = false;
    reportBoot(verdict, text);
  }

  function report(message) {
    log.innerHTML = "";
    bubble("user", "The installer stopped with: " + message);
    messages = [{ role: "user", content: [
      "The browser installer failed while flashing a LilyGo T-Lora Pager.",
      "Which button: " + kind,
      "Error: " + message,
      "Steps reached: " + (trail.join(" → ") || "none"),
      "Browser: " + navigator.userAgent,
      "Explain what this means and what to try, in a couple of short steps.",
    ].join("\n") }];
    send();
  }

  function reportBoot(verdict, text) {
    const tail = (text || "").split(/\r?\n/).filter((l) => l.trim()).slice(-25).join("\n");
    messages = [{ role: "user", content: [
      "A LilyGo T-Lora Pager was just flashed with the browser installer (" + kind + ").",
      "The write succeeded, and it has already been rewritten once automatically.",
      verdict === "radio" ? "Its boot output says the radio did not come up."
                          : "It still is not booting cleanly.",
      "This is the pager's own USB output:",
      tail || "(nothing at all)",
      "Say what is wrong and the single most useful thing to do next. Be brief.",
    ].join("\n") }];
    send();
  }

  async function send() {
    if (asking) return;
    asking = true;
    const wait = bubble("assistant", "Looking at that…", "pending");
    try {
      const r = await fetch("/api/help/chat", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ messages }),
      });
      const j = await r.json().catch(() => ({}));
      wait.remove();
      if (r.ok && j.reply) {
        messages.push({ role: "assistant", content: j.reply });
        bubble("assistant", j.reply);
      } else {
        messages.pop();
        bubble("assistant", "I can't reach the helper right now. The \"If something goes wrong\" section " +
          "below covers the common causes, or ask on GitHub.", "note");
      }
    } catch (e) {
      wait.remove();
      messages.pop();
      bubble("assistant", "I can't reach the helper right now — check your connection.", "note");
    }
    asking = false;
  }

  form.addEventListener("submit", (e) => {
    e.preventDefault();
    const text = input.value.trim();
    if (!text || asking || messages.length > 20) return;
    input.value = "";
    messages.push({ role: "user", content: text });
    bubble("user", text);
    send();
  });
  input.addEventListener("keydown", (e) => {
    if (e.key === "Enter" && !e.shiftKey) { e.preventDefault(); form.requestSubmit(); }
  });

  again.addEventListener("click", () => {
    if (!button) return;
    box.hidden = true;
    (document.querySelector('esp-web-install-button[manifest*="manifest-install"]') || button).click();
  });
})();
