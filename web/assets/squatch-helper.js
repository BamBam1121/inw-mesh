/* The sasquatch at the right edge of every page. On the first page of a visit he knocks
   on the inside of the screen, breaks through its edge and leans out of the hole; on
   later pages he pops straight out. He waves, blinks, watches the pointer and ducks
   back in now and then. A click opens a small chat with the same helper as /help (the
   help desk on the site's own server). The conversation is kept in this tab under the
   help page's own key, so it carries on from page to page and onto /help. The X sends
   him back in and seals the hole until the page is reloaded. On /help itself a click
   takes you to the chat on the page instead. */
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
  // The corner scene in a 170 x 210 box whose right side is the screen's right edge:
  // a hole broken through the edge with cracks around it, the pieces that fly off, and
  // the sasquatch leaning out of it - one hand on the rim, the other waving.
  const HOLE = "M170 26L152 33L143 22L130 38L117 33L108 50L95 47L90 64L78 69L83 86L70 95L77 110L66 122L75 137" +
    "L68 151L81 160L76 174L90 181L95 196L112 191L121 204L138 195L150 206L170 199Z";
  const CRACKS_A = ["M108 50L99 31L101 17", "M78 69L60 61L49 66", "M70 95L52 92"];
  const CRACKS_B = ["M66 122L47 126L36 138", "M68 151L52 160L47 172", "M95 196L88 208", "M130 38L127 18L118 6",
    "M101 17L92 9", "M49 66L40 58", "M47 126L44 112", "M52 92L41 99"];
  const SHARDS = [
    ["M84 80L95 72L92 88Z", -64, -34, -220], ["M70 104L80 98L82 112Z", -86, 8, 260], ["M72 140L84 132L83 148Z", -74, 44, -180],
    ["M100 52L112 48L106 60Z", -42, -66, 200], ["M86 168L96 162L98 176Z", -58, 64, 240], ["M120 192L132 196L124 204Z", -24, 74, -160],
    ["M135 30L146 26L142 38Z", -34, -74, 300], ["M77 122L84 118L83 126Z", -104, -12, 320], ["M92 50L97 46L98 54Z", -70, -58, -280],
  ];
  function scene() {
    const crack = (d, k) => '<path class="sqh-crack ' + k + '" pathLength="1" d="' + d + '"/>';
    return '<svg viewBox="0 0 170 210" aria-hidden="true" focusable="false">' +
      '<defs>' +
        '<linearGradient id="sqh-sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#0c1c3d"/>' +
          '<stop offset="0.6" stop-color="#1b3a6b"/><stop offset="1" stop-color="#2d5a7a"/></linearGradient>' +
        '<clipPath id="sqh-clip"><rect x="-300" y="-300" width="470" height="510"/></clipPath>' +
        '<clipPath id="sqh-holeclip"><path d="' + HOLE + '"/></clipPath>' +
      '</defs>' +
      '<g class="sqh-cracks" fill="none" stroke="var(--sqh-rim)" stroke-opacity=".5" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round">' +
        CRACKS_A.map((d) => crack(d, "a")).join("") + CRACKS_B.map((d) => crack(d, "b")).join("") +
      '</g>' +
      '<g class="sqh-hole">' +
        // through the hole: his world, a night sky over the pines
        '<path class="sqh-hole-fill" d="' + HOLE + '" fill="url(#sqh-sky)" stroke="#000" stroke-opacity=".75" stroke-width="6" stroke-linejoin="round"/>' +
        '<g clip-path="url(#sqh-holeclip)">' +
          '<g class="sqh-stars" fill="#fff">' +
            '<circle cx="150" cy="45" r="1.1"/><circle cx="138" cy="57" r=".8"/><circle cx="86" cy="73" r=".9"/><circle cx="79" cy="104" r="1.2"/>' +
            '<circle cx="88" cy="150" r=".8"/><circle cx="82" cy="127" r=".7"/><circle cx="161" cy="60" r="1"/><circle cx="100" cy="60" r=".7"/>' +
            '<circle cx="93" cy="88" r=".6"/><circle cx="157" cy="34" r=".7"/>' +
          '</g>' +
          '<path d="M150 50a9 9 0 1 0 7 15 7 7 0 1 1-7-15z" fill="#f4f1d0" opacity=".9"/>' +
          '<path d="M70 210L82 172L94 210ZM86 210L99 164L112 210ZM104 210L114 180L124 210ZM118 210L131 170L144 210ZM138 210L150 176L162 210Z" fill="#0a2a1e"/>' +
        '</g>' +
        '<path class="sqh-rim" d="' + HOLE + '" fill="none" stroke="var(--sqh-rim)" stroke-opacity=".6" stroke-width="1.6" stroke-linejoin="round"/>' +
        '<g class="sqh-glow"><circle cx="131" cy="112" r="7" fill="#e8ff9a" opacity=".25"/><circle cx="149" cy="112" r="7" fill="#e8ff9a" opacity=".25"/>' +
          '<ellipse cx="131" cy="112" rx="3.2" ry="2.4" fill="#f4ffc4"/><ellipse cx="149" cy="112" rx="3.2" ry="2.4" fill="#f4ffc4"/></g>' +
      '</g>' +
      '<g clip-path="url(#sqh-clip)"><g class="sqh-out"><g class="sqh-lean"><g class="sqh-breath sqh-drop">' +
        // the arm reaching up to grip the rim, behind his head
        '<path d="M168 76Q152 52 129 43" fill="none" stroke="var(--sqh-fur)" stroke-width="12" stroke-linecap="round"/>' +
        // body
        '<path d="' + shaggy(144, 178, 44, 48, 16, 0.1) + '" fill="var(--sqh-fur)" stroke="var(--sqh-fur)" stroke-width="2" stroke-linejoin="round"/>' +
        // shoulders and neck, so the head sits on the body
        '<path d="' + shaggy(120, 146, 36, 26, 14, 0.1) + '" fill="var(--sqh-fur)" stroke="var(--sqh-fur)" stroke-width="2" stroke-linejoin="round"/>' +
        '<ellipse cx="136" cy="186" rx="20" ry="22" fill="var(--sqh-skin)"/>' +
        '<path d="M129 179q3 2 6 0M137 185q3 2 6 0" fill="none" stroke="var(--sqh-skin-dark)" stroke-width="1.4" stroke-linecap="round"/>' +
        // scarf
        '<path d="M88 126Q112 141 139 128L137 137Q112 151 90 135Z" fill="var(--sqh-scarf)"/>' +
        '<path d="M108 141L120 141L113 156Z" fill="var(--sqh-scarf)"/>' +
        '<circle cx="114" cy="140" r="4.5" fill="var(--sqh-scarf-dark)"/>' +
        // the waving arm
        '<g class="sqh-arm">' +
          '<path d="M108 160Q90 156 72 140" fill="none" stroke="var(--sqh-fur)" stroke-width="14" stroke-linecap="round"/>' +
          '<circle cx="56" cy="124" r="3.6" fill="var(--sqh-skin)"/><circle cx="61" cy="119.5" r="3.8" fill="var(--sqh-skin)"/>' +
          '<circle cx="67.5" cy="118.5" r="3.7" fill="var(--sqh-skin)"/><circle cx="73" cy="122" r="3.4" fill="var(--sqh-skin)"/>' +
          '<circle cx="55" cy="136" r="3.2" fill="var(--sqh-skin)"/>' +
          '<circle cx="64" cy="132" r="9.5" fill="var(--sqh-skin)"/>' +
          '<ellipse cx="64" cy="133.5" rx="4.2" ry="3.3" fill="var(--sqh-skin-dark)" opacity=".55"/>' +
        '</g>' +
        // head
        '<g class="sqh-head">' +
          '<circle cx="80" cy="96" r="8" fill="var(--sqh-fur)"/><circle cx="80" cy="96" r="4" fill="var(--sqh-skin-dark)"/>' +
          '<circle cx="144" cy="90" r="8" fill="var(--sqh-fur)"/><circle cx="144" cy="90" r="4" fill="var(--sqh-skin-dark)"/>' +
          '<path d="M99 70L104 56L109 67L114 53L118 67L124 58L125 71Z" fill="var(--sqh-fur)"/>' +
          '<path d="' + shaggy(112, 98, 33, 32, 20, 0.11) + '" fill="var(--sqh-fur)" stroke="var(--sqh-fur)" stroke-width="2" stroke-linejoin="round"/>' +
          '<path d="M110 121c-12 0-20-6-20-14 0-4 1-7 3-9a11 11 0 0 1 17-5 11 11 0 0 1 17 5c2 2 3 5 3 9 0 8-8 14-20 14z" fill="var(--sqh-skin)"/>' +
          '<path d="M93 86Q99 81 106 85.5M114 85.5Q121 81 127 86" fill="none" stroke="var(--sqh-fur-dark)" stroke-width="2.6" stroke-linecap="round"/>' +
          '<g class="sqh-eyes">' +
            '<ellipse cx="101" cy="97" rx="5.2" ry="6.2" fill="#fff"/><ellipse cx="119" cy="97" rx="5.2" ry="6.2" fill="#fff"/>' +
            '<g class="sqh-pupils"><ellipse cx="101.5" cy="98" rx="3.3" ry="4.1" fill="#24160f"/><ellipse cx="119.5" cy="98" rx="3.3" ry="4.1" fill="#24160f"/>' +
              '<circle cx="102.8" cy="96.3" r="1.3" fill="#fff"/><circle cx="120.8" cy="96.3" r="1.3" fill="#fff"/></g>' +
          '</g>' +
          '<ellipse cx="93" cy="108" rx="4.2" ry="2.8" fill="var(--sqh-cheek)" opacity=".6"/>' +
          '<ellipse cx="127" cy="108" rx="4.2" ry="2.8" fill="var(--sqh-cheek)" opacity=".6"/>' +
          '<ellipse cx="110" cy="106" rx="5" ry="3.4" fill="#5a3422"/>' +
          '<ellipse cx="108.4" cy="104.9" rx="1.6" ry=".8" fill="#fff" opacity=".5"/>' +
          '<path d="M102 111Q110 120 118 111Q110 114 102 111Z" fill="#8e3b2f"/>' +
          '<ellipse cx="110" cy="115" rx="3.4" ry="1.7" fill="#e0736a"/>' +
        '</g>' +
        // the hand on the rim, fingers over the broken edge
        '<circle cx="128" cy="42" r="7" fill="var(--sqh-skin)"/>' +
        '<circle cx="120.5" cy="38.5" r="3.2" fill="var(--sqh-skin)"/><circle cx="123.5" cy="34.6" r="3.2" fill="var(--sqh-skin)"/>' +
        '<circle cx="128.6" cy="33.2" r="3.1" fill="var(--sqh-skin)"/>' +
      '</g></g></g></g>' +
      '<g class="sqh-shards" fill="#1b2b24" stroke="var(--sqh-rim)" stroke-opacity=".55" stroke-width=".8" stroke-linejoin="round">' +
        SHARDS.map((s) => '<path class="sqh-shard" d="' + s[0] + '" style="--dx:' + s[1] + 'px;--dy:' + s[2] + 'px;--r:' + s[3] + 'deg"/>').join("") +
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
    '<button type="button" class="sqh-close" aria-label="Send the sasquatch away until the page reloads">' + ICON.x + '</button>' +
    '<button type="button" class="sqh-squatch" aria-label="Chat with the Squatch helper" aria-expanded="false" aria-controls="sqh-panel">' +
      scene() + '</button>';

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
  const BROKE = "sm-helper-broke";    // the full break-in plays once per tab
  const still = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  const close = root.querySelector(".sqh-close");
  const pupils = root.querySelector(".sqh-pupils");
  let leaving = false, hovering = false;

  const later = (ms, fn) => setTimeout(fn, ms);
  let excitedTimer = 0;
  function excite() {
    root.classList.add("excited");
    clearTimeout(excitedTimer);
    excitedTimer = later(1000, () => root.classList.remove("excited"));
  }
  function hideHello() {
    if (hello.hidden) return;
    hello.classList.add("gone");
    later(260, () => { hello.hidden = true; });
  }
  function setOpen(open) {
    panel.hidden = !open;
    root.classList.toggle("open", open);
    button.setAttribute("aria-expanded", String(open));
    look(open ? -1 : 0, open ? 0.2 : 0);
    if (open) {
      hideHello();
      load();
      log.scrollTop = log.scrollHeight;
      if (window.matchMedia("(hover: hover)").matches) input.focus();
    }
  }
  function onSquatch() {
    if (leaving) return;
    excite();
    hideHello();
    if (pageChat) {               // /help: the big chat is right there
      pageChat.scrollIntoView({ behavior: "smooth", block: "center" });
      const box = document.getElementById("chat-input");
      if (box) later(350, () => box.focus({ preventScroll: true }));
      return;
    }
    setOpen(panel.hidden);
  }
  button.addEventListener("click", onSquatch);
  hello.addEventListener("click", onSquatch);
  button.addEventListener("pointerenter", () => { hovering = true; });
  button.addEventListener("pointerleave", () => { hovering = false; });
  root.querySelector(".sqh-min").addEventListener("click", () => { setOpen(false); button.focus(); });
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && !panel.hidden) { setOpen(false); button.focus(); }
  });

  // The X: a goodbye wave, back into the hole, the hole seals, gone until a reload.
  close.addEventListener("click", () => {
    if (leaving) return;
    leaving = true;
    setOpen(false);
    root.classList.remove("enter", "enter-quick", "duck", "excited");
    root.classList.add("leave");
    later(still ? 0 : 1500, () => { root.hidden = true; });
  });

  // His eyes follow the pointer (or the chat, while it's open).
  function look(x, y) {
    if (pupils) pupils.style.transform = "translate(" + (x * 2.2).toFixed(2) + "px," + (y * 2.2).toFixed(2) + "px)";
  }
  let lookFrame = 0;
  if (!still) {
    document.addEventListener("pointermove", (e) => {
      if (lookFrame || !panel.hidden || e.pointerType === "touch") return;
      lookFrame = requestAnimationFrame(() => {
        lookFrame = 0;
        const r = pupils.getBoundingClientRect();
        const dx = e.clientX - (r.left + r.width / 2), dy = e.clientY - (r.top + r.height / 2);
        const d = Math.hypot(dx, dy) || 1;
        const k = Math.min(1, d / 120);
        look((dx / d) * k, (dy / d) * k);
      });
    }, { passive: true });
  }

  // Now and then he ducks back into the hole and pops out again.
  function scheduleDuck() {
    later(16000 + Math.random() * 14000, () => {
      if (leaving) return;
      if (!hovering && panel.hidden && !document.hidden) {
        root.classList.add("duck");
        later(1900, () => root.classList.remove("duck"));
      }
      scheduleDuck();
    });
  }

  // "Need a hand?" once per tab, once he's out, then out of the way.
  function greet(at) {
    let greeted = false;
    try { greeted = sessionStorage.getItem(HELLO) === "1"; } catch (e) { /* fine */ }
    if (greeted || pageChat) return;
    later(at, () => {
      if (root.hidden || leaving || !panel.hidden) return;
      hello.hidden = false;
      try { sessionStorage.setItem(HELLO, "1"); } catch (e) { /* fine */ }
      later(9000, hideHello);
    });
  }

  // The entrance: the whole break-in on the first page of a visit, a quick pop-out after.
  function enter() {
    let first = true;
    try { first = sessionStorage.getItem(BROKE) !== "1"; sessionStorage.setItem(BROKE, "1"); } catch (e) { /* fine */ }
    if (still) { root.classList.add("idle"); greet(800); return; }
    root.classList.add(first ? "enter" : "enter-quick");
    later(first ? 4100 : 1600, () => {
      root.classList.remove("enter", "enter-quick");
      if (!leaving) { root.classList.add("idle"); scheduleDuck(); }
    });
    greet(first ? 3300 : 1500);
  }

  // Show him once his stylesheet is in (or after a moment regardless).
  const css = document.createElement("link");
  css.rel = "stylesheet";
  css.href = "/assets/squatch-helper.css?v=3";
  let shown = false;
  const show = () => { if (!shown) { shown = true; root.hidden = false; enter(); } };
  css.addEventListener("load", show);
  later(2500, show);
  document.head.appendChild(css);
  document.body.appendChild(root);
})();
