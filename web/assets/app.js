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
  function render(box, list) {
    if (!list || !list.length) { box.innerHTML = '<div class="panel">No releases yet.</div>'; return; }
    box.innerHTML = list.map((rel, i) => {
      const date = new Date(rel.published_at || rel.created_at).toLocaleDateString(undefined, { year: "numeric", month: "short", day: "numeric" });
      const body = esc(rel.body || "").replace(/\r?\n/g, "<br>");
      return '<article class="panel" style="margin-bottom:14px">' +
        '<div class="tag">' + esc(date) + (i === 0 ? " &middot; latest" : "") + "</div>" +
        '<h3 style="margin-top:8px">' + esc(rel.tag_name || rel.name) + "</h3>" +
        '<div style="color:var(--muted);font-size:15px">' + body + "</div></article>";
    }).join("");
  }
})();

/* Privacy notice: a small card until someone closes it. The choice is kept in
   localStorage, not a cookie, so the notice stays true. Visible by default: the
   slide-in is decoration and never gates it. */
(function () {
  const KEY = "sm-privacy-notice";
  try { if (localStorage.getItem(KEY)) return; } catch (e) { /* storage blocked: show it */ }
  const box = document.createElement("div");
  box.className = "notice";
  box.setAttribute("role", "region");
  box.setAttribute("aria-label", "Privacy notice");
  box.innerHTML =
    '<p>This site uses <b>no cookies</b> &mdash; no tracking, no analytics. Cloudflare may set a ' +
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
