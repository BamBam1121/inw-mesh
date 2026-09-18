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
  // The releases page fills this in; no inline script needed, so the CSP can forbid them.
  if (document.getElementById("releases")) window.loadReleases("releases");

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

/* Motion. Decoration only: every piece checks reduced-motion, and nothing is
   hidden until .motion is on <html>, with a timer that shows everything anyway
   in case scrolling never fires (screenshots, odd browsers, print). */
(function () {
  const reduce = window.matchMedia && matchMedia("(prefers-reduced-motion: reduce)").matches;
  const run = () => {
    /* scroll reveal: section heads, cards, panels and the blocks of inner pages */
    if (!reduce && "IntersectionObserver" in window) {
      const sel = ".section-head, .grid > *, .stats, .cta, .themes, .faq details, main.wrap > :not(.tag):not(h1):not(.sub):not(:first-child), .page-head ~ section .wrap > *";
      const els = [...document.querySelectorAll(sel)].filter((el) =>
        !el.closest(".hero") && el.getBoundingClientRect().top > window.innerHeight * 0.9);
      if (els.length) {
        els.forEach((el) => {
          el.classList.add("rv");
          const sibs = el.parentElement ? [...el.parentElement.children].filter((c) => c.classList.contains("rv")) : [];
          const i = Math.max(0, sibs.indexOf(el));
          if (el.parentElement && el.parentElement.classList.contains("grid")) el.style.setProperty("--rv-d", Math.min(i, 5) * 0.08 + "s");
        });
        document.documentElement.classList.add("motion");
        const io = new IntersectionObserver((entries) => {
          entries.forEach((e) => { if (e.isIntersecting) { e.target.classList.add("in"); io.unobserve(e.target); } });
        }, { rootMargin: "0px 0px -8% 0px", threshold: 0.08 });
        els.forEach((el) => io.observe(el));
        // failsafe: nothing stays hidden for long, whatever happens
        setTimeout(() => els.forEach((el) => el.classList.add("in")), 4000);
        window.addEventListener("beforeprint", () => els.forEach((el) => el.classList.add("in")));
      }
    }

    /* the mesh behind the hero: nodes, links, and packets hopping between them */
    const hero = document.querySelector(".hero");
    if (hero && !reduce) {
      const W = 1200, H = 700, NS = "http://www.w3.org/2000/svg";
      let seed = 7;
      const rnd = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
      const nodes = [];
      for (let i = 0; i < 26; i++) nodes.push([60 + rnd() * (W - 120), 50 + rnd() * (H - 100)]);
      const svg = document.createElementNS(NS, "svg");
      svg.setAttribute("class", "mesh-bg");
      svg.setAttribute("viewBox", `0 0 ${W} ${H}`);
      svg.setAttribute("preserveAspectRatio", "xMidYMid slice");
      svg.setAttribute("aria-hidden", "true");
      const mk = (tag, attrs) => { const e = document.createElementNS(NS, tag); for (const k in attrs) e.setAttribute(k, attrs[k]); svg.appendChild(e); return e; };
      const edges = [];
      nodes.forEach((a, i) => {
        nodes.map((b, j) => [j, Math.hypot(a[0] - b[0], a[1] - b[1])]).filter(([j]) => j > i)
          .sort((x, y) => x[1] - y[1]).slice(0, 2)
          .forEach(([j, d]) => { if (d < 330) edges.push([i, j, d]); });
      });
      edges.forEach(([i, j]) => mk("line", { x1: nodes[i][0], y1: nodes[i][1], x2: nodes[j][0], y2: nodes[j][1] }));
      nodes.forEach(([x, y]) => mk("circle", { cx: x, cy: y, r: 2.6 }));
      // a handful of packets, each ending with a ping on the node it reached
      edges.filter((_, k) => k % 3 === 0).slice(0, 9).forEach(([i, j, d], k) => {
        const dur = (3.2 + rnd() * 2.4).toFixed(2) + "s", wait = (k * 0.9).toFixed(2) + "s";
        const p = mk("path", { class: "pkt", d: `M${nodes[i][0]} ${nodes[i][1]} L${nodes[j][0]} ${nodes[j][1]}` });
        p.style.cssText = `--d:${dur};--w:${wait};--len:${Math.round(d)}px`;
        const r = mk("circle", { class: "node-ping", cx: nodes[j][0], cy: nodes[j][1], r: 3 });
        r.style.cssText = `--d:${dur};--w:${wait}`;
      });
      hero.insertBefore(svg, hero.firstChild);
    }

    /* cards: a soft light that follows the pointer */
    if (!reduce) document.querySelectorAll("a.card").forEach((c) => {
      c.addEventListener("pointermove", (e) => {
        const r = c.getBoundingClientRect();
        c.style.setProperty("--mx", e.clientX - r.left + "px");
        c.style.setProperty("--my", e.clientY - r.top + "px");
      });
    });

    /* stats count up the first time they come into view; the real text is there
       from the start and is what screen readers get */
    const stats = document.querySelectorAll(".stat b");
    if (stats.length && !reduce && "IntersectionObserver" in window) {
      const io = new IntersectionObserver((entries) => entries.forEach((e) => {
        if (!e.isIntersecting) return;
        io.unobserve(e.target);
        const b = e.target, final = b.textContent, m = final.match(/^(\D*)([\d,]+)(.*)$/);
        if (!m) return;
        const target = parseInt(m[2].replace(/,/g, ""), 10);
        if (!target) return;
        b.setAttribute("aria-label", final);
        const t0 = performance.now(), dur = 1100;
        const step = (now) => {
          const k = Math.min(1, (now - t0) / dur), v = Math.round(target * (1 - Math.pow(1 - k, 3)));
          b.textContent = m[1] + v.toLocaleString("en-US") + m[3];
          if (k < 1) requestAnimationFrame(step); else b.textContent = final;
        };
        requestAnimationFrame(step);
      }), { threshold: 0.4 });
      stats.forEach((b) => io.observe(b));
    }
  };
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", run); else run();
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
