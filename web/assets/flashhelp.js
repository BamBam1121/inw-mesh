/* Watches the browser installer, then asks the pager itself how it is.
   The installer only knows the bytes were written; only the pager knows whether
   it came up. It answers "status" over the same USB port.

   Rules this file sticks to, because getting them wrong breaks working pagers:
   - silence is NOT failure (older firmware has no status line, and the boot
     report has usually scrolled past by the time the browser can listen);
   - the only thing fixed automatically is a restart loop, which is unambiguous
     and which rewriting reliably fixes;
   - a retry repeats exactly what the person chose, never a different one;
   - nothing here touches DTR/RTS - those are the reset lines, and getting them
     wrong strands the pager in download mode. */
(function () {
  const box = document.getElementById("flash-help");
  if (!box) return;
  const head = box.querySelector(".flash-head");
  const log = box.querySelector(".chat-log");
  const form = box.querySelector(".chat-form");
  const input = form.querySelector("textarea");
  const again = box.querySelector(".try-again");

  let messages = [];
  let trail = [];
  let kind = "install";
  let button = null;
  let asking = false;
  let selfFixed = false;

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
      show("Written. Asking the pager how it is…", "busy");
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

  // Flashing already granted this page the port, so reopening asks nothing.
  async function askPager(totalMs) {
    if (!navigator.serial || !navigator.serial.getPorts) return "";
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
          continue;                    // still re-enumerating, or something else holds it
        }
        try {
          const w = port.writable.getWriter();
          await w.write(new TextEncoder().encode("\nstatus\n"));
          w.releaseLock();
          const dec = new TextDecoder();
          reader = port.readable.getReader();
          while (Date.now() < deadline) {
            const chunk = await Promise.race([
              reader.read(),
              new Promise((r) => setTimeout(() => r({ timeout: true }), 1200)),
            ]);
            if (!chunk || chunk.timeout || chunk.done) break;
            text += dec.decode(chunk.value, { stream: true });
            if (text.indexOf("[status]") >= 0 || /\[boot\]\s+ready/.test(text)) break;
          }
        } catch (e) {
          /* whatever we heard is what we report */
        } finally {
          try { if (reader) { await reader.cancel(); reader.releaseLock(); } } catch (e) { /* ignore */ }
          try { await port.close(); } catch (e) { /* ignore */ }
        }
        if (text.trim()) return text;
      }
      await new Promise((r) => setTimeout(r, 700));
    }
    return text;
  }

  // What the pager said. "noanswer" means we learned nothing - never treat that
  // as a fault.
  function classify(text) {
    if (!text || !text.trim()) return "noanswer";
    const m = text.match(/\[status\][^\n]*radio_ok=(\d)/);
    if (m) return m[1] === "1" ? "ok" : "radio";
    const resets = (text.match(/rst:0x|Guru Meditation|assert failed|Backtrace:/g) || []).length;
    const boots = (text.match(/\[boot\]\s+power rails/g) || []).length;
    if (resets > 1 || boots > 1) return "loop";
    if (/\[boot\]\s+radio\s+FAILED/.test(text) || /radio init failed/.test(text)) return "radio";
    if (/\[boot\]\s+radio\s+ok/.test(text) || /\[boot\]\s+ready/.test(text)) return "ok";
    return "noanswer";
  }
  window.__squatchClassify = classify;

  async function checkItCameUp() {
    show("Written. Asking the pager how it is…", "busy");
    let text = "";
    try { text = await askPager(20000); } catch (e) { text = ""; }
    const verdict = classify(text);

    if (verdict === "ok") {
      const chip = (text.match(/radio=(\S+)/) || text.match(/\[boot\]\s+radio\s+ok\s+(\S+)/) || [])[1];
      show("Done — your pager is up", "ok");
      bubble("assistant",
        "The pager answered: it started and its radio is running" +
        (chip && chip !== "none" ? " (" + chip + ")" : "") + "." +
        (kind === "first install"
          ? " Storage is set up on that first start, so give it a few minutes to settle."
          : " Your contacts, channels and messages are untouched.") +
        "\n\nAnything not right? Ask me here.", "note");
      form.hidden = false;
      return;
    }

    // The one thing worth fixing without being asked, and the one rewriting fixes.
    if (verdict === "loop" && !selfFixed && button) {
      selfFixed = true;
      show("It keeps restarting. Writing it again…", "busy");
      bubble("assistant", "The pager is restarting over and over, so the write didn't take. " +
        "I'm writing the same thing again — that fixes it.", "note");
      button.click();
      setTimeout(() => {
        if (!document.querySelector("ewt-install-dialog")) {
          show("It keeps restarting — one tap to fix it", "bad");
          again.hidden = false;
          again.textContent = "Write it again";
          bubble("assistant", "Your browser wants a click before it can open the USB port again. " +
            "Press **Write it again**.", "note");
        }
      }, 1200);
      return;
    }

    if (verdict === "radio") {
      show("It started, but the radio didn't", "bad");
      again.hidden = false;
      form.hidden = false;
      reportBoot(text);
      return;
    }

    // No answer. Usually just means it finished starting before we could listen.
    show("Written successfully", "ok");
    bubble("assistant",
      "That wrote cleanly. I couldn't get a word out of the pager afterwards, which is normal — it has " +
      "usually finished starting before the browser can listen.\n\n**Look at the pager itself:** if the screen " +
      "is on and it isn't restarting, you're done." +
      (kind === "first install" ? " A first install takes a few minutes to set up storage the first time." : "") +
      "\n\nIf it looks dead or keeps restarting, tell me here and I'll sort it out.", "note");
    form.hidden = false;
  }

  function report(message) {
    log.innerHTML = "";
    bubble("user", "The installer stopped with: " + message);
    messages = [{ role: "user", content: [
      "The browser installer failed while flashing a LilyGo T-Lora Pager.",
      "Which button: " + kind,
      "Error: " + message,
      "Steps reached: " + (trail.join(" -> ") || "none"),
      "Browser: " + navigator.userAgent,
      "Explain what this means and what to try, in a couple of short steps.",
    ].join("\n") }];
    send();
  }

  function reportBoot(text) {
    const tail = (text || "").split(/\r?\n/).filter((l) => l.trim()).slice(-25).join("\n");
    messages = [{ role: "user", content: [
      "A LilyGo T-Lora Pager was just flashed with the browser installer (" + kind + ").",
      "The write succeeded but the pager reports its radio did not come up.",
      "This is the pager's own USB output:",
      tail || "(nothing)",
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
    button.click();          // the same one they chose, never a different one
  });
})();
