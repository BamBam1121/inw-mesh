/* Squatch Mesh site behaviour: header, mobile menu, theme switcher, live version,
   releases list and the privacy notice. Nothing here is needed to read a page. */
(function () {
  const SITE = "https://bambam1121.github.io/inw-mesh/firmware/";

  /* header: a border once scrolled, and mark the current page */
  const nav = document.querySelector("header.nav");
  if (nav) {
    const onScroll = () => nav.classList.toggle("stuck", window.scrollY > 8);
    window.addEventListener("scroll", onScroll, { passive: true });
    onScroll();
    const here = "/" + (location.pathname.split("/").pop() || "").replace(/\.html$/, "");
    nav.querySelectorAll("a.link").forEach((a) => {
      if (a.getAttribute("href") === here) a.setAttribute("aria-current", "page");
    });

    /* mobile menu */
    const toggle = nav.querySelector(".nav-toggle");
    const links = nav.querySelector(".nav-links");
    if (toggle && links) {
      const set = (open) => {
        toggle.setAttribute("aria-expanded", String(open));
        toggle.setAttribute("aria-label", open ? "Close menu" : "Open menu");
        links.classList.toggle("open", open);
      };
      toggle.addEventListener("click", () => set(toggle.getAttribute("aria-expanded") !== "true"));
      document.addEventListener("keydown", (e) => { if (e.key === "Escape") set(false); });
      links.addEventListener("click", (e) => { if (e.target.closest("a")) set(false); });
    }
  }

  /* theme switcher: tabs over four panels; with no JS every panel just shows */
  document.querySelectorAll("[data-theme-switcher]").forEach((root) => {
    const tabs = [...root.querySelectorAll('[role="tab"]')];
    const panels = [...root.querySelectorAll('[role="tabpanel"]')];
    const select = (i, focus) => {
      tabs.forEach((t, j) => {
        t.setAttribute("aria-selected", String(i === j));
        t.tabIndex = i === j ? 0 : -1;
      });
      panels.forEach((p, j) => { p.hidden = i !== j; });
      if (focus) tabs[i].focus();
    };
    tabs.forEach((t, i) => {
      t.addEventListener("click", () => select(i));
      t.addEventListener("keydown", (e) => {
        if (e.key === "ArrowRight") select((i + 1) % tabs.length, true);
        if (e.key === "ArrowLeft") select((i - 1 + tabs.length) % tabs.length, true);
      });
    });
    select(0);
  });

  /* live version badge: ota.json, then version.txt, else leave the placeholder */
  (async () => {
    const slots = document.querySelectorAll("[data-version]");
    if (!slots.length) return;
    const set = (txt) => slots.forEach((s) => { s.textContent = txt; });
    try {
      const r = await fetch(SITE + "ota.json", { cache: "no-store" });
      if (r.ok) { const j = await r.json(); if (j.version) { set("v" + j.version); return; } }
    } catch (e) { /* offline or blocked */ }
    try {
      const r = await fetch(SITE + "version.txt", { cache: "no-store" });
      if (r.ok) { const t = (await r.text()).trim().split(/\s+/)[0]; if (t) set(t); }
    } catch (e) { /* leave placeholder */ }
  })();

  /* releases page: real data from GitHub, cached briefly so we don't hammer the API */
  window.loadReleases = async function (elId) {
    const box = document.getElementById(elId);
    if (!box) return;
    const KEY = "sm-releases";
    try {
      const cached = sessionStorage.getItem(KEY);
      if (cached) {
        const { at, data } = JSON.parse(cached);
        if (Date.now() - at < 600000) return render(box, data);
      }
    } catch (e) { /* storage blocked */ }
    try {
      const r = await fetch("https://api.github.com/repos/BamBam1121/inw-mesh/releases");
      if (!r.ok) throw new Error(r.status);
      const data = await r.json();
      try { sessionStorage.setItem(KEY, JSON.stringify({ at: Date.now(), data })); } catch (e) { /* fine */ }
      render(box, data);
    } catch (e) {
      box.innerHTML = '<div class="panel">Could not reach GitHub. ' +
        '<a href="https://github.com/BamBam1121/inw-mesh/releases">See the releases on GitHub</a>.</div>';
    }
  };
  const esc = (s) => String(s).replace(/[&<>"']/g, (c) =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  // The little markdown release notes use: ## headings, - lists, **bold**, *italic*
  // and [links](https://...). Everything is escaped first, so only these tags come out.
  const inline = (s) => esc(s)
    .replace(/\[([^\]]+)\]\((https?:\/\/[^)\s]+)\)/g, '<a href="$2">$1</a>')
    .replace(/\*\*(.+?)\*\*/g, "<b>$1</b>")
    .replace(/(^|[^*])\*([^*\s][^*]*?)\*(?!\*)/g, "$1<i>$2</i>");
  const notes = (md) => {
    let out = "", list = false;
    for (const raw of String(md).split(/\r?\n/)) {
      const line = raw.trim(), item = /^[-*] +(.*)/.exec(line), head = /^#{1,4} +(.*)/.exec(line);
      if (list && !item) { out += "</ul>"; list = false; }
      if (item) {
        if (!list) { out += '<ul style="margin:6px 0 10px;padding-left:20px">'; list = true; }
        out += '<li style="margin:3px 0">' + inline(item[1]) + "</li>";
      } else if (head) out += '<h4 style="margin:14px 0 4px">' + inline(head[1]) + "</h4>";
      else if (line) out += '<p style="margin:0 0 8px">' + inline(line) + "</p>";
    }
    return out + (list ? "</ul>" : "");
  };
  // The releases page fills this in; no inline script needed, so the CSP can forbid them.
  if (document.getElementById("releases")) window.loadReleases("releases");

  /* home page: the last few releases for every board, newest first. Betas are left
     out (T-Deck and M9 releases are GitHub pre-releases, so go by the name instead).
     The static line in the page stays if GitHub can't be reached. */
  const latest = document.getElementById("latest");
  if (latest) (async () => {
    let data = null;
    try {
      const c = sessionStorage.getItem("sm-releases");
      if (c) { const { at, data: d } = JSON.parse(c); if (Date.now() - at < 600000) data = d; }
    } catch (e) { /* storage blocked */ }
    if (!data) {
      try {
        const r = await fetch("https://api.github.com/repos/BamBam1121/inw-mesh/releases");
        if (!r.ok) return;
        data = await r.json();
        try { sessionStorage.setItem("sm-releases", JSON.stringify({ at: Date.now(), data })); } catch (e) { /* fine */ }
      } catch (e) { return; }
    }
    const board = (tag) => /^tdeck-/.test(tag) ? "T-Deck" : /^m9-/.test(tag) ? "ThinkNode M9" : "T-Lora Pager";
    const rows = (data || []).filter((r) => !/beta|rc/i.test(r.tag_name || "") && !r.draft).slice(0, 5).map((r) => {
      const tag = r.tag_name || "";
      const ver = tag.replace(/^(tdeck-|m9-)?v/, "");
      let what = (r.name || "").replace(/^(T-Deck|ThinkNode M9)\s+/, "").trim();
      what = what.startsWith(ver) ? what.slice(ver.length).replace(/^\s*-\s*/, "") : "";
      const date = new Date(r.published_at || r.created_at).toLocaleDateString(undefined, { year: "numeric", month: "short", day: "numeric" });
      return '<li><span class="px">' + esc(date) + "</span>" + '<a href="' + esc(r.html_url) + '">' + esc(board(tag) + " " + ver) + "</a>" +
        (what ? ": " + esc(what) : "") + "</li>";
    });
    if (rows.length) latest.innerHTML = rows.join("");
  })();

  function render(box, list) {
    // The pager's releases. T-Deck betas are pre-releases tagged tdeck-v*.
    list = (list || []).filter((rel) => !rel.prerelease && !/^tdeck-/.test(rel.tag_name || ""));
    if (!list.length) { box.innerHTML = '<div class="panel">No releases yet.</div>'; return; }
    box.innerHTML = list.map((rel, i) => {
      const date = new Date(rel.published_at || rel.created_at).toLocaleDateString(undefined, { year: "numeric", month: "short", day: "numeric" });
      const body = notes(rel.body || "");
      return '<article class="panel" style="margin-bottom:14px">' +
        '<div class="tag">' + esc(date) + (i === 0 ? " &middot; latest" : "") + "</div>" +
        '<h3 style="margin-top:8px">' + esc(rel.tag_name || rel.name) + "</h3>" +
        '<div style="color:var(--muted);font-size:15px">' + body + "</div></article>";
    }).join("");
  }
})();

/* Privacy notice: a small card until someone closes it. The choice is kept in
   localStorage, not a cookie, so the notice stays true. Visible by default. */
(function () {
  const KEY = "sm-privacy-notice";
  try { if (localStorage.getItem(KEY)) return; } catch (e) { /* storage blocked: show it */ }
  const box = document.createElement("div");
  box.className = "notice";
  box.setAttribute("role", "region");
  box.setAttribute("aria-label", "Privacy notice");
  box.innerHTML =
    '<p>This site uses <b>no cookies</b>: no tracking, no analytics. Cloudflare may set a ' +
    'short-lived security cookie if it needs to check you are not a bot. <a href="/privacy">Privacy</a></p>' +
    '<button type="button" class="btn solid">Got it</button>';
  box.querySelector("button").addEventListener("click", () => {
    try { localStorage.setItem(KEY, "1"); } catch (e) { /* it just shows again */ }
    box.classList.add("gone");
    setTimeout(() => box.remove(), 250);
  });
  const show = () => document.body.appendChild(box);
  if (document.body) show(); else document.addEventListener("DOMContentLoaded", show);
})();
