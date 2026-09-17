/* Help page chat: questions go to the Squatch Mesh helper (Claude, on the site's own
   server). When it can't solve something it offers to send the conversation to the
   developer. The conversation lives only in this tab. */
(function () {
  const root = document.getElementById("helper");
  if (!root) return;
  const log = root.querySelector(".chat-log");
  const form = root.querySelector(".chat-form");
  const input = form.querySelector("textarea");
  const send = form.querySelector("button[type=submit]");
  const handoffBox = document.getElementById("handoff");
  const hForm = handoffBox.querySelector("form");
  const hTitle = handoffBox.querySelector("h3");
  const hNote = hForm.querySelector("[name=note]");
  const hStatus = hForm.querySelector(".form-status");
  const direct = document.getElementById("direct-message");
  const resetBtn = root.querySelector(".chat-reset");
  const KEY = "sm-helper-chat";

  let messages = [];
  let pendingHandoff = null;
  let busy = false;

  const esc = (s) => String(s).replace(/[&<>"']/g, (c) =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

  // Tiny, safe formatting: escape first, then links, **bold**, `code` and line breaks.
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

  function save() {
    try { sessionStorage.setItem(KEY, JSON.stringify({ messages, pendingHandoff })); } catch (e) { /* fine */ }
  }

  function showHandoff(h) {
    pendingHandoff = h;
    hTitle.textContent = h ? "Send this to the developer" : "Message the developer";
    hNote.placeholder = h ? "Anything to add? (optional)" : "What's going on, or what's your idea?";
    hStatus.textContent = "";
    handoffBox.hidden = false;
    save();
  }

  async function ask(text) {
    if (busy) return;
    busy = true;
    send.disabled = true;
    messages.push({ role: "user", content: text });
    bubble("user", text);
    const wait = bubble("assistant", "Thinking…", "pending");
    try {
      const r = await fetch("/api/help/chat", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ messages }),
      });
      const j = await r.json().catch(() => ({}));
      wait.remove();
      if (!r.ok) {
        messages.pop();
        if (j.error === "offline" || r.status >= 500) {
          bubble("assistant", "The helper is taking a break right now. You can still message the developer below, " +
            "or post on GitHub.", "note");
          showHandoff(null);
        } else {
          bubble("assistant", j.error || "Something went wrong. Try again.", "note");
        }
        input.value = text;
      } else {
        messages.push({ role: "assistant", content: j.reply });
        bubble("assistant", j.reply);
        if (j.handoff) showHandoff(j.handoff);
      }
    } catch (e) {
      wait.remove();
      messages.pop();
      input.value = text;
      bubble("assistant", "Couldn't reach the helper. Check your connection and try again.", "note");
    }
    save();
    busy = false;
    send.disabled = false;
    input.focus();
  }

  form.addEventListener("submit", (e) => {
    e.preventDefault();
    const text = input.value.trim();
    if (!text) return;
    if (messages.length >= 22) {
      bubble("assistant", "This conversation is getting long. Start over, or send it to the developer below.", "note");
      showHandoff(pendingHandoff);
      return;
    }
    input.value = "";
    ask(text);
  });
  input.addEventListener("keydown", (e) => {
    if (e.key === "Enter" && !e.shiftKey) { e.preventDefault(); form.requestSubmit(); }
  });

  root.querySelectorAll("[data-ask]").forEach((b) =>
    b.addEventListener("click", () => ask(b.getAttribute("data-ask"))));

  direct.addEventListener("click", () => {
    showHandoff(pendingHandoff);
    handoffBox.scrollIntoView({ behavior: "smooth", block: "center" });
    hNote.focus();
  });

  resetBtn.addEventListener("click", () => {
    messages = [];
    pendingHandoff = null;
    handoffBox.hidden = true;
    log.querySelectorAll(".msg:not(.intro)").forEach((m) => m.remove());
    save();
    input.focus();
  });

  hForm.addEventListener("submit", async (e) => {
    e.preventDefault();
    const note = hNote.value.trim();
    if (!pendingHandoff && !note && !messages.length) { hStatus.textContent = "Write a message first."; return; }
    const btn = hForm.querySelector("button[type=submit]");
    btn.disabled = true;
    hStatus.textContent = "Sending…";
    try {
      const r = await fetch("/api/help/handoff", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          messages,
          note,
          summary: pendingHandoff ? pendingHandoff.summary : "",
          category: pendingHandoff ? pendingHandoff.category : "message",
          name: hForm.querySelector("[name=name]").value,
          email: hForm.querySelector("[name=email]").value,
        }),
      });
      const j = await r.json().catch(() => ({}));
      if (r.ok) {
        hForm.reset();
        handoffBox.hidden = true;
        pendingHandoff = null;
        bubble("assistant", "Sent to the developer. If you left an email address, the reply will come there. " +
          "It's a one-person project, so give it a few days.", "note");
        save();
      } else {
        hStatus.textContent = j.error || "That didn't send. Please post on GitHub instead.";
      }
    } catch (err) {
      hStatus.textContent = "Couldn't reach the server. Please post on GitHub instead.";
    }
    btn.disabled = false;
  });

  // Pick up where this tab left off.
  try {
    const saved = JSON.parse(sessionStorage.getItem(KEY) || "null");
    if (saved && Array.isArray(saved.messages)) {
      messages = saved.messages;
      messages.forEach((m) => bubble(m.role, m.content));
      if (saved.pendingHandoff) showHandoff(saved.pendingHandoff);
    }
  } catch (e) { /* start fresh */ }

  root.classList.add("ready");
})();
