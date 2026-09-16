/* The living mesh behind every page.
   Nodes drift, links form by distance, and messages FLOOD: each node that hears
   a packet re-broadcasts once and only once, which is how MeshCore actually
   de-duplicates. Occasionally an amber packet takes a single direct path. */
(function () {
  const canvas = document.getElementById("mesh-bg");
  if (!canvas) return;
  const ctx = canvas.getContext("2d", { alpha: true });
  const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  const C = { node: "#1f6f4e", hub: "#3dffa8", link: "#16241c", direct: "#e6b955" };
  const LINK_DIST = 180;
  const HOP_MS = 400;
  const MAX_HOPS = 5;

  let w = 0, h = 0, dpr = 1;
  let nodes = [];
  let packets = [];   // travelling dots
  let dim = 0;        // 0 = hero brightness, 1 = dimmed behind text
  const mouse = { x: -999, y: -999 };

  function resize() {
    dpr = Math.min(window.devicePixelRatio || 1, 2);
    w = canvas.clientWidth = window.innerWidth;
    h = canvas.clientHeight = window.innerHeight;
    canvas.width = Math.floor(w * dpr);
    canvas.height = Math.floor(h * dpr);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    build();
  }

  function build() {
    const count = Math.max(38, Math.min(70, Math.round((w * h) / 26000)));
    nodes = [];
    for (let i = 0; i < count; i++) {
      nodes.push({
        x: Math.random() * w,
        y: Math.random() * h,
        vx: (Math.random() - 0.5) * 0.12,
        vy: (Math.random() - 0.5) * 0.12,
        hub: Math.random() < 0.18,       // "repeaters"
        flash: 0,
      });
    }
    linkAll();
  }

  function linkAll() {
    for (const n of nodes) n.near = [];
    for (let i = 0; i < nodes.length; i++) {
      for (let j = i + 1; j < nodes.length; j++) {
        const dx = nodes[i].x - nodes[j].x, dy = nodes[i].y - nodes[j].y;
        if (dx * dx + dy * dy < LINK_DIST * LINK_DIST) {
          nodes[i].near.push(j);
          nodes[j].near.push(i);
        }
      }
    }
  }

  /* A flood: the origin sends to each neighbour; every node relays once. */
  function flood(from) {
    const seen = new Set([from]);
    hop(from, 0, seen);
  }

  function hop(idx, depth, seen) {
    if (depth >= MAX_HOPS) return;
    const n = nodes[idx];
    if (!n || !n.near) return;
    for (const j of n.near) {
      if (seen.has(j)) continue;          // already relayed: MeshCore drops it
      seen.add(j);
      packets.push({ a: idx, b: j, t: 0, depth: depth, kind: "flood" });
      const delay = HOP_MS + Math.random() * 220;
      setTimeout(() => {
        if (nodes[j]) { nodes[j].flash = 1; hop(j, depth + 1, seen); }
      }, delay);
    }
  }

  function direct() {
    if (nodes.length < 6) return;
    let a = (Math.random() * nodes.length) | 0;
    let path = [a];
    for (let k = 0; k < 3; k++) {
      const n = nodes[path[path.length - 1]];
      if (!n.near || !n.near.length) break;
      const next = n.near[(Math.random() * n.near.length) | 0];
      if (path.includes(next)) break;
      path.push(next);
    }
    for (let i = 0; i < path.length - 1; i++) {
      setTimeout(() => packets.push({ a: path[i], b: path[i + 1], t: 0, depth: 0, kind: "direct" }),
        i * HOP_MS);
    }
  }

  let lastSend = 0;
  function tick(now) {
    if (document.hidden) { raf = requestAnimationFrame(tick); return; }

    // scroll parallax: dimmer once past the first screen
    const target = Math.min(1, window.scrollY / (window.innerHeight * 0.8));
    dim += (target - dim) * 0.06;

    ctx.clearRect(0, 0, w, h);
    const fade = 1 - dim * 0.6;

    for (const n of nodes) {
      n.x += n.vx; n.y += n.vy;
      if (n.x < 0 || n.x > w) n.vx *= -1;
      if (n.y < 0 || n.y > h) n.vy *= -1;
      if (n.flash > 0) n.flash -= 0.03;
    }

    ctx.lineWidth = 1;
    for (let i = 0; i < nodes.length; i++) {
      const a = nodes[i];
      if (!a.near) continue;
      for (const j of a.near) {
        if (j < i) continue;
        const b = nodes[j];
        const d = Math.hypot(a.x - b.x, a.y - b.y);
        const o = (1 - d / LINK_DIST) * 0.5 * fade;
        if (o <= 0.01) continue;
        ctx.strokeStyle = `rgba(31,111,78,${o})`;
        ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.stroke();
      }
    }

    for (const n of nodes) {
      const near = Math.hypot(n.x - mouse.x, n.y - mouse.y) < 130;
      const r = n.hub ? 2.6 : 1.7;
      const glow = Math.max(n.flash, near ? 0.5 : 0);
      if (n.hub || glow > 0) {
        ctx.fillStyle = `rgba(61,255,168,${(0.5 + glow * 0.5) * fade})`;
        if (n.hub) {
          ctx.beginPath(); ctx.arc(n.x, n.y, r + 4, 0, 7);
          ctx.fillStyle = `rgba(61,255,168,${0.07 * fade})`; ctx.fill();
          ctx.fillStyle = `rgba(61,255,168,${(0.6 + glow * 0.4) * fade})`;
        }
      } else {
        ctx.fillStyle = `rgba(31,111,78,${0.75 * fade})`;
      }
      ctx.beginPath(); ctx.arc(n.x, n.y, r, 0, 7); ctx.fill();
    }

    for (let i = packets.length - 1; i >= 0; i--) {
      const p = packets[i];
      p.t += 1 / (HOP_MS / 16.7);
      if (p.t >= 1) { packets.splice(i, 1); continue; }
      const a = nodes[p.a], b = nodes[p.b];
      if (!a || !b) { packets.splice(i, 1); continue; }
      const x = a.x + (b.x - a.x) * p.t, y = a.y + (b.y - a.y) * p.t;
      const strength = (1 - p.depth / MAX_HOPS) * fade;
      ctx.fillStyle = p.kind === "direct"
        ? `rgba(230,185,85,${0.9 * fade})`
        : `rgba(61,255,168,${0.35 + 0.6 * strength})`;
      ctx.beginPath(); ctx.arc(x, y, p.kind === "direct" ? 2.2 : 2.6, 0, 7); ctx.fill();
    }

    if (now - lastSend > 1000 + Math.random() * 2000) {
      lastSend = now;
      if (Math.random() < 0.22) direct();
      else flood((Math.random() * nodes.length) | 0);
    }

    raf = requestAnimationFrame(tick);
  }

  function nearest(x, y) {
    let best = 0, bd = Infinity;
    nodes.forEach((n, i) => {
      const d = (n.x - x) ** 2 + (n.y - y) ** 2;
      if (d < bd) { bd = d; best = i; }
    });
    return best;
  }

  let raf = 0;
  window.addEventListener("resize", resize);
  window.addEventListener("mousemove", (e) => { mouse.x = e.clientX; mouse.y = e.clientY; });
  window.addEventListener("mouseleave", () => { mouse.x = mouse.y = -999; });
  window.addEventListener("click", (e) => {
    if (e.target.closest("a,button,esp-web-install-button,input,select")) return;
    if (!nodes.length) return;
    flood(nearest(e.clientX, e.clientY));
  });

  resize();
  if (reduced) {
    // static frame: draw once, no animation
    ctx.clearRect(0, 0, w, h);
    for (let i = 0; i < nodes.length; i++) {
      const a = nodes[i];
      for (const j of a.near || []) {
        if (j < i) continue;
        const b = nodes[j];
        ctx.strokeStyle = "rgba(31,111,78,0.28)";
        ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.stroke();
      }
    }
    for (const n of nodes) {
      ctx.fillStyle = n.hub ? "rgba(61,255,168,0.75)" : "rgba(31,111,78,0.7)";
      ctx.beginPath(); ctx.arc(n.x, n.y, n.hub ? 2.6 : 1.7, 0, 7); ctx.fill();
    }
  } else {
    raf = requestAnimationFrame(tick);
  }
})();
