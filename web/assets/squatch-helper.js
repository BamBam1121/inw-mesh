/* The sasquatch in the corner of every page. He waves now and then; a click opens a
   small chat with the same helper as /help (the help desk on the site's own server).
   The conversation is kept in this tab under the help page's own key, so it carries
   on from page to page and onto /help. The X hides him until the page is reloaded.
   On /help itself a click takes you to the chat on the page instead. */
(function () {
  if (window.__squatchHelper) return;
  window.__squatchHelper = true;

  const KEY = "sm-helper-chat";       // shared with assets/help.js
  const HELLO = "sm-helper-hello";    // the greeting shows once per tab
  const pageChat = document.getElementById("helper");

  // A shaggy outline: an ellipse whose edge goes in and out like fur.
  function shaggy(cx, cy, rx, ry, tufts, depth) {
    let d = "";
    for (let i = 0; i < tufts * 2; i++) {
      const a = (Math.PI * i) / tufts;
      const k = i % 2 ? 1 - depth : 1;
      d += (i ? "L" : "M") + (cx + Math.cos(a) * rx * k).toFixed(1) + " " + (cy + Math.sin(a) * ry * k).toFixed(1);
    }
    return d + "Z";
  }

  // Drawn in a 120 x 150 box: brown fur, peach face, hands and big feet, a green scarf.
  // The right arm is its own group so it can wave from the shoulder.
  function squatch(animated) {
    const c = (name) => (animated ? ' class="' + name + '"' : "");
    return '<svg viewBox="0 0 120 150" aria-hidden="true" focusable="false">' +
      '<ellipse cx="60" cy="144" rx="34" ry="4.5" fill="#000" opacity=".35"/>' +
      '<g' + c("sqh-body") + '>' +
        // legs and feet
        '<rect x="37" y="110" width="18" height="27" rx="9" fill="var(--sqh-fur)"/>' +
        '<rect x="65" y="110" width="18" height="27" rx="9" fill="var(--sqh-fur)"/>' +
        '<ellipse cx="42" cy="138" rx="16" ry="7" fill="var(--sqh-skin)"/>' +
        '<ellipse cx="78" cy="138" rx="16" ry="7" fill="var(--sqh-skin)"/>' +
        '<circle cx="29" cy="134.5" r="3.4" fill="var(--sqh-skin)"/><circle cx="34.5" cy="132" r="3.2" fill="var(--sqh-skin)"/><circle cx="40" cy="131.2" r="3" fill="var(--sqh-skin)"/>' +
        '<circle cx="91" cy="134.5" r="3.4" fill="var(--sqh-skin)"/><circle cx="85.5" cy="132" r="3.2" fill="var(--sqh-skin)"/><circle cx="80" cy="131.2" r="3" fill="var(--sqh-skin)"/>' +
        '<path d="M33 139q9 3 18 0M69 139q9 3 18 0" fill="none" stroke="var(--sqh-skin-dark)" stroke-width="1.4" stroke-linecap="round"/>' +
        // resting arm, then the body over its shoulder
        '<path d="M35 88Q22 101 27 114" fill="none" stroke="var(--sqh-fur)" stroke-width="15" stroke-linecap="round"/>' +
        '<circle cx="27" cy="116" r="7.5" fill="var(--sqh-skin)"/>' +
        '<path d="' + shaggy(60, 100, 36, 33, 18, 0.1) + '" fill="var(--sqh-fur)" stroke="var(--sqh-fur)" stroke-width="2" stroke-linejoin="round"/>' +
        '<path d="M34 90q-2 8 1 15M86 90q2 8-1 15M44 120q2 5 6 7M76 120q-2 5-6 7" fill="none" stroke="var(--sqh-fur-dark)" stroke-width="1.6" stroke-linecap="round" opacity=".7"/>' +
        '<ellipse cx="60" cy="106" rx="19" ry="19" fill="var(--sqh-skin)"/>' +
        '<path d="M53 99q3 2 6 0M61 104q3 2 6 0M54 110q3 2 6 0" fill="none" stroke="var(--sqh-skin-dark)" stroke-width="1.4" stroke-linecap="round"/>' +
        // the waving arm
        '<g' + c("sqh-arm") + '>' +
          '<path d="M85 88Q99 76 101 57" fill="none" stroke="var(--sqh-fur)" stroke-width="15" stroke-linecap="round"/>' +
          '<circle cx="95" cy="40" r="3.7" fill="var(--sqh-skin)"/>' +
          '<circle cx="101.5" cy="37" r="3.9" fill="var(--sqh-skin)"/>' +
          '<circle cx="108" cy="39.5" r="3.7" fill="var(--sqh-skin)"/>' +
          '<circle cx="111.5" cy="48" r="3.4" fill="var(--sqh-skin)"/>' +
          '<circle cx="102" cy="48" r="9" fill="var(--sqh-skin)"/>' +
          '<ellipse cx="102" cy="49.5" rx="4" ry="3.2" fill="var(--sqh-skin-dark)" opacity=".55"/>' +
        '</g>' +
        // scarf
        '<path d="M37 77Q60 91 83 77L81 86Q60 98 39 86Z" fill="var(--sqh-scarf)"/>' +
        '<path d="M55 89L66 89L61 102Z" fill="var(--sqh-scarf)"/>' +
        '<circle cx="60" cy="89" r="4" fill="var(--sqh-scarf-dark)"/>' +
        // head
        '<circle cx="33" cy="46" r="8" fill="var(--sqh-fur)"/><circle cx="33" cy="46" r="4" fill="var(--sqh-skin-dark)"/>' +
        '<circle cx="87" cy="46" r="8" fill="var(--sqh-fur)"/><circle cx="87" cy="46" r="4" fill="var(--sqh-skin-dark)"/>' +
        '<path d="M47 26L52 12L57 23L62 9L66 23L72 14L73 27Z" fill="var(--sqh-fur)"/>' +
        '<path d="' + shaggy(60, 50, 32, 31, 20, 0.11) + '" fill="var(--sqh-fur)" stroke="var(--sqh-fur)" stroke-width="2" stroke-linejoin="round"/>' +
        '<path d="M60 75c-12 0-20-6-20-14 0-4 1-7 3-9a11 11 0 0 1 17-5 11 11 0 0 1 17 5c2 2 3 5 3 9 0 8-8 14-20 14z" fill="var(--sqh-skin)"/>' +
        '<path d="M44 42Q50 37.5 56 41.5M64 41.5Q70 37.5 76 42" fill="none" stroke="var(--sqh-fur-dark)" stroke-width="2.6" stroke-linecap="round"/>' +
        '<g' + c("sqh-eyes") + '>' +
          '<ellipse cx="51" cy="52" rx="3.8" ry="4.8" fill="#24160f"/><ellipse cx="69" cy="52" rx="3.8" ry="4.8" fill="#24160f"/>' +
          '<circle cx="52.4" cy="50.2" r="1.4" fill="#fff"/><circle cx="70.4" cy="50.2" r="1.4" fill="#fff"/>' +
        '</g>' +
        '<ellipse cx="44" cy="61" rx="4.2" ry="2.8" fill="var(--sqh-cheek)" opacity=".6"/>' +
        '<ellipse cx="76" cy="61" rx="4.2" ry="2.8" fill="var(--sqh-cheek)" opacity=".6"/>' +
        '<ellipse cx="60" cy="59.5" rx="5" ry="3.4" fill="#5a3422"/>' +
        '<ellipse cx="58.4" cy="58.4" rx="1.6" ry=".8" fill="#fff" opacity=".5"/>' +
        '<path d="M52 65Q60 74 68 65Q60 68 52 65Z" fill="#8e3b2f"/>' +
        '<ellipse cx="60" cy="69" rx="3.4" ry="1.7" fill="#e0736a"/>' +
      '</g></svg>';
  }
  const ICON = {
    x: '<svg viewBox="0 0 12 12" aria-hidden="true"><path d="M2 2l8 8M10 2l-8 8" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/></svg>',
    down: '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M3.5 6l4.5 4.5L12.5 6" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>',
    send: '<svg viewBox="0 0 18 18" aria-hidden="true"><path d="M9 15V3.5M4 8.5L9 3.5l5 5" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  };

  const root = document.createElement("div");
  root.className = "sqh";
  root.hidden = true;                 // until its stylesheet is in, so it never flashes unstyled
  root.innerHTML =
    '<div class="sqh-panel" id="sqh-panel" role="dialog" aria-label="Chat with the Squatch helper" hidden>' +
      '<div class="sqh-top">' +
        '<span class="sqh-avatar">' + squatch(false) + '</span>' +
        '<span class="sqh-who"><b>Squatch helper</b><span>Pager and T-Deck help</span></span>' +
        '<button type="button" class="sqh-tool sqh-reset">Start over</button>' +
        '<button type="button" class="sqh-tool sqh-min" aria-label="Close the chat">' + ICON.down + '</button>' +
      '</div>' +
      '<div class="sqh-log" aria-live="polite">' +
        '<div class="sqh-msg assistant intro"><p>Hi! I\'m the Squatch helper. What\'s going on with your pager or T-Deck?</p>' +
          '<div class="sqh-chips">' +
            '<button type="button" data-ask="My computer doesn\'t see the pager when I try to install.">Installer can\'t find it</button>' +
            '<button type="button" data-ask="How do I update my firmware?">Updating</button>' +
            '<button type="button" data-ask="My pager won\'t turn on or start properly.">Won\'t start</button>' +
            '<button type="button" data-ask="I have an idea for a feature.">I have an idea</button>' +
          '</div></div>' +
      '</div>' +
      '<form class="sqh-form">' +
        '<label class="sqh-sr" for="sqh-input">Your message</label>' +
        '<textarea id="sqh-input" rows="1" maxlength="2000" placeholder="Type your question…" autocomplete="off"></textarea>' +
        '<button type="submit" class="sqh-send" aria-label="Send">' + ICON.send + '</button>' +
      '</form>' +
      '<p class="sqh-fine">Answers come from Claude, an AI, and can be wrong. Never share backups, keys or channel secrets. ' +
        '<a href="/help">More help</a> &middot; <a href="/privacy#helper">Privacy</a></p>' +
    '</div>' +
    '<div class="sqh-hello" hidden>Need a hand?</div>' +
    '<button type="button" class="sqh-close" aria-label="Hide the sasquatch until the page reloads">' + ICON.x + '</button>' +
    '<button type="button" class="sqh-squatch" aria-label="Chat with the Squatch helper" aria-expanded="false" aria-controls="sqh-panel">' +
      squatch(true) + '</button>';

  const panel = root.querySelector(".sqh-panel");
  const log = root.querySelector(".sqh-log");
  const form = root.querySelector(".sqh-form");
  const input = root.querySelector("#sqh-input");
  const send = root.querySelector(".sqh-send");
  const button = root.querySelector(".sqh-squatch");
  const hello = root.querySelector(".sqh-hello");

  let messages = [];
  let pendingHandoff = null;
  let busy = false;

  const esc = (s) => String(s).replace(/[&<>"']/g, (ch) =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[ch]));

  // As on /help: escape first, then links, **bold**, `code` and line breaks.
  function format(text) {
    let h = esc(text);
    h = h.replace(/\bhttps:\/\/[^\s<)]+[^\s<).,;:!?]/g, (u) => '<a href="' + u + '" rel="noopener">' + u + "</a>");
    h = h.replace(/\*\*([^*\n]+)\*\*/g, "<b>$1</b>");
    h = h.replace(/`([^`\n]+)`/g, "<code>$1</code>");
    return h.split(/\n{2,}/).map((p) => "<p>" + p.replace(/\n/g, "<br>") + "</p>").join("");
  }

  function bubble(role, text, extra, html) {
    const div = document.createElement("div");
    div.className = "sqh-msg " + role + (extra ? " " + extra : "");
    div.innerHTML = html ? html : role === "user" ? "<p>" + esc(text).replace(/\n/g, "<br>") + "</p>" : format(text);
    log.appendChild(div);
    log.scrollTop = log.scrollHeight;
    return div;
  }

  function save() {
    try { sessionStorage.setItem(KEY, JSON.stringify({ messages, pendingHandoff })); } catch (e) { /* fine */ }
  }

  // Whatever this tab said to the helper so far, here or on another page.
  function load() {
    let saved = null;
    try { saved = JSON.parse(sessionStorage.getItem(KEY) || "null"); } catch (e) { /* start fresh */ }
    const list = saved && Array.isArray(saved.messages) ? saved.messages : [];
    if (list.length === messages.length && log.querySelectorAll(".sqh-msg:not(.intro)").length) return;
    messages = list;
    pendingHandoff = saved ? saved.pendingHandoff || null : null;
    log.querySelectorAll(".sqh-msg:not(.intro)").forEach((m) => m.remove());
    messages.forEach((m) => bubble(m.role, m.content));
  }

  const toHelp = '<p>For more than a quick chat, <a href="/help#handoff">the help page</a> has room to write to the developer.</p>';

  async function ask(text) {
    if (busy) return;
    if (messages.length >= 22) {
      bubble("assistant", "", "note", "<p>This conversation is getting long.</p>" + toHelp);
      return;
    }
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
        input.value = text;
        if (j.error === "offline" || r.status >= 500) {
          bubble("assistant", "", "note", "<p>I'm taking a break right now.</p>" + toHelp);
        } else {
          bubble("assistant", j.error || "Something went wrong. Try again.", "note");
        }
      } else {
        messages.push({ role: "assistant", content: j.reply });
        bubble("assistant", j.reply);
        if (j.handoff) {
          pendingHandoff = j.handoff;
          bubble("assistant", "", "note", "<p>I've passed this to the developer. Want a reply? " +
            '<a href="/help#handoff">Leave your email on the help page</a>; this chat goes with you.</p>');
        }
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
    if (!panel.hidden) input.focus();
  }

  const fitInput = () => { input.style.height = "auto"; input.style.height = Math.min(input.scrollHeight + 2, 120) + "px"; };

  form.addEventListener("submit", (e) => {
    e.preventDefault();
    const text = input.value.trim();
    if (!text) return;
    input.value = "";
    fitInput();
    ask(text);
  });
  input.addEventListener("keydown", (e) => {
    if (e.key === "Enter" && !e.shiftKey) { e.preventDefault(); form.requestSubmit(); }
  });
  input.addEventListener("input", fitInput);
  root.querySelectorAll("[data-ask]").forEach((b) => b.addEventListener("click", () => ask(b.getAttribute("data-ask"))));
  root.querySelector(".sqh-reset").addEventListener("click", () => {
    messages = [];
    pendingHandoff = null;
    log.querySelectorAll(".sqh-msg:not(.intro)").forEach((m) => m.remove());
    save();
    input.focus();
  });

  // ---- opening, closing, waving --------------------------------------------------------
  let excitedTimer = 0;
  function excite() {
    root.classList.add("excited");
    clearTimeout(excitedTimer);
    excitedTimer = setTimeout(() => root.classList.remove("excited"), 1100);
  }
  function hideHello() {
    if (hello.hidden) return;
    hello.classList.add("gone");
    setTimeout(() => { hello.hidden = true; }, 260);
  }
  function setOpen(open) {
    panel.hidden = !open;
    root.classList.toggle("open", open);
    button.setAttribute("aria-expanded", String(open));
    if (open) {
      hideHello();
      load();
      log.scrollTop = log.scrollHeight;
      if (window.matchMedia("(hover: hover)").matches) input.focus();
    }
  }
  function onSquatch() {
    excite();
    hideHello();
    if (pageChat) {               // /help: the big chat is right there
      pageChat.scrollIntoView({ behavior: "smooth", block: "center" });
      const box = document.getElementById("chat-input");
      if (box) setTimeout(() => box.focus({ preventScroll: true }), 350);
      return;
    }
    setOpen(panel.hidden);
  }
  button.addEventListener("click", onSquatch);
  hello.addEventListener("click", onSquatch);
  root.querySelector(".sqh-min").addEventListener("click", () => { setOpen(false); button.focus(); });
  root.querySelector(".sqh-close").addEventListener("click", () => { root.hidden = true; });
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && !panel.hidden) { setOpen(false); button.focus(); }
  });

  // "Need a hand?" once per tab, a few seconds in, then out of the way.
  let greeted = false;
  try { greeted = sessionStorage.getItem(HELLO) === "1"; } catch (e) { /* fine */ }
  if (!greeted && !pageChat) {
    setTimeout(() => {
      if (root.hidden || !panel.hidden) return;
      hello.hidden = false;
      excite();
      try { sessionStorage.setItem(HELLO, "1"); } catch (e) { /* fine */ }
      setTimeout(hideHello, 9000);
    }, 2500);
  }

  // Show him once his stylesheet is in (or after a moment regardless).
  const css = document.createElement("link");
  css.rel = "stylesheet";
  css.href = "/assets/squatch-helper.css?v=1";
  let shown = false;
  const show = () => { if (!shown) { shown = true; root.hidden = false; } };
  css.addEventListener("load", show);
  setTimeout(show, 2500);
  document.head.appendChild(css);
  document.body.appendChild(root);
})();
