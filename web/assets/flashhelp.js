/* Watches the browser installer and reports what happened.
   Success gets a plain "it worked". A failure goes straight to the Squatch
   helper with the real error text, so nobody has to describe it themselves.
   The installer (esp-web-tools) creates an <ewt-install-dialog> on the body and
   fires "state-changed" on it: initializing, preparing, erasing, writing,
   finished, error. */
(function () {
  const box = document.getElementById("flash-help");
  if (!box) return;
  const head = box.querySelector(".flash-head");
  const log = box.querySelector(".chat-log");
  const form = box.querySelector(".chat-form");
  const input = form.querySelector("textarea");
  const again = box.querySelector(".try-again");

  let messages = [];      // the conversation, same shape the helper expects
  let trail = [];         // states seen during this attempt
  let kind = "install";   // which button was pressed
  let button = null;      // so "try again" can re-run the same one
  let asking = false;

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

  // Remember which button started this, for the error report and for retrying.
  document.querySelectorAll("esp-web-install-button").forEach((b) => {
    b.addEventListener("click", () => {
      button = b;
      kind = (b.getAttribute("manifest") || "").includes("manifest-install") ? "first install" : "update";
    }, true);
  });

  // The installer builds its dialog on the body when it opens.
  new MutationObserver((muts) => {
    muts.forEach((m) => m.addedNodes.forEach((n) => {
      if (n.nodeName && n.nodeName.toLowerCase() === "ewt-install-dialog") {
        trail = [];
        n.addEventListener("state-changed", (e) => onState(e.detail));
      }
    }));
  }).observe(document.body, { childList: true });

  function onState(s) {
    if (!s || !s.state) return;
    const last = trail[trail.length - 1];
    if (last !== s.state) trail.push(s.state);
    if (s.state === "finished") {
      show("Flashed successfully", "ok");
      again.hidden = true;
      log.innerHTML = "";
      bubble("assistant",
        kind === "first install"
          ? "That wrote cleanly. The first start formats the pager's storage, which takes a few minutes — leave it plugged in and let it finish. If you put your old data on the SD card first, it comes back on its own.\n\nAnything not right? Ask me here."
          : "That wrote cleanly. Your contacts, channels and messages are untouched.\n\nAnything not right? Ask me here.",
        "note");
      form.hidden = false;
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

  // Hand the failure to the helper without making anyone retype it.
  function report(message) {
    log.innerHTML = "";
    bubble("user", "The installer stopped with: " + message);
    const why = [
      "The browser installer failed while flashing a LilyGo T-Lora Pager.",
      "Which button: " + kind,
      "Error: " + message,
      "Steps reached: " + (trail.join(" → ") || "none"),
      "Browser: " + navigator.userAgent,
      "Explain what this means and what to try, in a couple of short steps.",
    ].join("\n");
    messages = [{ role: "user", content: why }];
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
        bubble("assistant",
          "I can't reach the helper right now. The install page's \"If something goes wrong\" section covers the " +
          "common causes, or ask on GitHub.", "note");
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
    button.click();
  });
})();
