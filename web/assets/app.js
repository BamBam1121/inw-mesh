/* Shared page behaviour: nav, headings that decode in, reveal-on-scroll, and
   the live version read from the installer site's ota.json. */
(function () {
  const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  const SITE = "https://bambam1121.github.io/inw-mesh/firmware/";

  /* nav gets a backdrop once you scroll */
  const nav = document.querySelector("header.nav");
  if (nav) {
    const onScroll = () => nav.classList.toggle("stuck", window.scrollY > 12);
    window.addEventListener("scroll", onScroll, { passive: true });
    onScroll();
    const here = location.pathname.split("/").pop() || "index.html";
    nav.querySelectorAll("a.link").forEach((a) => {
      if (a.getAttribute("href") === here) a.setAttribute("aria-current", "page");
    });
  }

  /* headings scramble briefly when they scroll into view, once each */
  const GLYPHS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#@%&*/\\<>";
  function decode(el) {
    if (reduced || el.dataset.done) return;
    el.dataset.done = "1";
    const real = el.textContent;
    let frame = 0;
    const steps = 14;
    const id = setInterval(() => {
      frame++;
      el.textContent = real.split("").map((ch, i) => {
        if (ch === " ") return " ";
        if (i < (real.length * frame) / steps) return ch;
        return GLYPHS[(Math.random() * GLYPHS.length) | 0];
      }).join("");
      if (frame >= steps) { clearInterval(id); el.textContent = real; }
    }, 28);
  }

  const io = new IntersectionObserver((entries) => {
    for (const e of entries) {
      if (!e.isIntersecting) continue;
      e.target.classList.add("in");
      if (e.target.matches("h2")) decode(e.target);
      io.unobserve(e.target);
    }
  }, { threshold: 0.15 });

  document.querySelectorAll(".reveal, h2").forEach((el) => io.observe(el));

  /* Failsafe: .reveal starts at opacity 0, so anything the observer never reports
     would stay invisible forever - no IntersectionObserver, a JS error, or a browser
     that lays the page out offscreen and reports nothing until the first scroll.
     Reading the page must never depend on the animation firing. */
  function revealAll() {
    document.querySelectorAll(".reveal:not(.in)").forEach((el) => el.classList.add("in"));
  }
  if (!("IntersectionObserver" in window)) revealAll();
  else window.addEventListener("load", () => setTimeout(revealAll, 1200));

  /* live version badge: ota.json -> version.txt -> leave the placeholder alone */
  async function version() {
    const slots = document.querySelectorAll("[data-version]");
    if (!slots.length) return;
    const set = (txt) => slots.forEach((s) => { s.textContent = txt; });
    try {
      const r = await fetch(SITE + "ota.json", { cache: "no-store" });
      if (r.ok) {
        const j = await r.json();
        if (j.version) { set("v" + j.version); return; }
      }
    } catch (e) { /* offline or blocked: fall through */ }
    try {
      const r = await fetch(SITE + "version.txt", { cache: "no-store" });
      if (r.ok) { const t = (await r.text()).trim().split(/\s+/)[0]; if (t) set(t); }
    } catch (e) { /* leave placeholder */ }
  }
  version();

  /* Releases page: real data from GitHub, cached so we do not hammer the API. */
  window.loadReleases = async function (elId) {
    const box = document.getElementById(elId);
    if (!box) return;
    const KEY = "inw-releases";
    const cached = sessionStorage.getItem(KEY);
    if (cached) {
      const { at, data } = JSON.parse(cached);
      if (Date.now() - at < 600000) return render(box, data);
    }
    try {
      const r = await fetch("https://api.github.com/repos/BamBam1121/inw-mesh/releases");
      if (!r.ok) throw new Error(r.status);
      const data = await r.json();
      sessionStorage.setItem(KEY, JSON.stringify({ at: Date.now(), data }));
      render(box, data);
    } catch (e) {
      box.innerHTML = '<div class="panel">Could not reach GitHub. ' +
        '<a href="https://github.com/BamBam1121/inw-mesh/releases">See the releases page</a>.</div>';
    }
  };

  function esc(s) {
    return String(s).replace(/[&<>"']/g, (c) =>
      ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  }

  function render(box, list) {
    if (!list || !list.length) {
      box.innerHTML = '<div class="panel">No releases yet.</div>';
      return;
    }
    box.innerHTML = list.map((rel, i) => {
      const date = new Date(rel.published_at || rel.created_at).toISOString().slice(0, 10);
      const body = esc(rel.body || "").replace(/\r?\n/g, "<br>");
      return '<div class="panel reveal in" style="margin-bottom:14px">' +
        '<div class="tag">' + date + (i === 0 ? " &middot; latest" : "") + "</div>" +
        "<h3>" + esc(rel.tag_name || rel.name) + "</h3>" +
        '<div style="color:var(--muted);font-size:13px">' + body + "</div></div>";
    }).join("");
  }
})();
