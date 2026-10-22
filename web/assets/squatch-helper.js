/* The sasquatch at the right edge of every page. On the first page of a visit he knocks
   on the inside of the screen, breaks through its edge and leans out of the hole; on
   later pages he pops straight out. He waves, blinks, watches the pointer, and now and
   then types on a pager, sneezes, ducks back in or looks around; he dozes off when
   nobody moves, gets dizzy when the page is flung about, and giggles when his face is
   touched. A click opens a small chat with the same helper as /help (the help desk on
   the site's own server); he scratches his head while it thinks. The conversation is
   kept in this tab under the help page's own key, so it carries on from page to page
   and onto /help. The X sends him back in and seals the hole until the page is
   reloaded. On /help itself a click takes you to the chat on the page instead. */
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
  // A comic-book burst.
  function star(cx, cy, points, outer, inner) {
    let d = "";
    for (let i = 0; i < points * 2; i++) {
      const a = (Math.PI * i) / points - Math.PI / 2;
      const r = i % 2 ? inner : outer;
      d += (i ? "L" : "M") + (cx + Math.cos(a) * r).toFixed(1) + " " + (cy + Math.sin(a) * r).toFixed(1);
    }
    return d + "Z";
  }
  // A dizzy eye: a white with a spiral turning on it.
  function spiral(cx, cy) {
    let d = "";
    for (let i = 0; i <= 22; i++) {
      const a = i * 0.6, r = 0.18 * i;
      d += (i ? "L" : "M") + (cx + Math.cos(a) * r).toFixed(2) + " " + (cy + Math.sin(a) * r).toFixed(2);
    }
    return '<ellipse cx="' + cx + '" cy="' + cy + '" rx="4.8" ry="4.6" fill="#fff"/><path d="' + d + '">' +
      '<animateTransform attributeName="transform" type="rotate" from="0 ' + cx + " " + cy + '" to="360 ' + cx + " " + cy +
      '" dur=".8s" repeatCount="indefinite"/></path>';
  }
  // A tuft of fur: a thin triangle from a base point, pointing along (dx, dy).
  function tuft(bx, by, dx, dy, len, half) {
    const n = Math.hypot(dx, dy) || 1;
    dx /= n; dy /= n;
    return "M" + (bx - dy * half).toFixed(1) + " " + (by + dx * half).toFixed(1) + "L" + (bx + dy * half).toFixed(1) + " " +
      (by - dx * half).toFixed(1) + "L" + (bx + dx * len).toFixed(1) + " " + (by + dy * len).toFixed(1) + "Z";
  }
  const dot = (x, y, r, paint) => '<circle cx="' + x + '" cy="' + y + '" r="' + r + '" ' + (paint === undefined ? 'fill="var(--sqh-skin)"' : paint) + "/>";
  const stroke = (d, w, color, extra) => '<path d="' + d + '" fill="none" stroke="' + color + '" stroke-width="' + w +
    '" stroke-linecap="round" stroke-linejoin="round"' + (extra || "") + "/>";

  // ---- the sasquatch ------------------------------------------------------------------
  // A broad, dome-shaped head with a little crest, shaggy cheeks, a heavy brow over
  // small kind eyes, a broad nose and a lighter muzzle; hair runs straight into big
  // shoulders. Fur in three tones: --sqh-fur, a darker one for shadow, a lighter one
  // for the strands that catch the light.
  const HX = 118, HY = 96, HRX = 34, HRY = 33;
  // tufts round the head: [angle in degrees, length, half width, how much it hangs down]
  const HEAD_TUFTS = [[-90, 12, 4.5, 0], [-104, 8, 4.5, 0], [-76, 8, 4.5, 0], [-122, 8, 5, 0.1], [-58, 8, 5, 0.1], [-142, 8, 5, 0.3],
    [-38, 8, 5, 0.3], [-162, 9, 5, 0.6], [-18, 9, 5, 0.6], [178, 10, 5.5, 0.9], [2, 10, 5.5, 0.9], [160, 10, 5.5, 1.2],
    [20, 10, 5.5, 1.2], [142, 9, 5, 1.5], [38, 9, 5, 1.5]];
  function headFur() {
    let d = "";
    HEAD_TUFTS.forEach((t) => {
      const a = (t[0] * Math.PI) / 180;
      d += tuft(HX + Math.cos(a) * HRX * 0.9, HY + Math.sin(a) * HRY * 0.9, Math.cos(a), Math.sin(a) + t[3], t[1], t[2]);
    });
    return '<ellipse cx="' + HX + '" cy="' + HY + '" rx="' + HRX + '" ry="' + HRY + '" fill="var(--sqh-fur)"/>' +
      '<path d="' + d + '" fill="var(--sqh-fur)" stroke="var(--sqh-fur)" stroke-width="1" stroke-linejoin="round"/>' +
      // strands: light ones on top, dark ones down the cheeks
      stroke("M103 70q-3 8-2 16M133 70q3 8 2 16M118 64v11M95 78q-4 6-4 13M141 78q4 6 4 13", 1.6, "var(--sqh-fur-light)") +
      stroke("M88 104q-1 9 3 17M148 104q1 9-3 17M94 116q1 6 5 10M142 116q-1 6-5 10", 1.5, "var(--sqh-fur-dark)", ' opacity=".7"');
  }
  const FACE = '<path d="M95 87Q118 80 141 87Q147 103 137 116Q129 125 118 125Q107 125 99 116Q89 103 95 87Z" fill="var(--sqh-skin)"/>' +
    '<ellipse cx="118" cy="114" rx="13.5" ry="8.5" fill="var(--sqh-muzzle)"/>' +
    // the brow's shadow over the eyes
    '<path d="M96 90Q118 84 140 90Q139 97 129 95Q118 93 107 95Q97 97 96 90Z" fill="var(--sqh-skin-dark)" opacity=".55"/>';
  const BROW = "M91 91Q103 79 118 85Q133 79 145 91Q138 94 129 91Q118 88 107 91Q98 94 91 91Z";
  const NOSE = '<path d="M109.5 103Q118 97.5 126.5 103Q129.5 109 123.5 110.5Q118 108.5 112.5 110.5Q106.5 109 109.5 103Z" fill="#4a2a1a"/>' +
    '<ellipse cx="114.5" cy="107.4" rx="1.6" ry="1.05" fill="#24130b"/><ellipse cx="121.5" cy="107.4" rx="1.6" ry="1.05" fill="#24130b"/>' +
    '<ellipse cx="116" cy="101.8" rx="2.6" ry=".9" fill="#fff" opacity=".28"/>';
  const SMILE = '<path d="M108 115Q118 122.5 128 115Q118 118 108 115Z" fill="#7d3328"/>';

  // Just his head, for the chat's header.
  function squatch() {
    return '<svg viewBox="76 50 84 84" aria-hidden="true" focusable="false">' + headFur() + FACE +
      '<path d="' + BROW + '" fill="var(--sqh-fur-dark)"/>' +
      '<ellipse cx="107" cy="96.5" rx="3.2" ry="3.4" fill="#24160f"/><ellipse cx="129" cy="96.5" rx="3.2" ry="3.4" fill="#24160f"/>' +
      NOSE + SMILE + "</svg>";
  }

  // The corner scene in a 170 x 210 box whose right side is the screen's right edge.
  // Back to front: a comic burst, the cracks, "knock" lettering, then the hole - his
  // night sky, the glowing eyes, and his shoulders and the arm up to the rim standing
  // in it, behind the broken edge - then what leans out over the edge (the waving arm,
  // his head, the fingers over the rim), and last the pieces of screen that fly off.
  // His body and what leans out move together.
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
  const PUFFS = [[86, 76, -11, -9], [150, 74, 10, -10], [118, 56, 0, -13], [88, 124, -13, 6], [148, 124, 12, 7]];
  const SKIN = "var(--sqh-skin)";

  function scene() {
    const crack = (d, k) => '<path class="sqh-crack ' + k + '" pathLength="1" d="' + d + '"/>';
    const moving = (inner, extra) => '<g class="sqh-out"><g class="sqh-lean"><g class="sqh-breath' + (extra || "") + '">' + inner + "</g></g></g>";

    const sky =
      '<g clip-path="url(#sqh-holeclip)">' +
        '<g class="sqh-stars" fill="#fff">' +
          dot(150, 45, 1.1, "") + dot(138, 57, 0.8, "") + dot(86, 73, 0.9, "") + dot(79, 104, 1.2, "") + dot(88, 150, 0.8, "") +
          dot(82, 127, 0.7, "") + dot(161, 60, 1, "") + dot(100, 60, 0.7, "") + dot(93, 88, 0.6, "") + dot(157, 34, 0.7, "") +
        "</g>" +
        '<path d="M150 50a9 9 0 1 0 7 15 7 7 0 1 1-7-15z" fill="#f4f1d0" opacity=".9"/>' +
        '<path d="M70 210L82 172L94 210ZM86 210L99 164L112 210ZM104 210L114 180L124 210ZM118 210L131 170L144 210ZM138 210L150 176L162 210Z" fill="#0a2a1e"/>' +
      "</g>";
    const glow = '<g class="sqh-glow"><circle cx="131" cy="112" r="7" fill="#e8ff9a" opacity=".25"/><circle cx="149" cy="112" r="7" fill="#e8ff9a" opacity=".25"/>' +
      '<ellipse cx="131" cy="112" rx="3.2" ry="2.4" fill="#f4ffc4"/><ellipse cx="149" cy="112" rx="3.2" ry="2.4" fill="#f4ffc4"/></g>';

    // In the hole, behind the rim: big shoulders, and his right arm reaching up to the
    // top of the rim, well clear of his head.
    const shoulderTufts = tuft(84, 146, -1, -0.4, 8, 4) + tuft(92, 134, -0.6, -1, 8, 4) + tuft(104, 128, -0.2, -1, 7, 4) +
      tuft(140, 128, 0.3, -1, 7, 4) + tuft(156, 136, 0.7, -1, 7, 4);
    const body =
      stroke("M156 140Q168 96 160 36", 14, "var(--sqh-fur)") +
      '<path d="' + tuft(166, 96, 1, 0.5, 7, 3) + tuft(165, 70, 1, 0.4, 6, 3) + '" fill="var(--sqh-fur)"/>' +
      '<path d="M76 214L78 168Q80 140 100 130Q118 124 136 130Q160 136 174 152L174 214Z" fill="var(--sqh-fur)"/>' +
      '<path d="' + shoulderTufts + '" fill="var(--sqh-fur)"/>' +
      '<path d="M104 152Q123 140 144 152Q144 178 124 192Q104 178 104 152Z" fill="var(--sqh-fur-light)" opacity=".55"/>' +
      stroke("M114 154q-2 10 1 20M134 154q2 10-1 20M124 160v14", 1.4, "var(--sqh-fur-dark)", ' opacity=".5"');

    // A big open hand, fingers up: palm centre (x, y).
    const openHand = (x, y) =>
      stroke("M" + (x - 6) + " " + (y - 3) + "l-3-10M" + (x - 2) + " " + (y - 6) + "l-1-11M" + (x + 2.5) + " " + (y - 6) +
        "l1.5-11M" + (x + 6.5) + " " + (y - 3) + "l3-9.5M" + (x - 7) + " " + (y + 4) + "l-6.5-4", 4.6, SKIN) +
      '<ellipse cx="' + x + '" cy="' + y + '" rx="8" ry="8.8" fill="' + SKIN + '"/>' +
      stroke("M" + (x - 3.5) + " " + (y + 1) + "q3.5 2.5 7 0", 1.1, "var(--sqh-skin-dark)");
    // The waving arm: the upper arm stays put, the forearm waves from the elbow.
    const waveArm = '<g class="sqh-armset">' +
      stroke("M100 140Q86 134 76 124", 15, "var(--sqh-fur)") +
      '<g class="sqh-arm">' + stroke("M76 124Q67 110 62 96", 13, "var(--sqh-fur)") +
        '<path d="' + tuft(70, 115, -1, 0.7, 7, 2.6) + tuft(65.5, 105, -1, 0.5, 6, 2.4) + tuft(80, 130, -0.4, 1, 6, 2.6) + '" fill="var(--sqh-fur)"/>' +
        openHand(61, 87) + "</g></g>";
    const pagerArm = '<g class="sqh-pager">' +
      stroke("M100 140Q78 156 83 146", 14, "var(--sqh-fur)") +
      '<g class="sqh-radio" fill="none" stroke="#3dffa8" stroke-width="1.3" stroke-linecap="round">' +
        '<path d="M81 116a6 6 0 0 1 8 0"/><path d="M78 112.5a10.5 10.5 0 0 1 14 0"/><path d="M75 109a15 15 0 0 1 20 0"/></g>' +
      '<path d="M88 130L86 121" stroke="#3a4a44" stroke-width="2" stroke-linecap="round"/>' + dot(85.8, 120.4, 1.5, 'fill="#3dffa8"') +
      '<rect x="84" y="129" width="27" height="17" rx="3.5" fill="#22322c" stroke="#0b1310"/>' +
      '<rect x="86.5" y="131.5" width="15.5" height="10" rx="1.3" fill="#062a1b"/>' +
      '<g class="sqh-scr" fill="#3dffa8"><rect x="88" y="133.3" width="10" height="1.5" rx=".7"/>' +
        '<rect x="88" y="136" width="7" height="1.5" rx=".7"/><rect x="88" y="138.7" width="11.5" height="1.5" rx=".7"/></g>' +
      dot(106.5, 137.5, 2.5, 'fill="#3a4a44" stroke="#0b1310" stroke-width=".6"') +
      '<ellipse cx="84" cy="144" rx="6.5" ry="5.5" fill="' + SKIN + '"/>' +
      '<circle class="sqh-thumb" cx="95" cy="131" r="2.8" fill="' + SKIN + '" stroke="var(--sqh-skin-dark)" stroke-width=".6"/>' +
      "</g>";
    // a little wooden sign on a stick, held up beside him
    const signArm = '<g class="sqh-sign">' +
      stroke("M100 140Q84 140 66 128", 15, "var(--sqh-fur)") +
      '<g class="sqh-board"><path d="M59 125L42 101" stroke="#7a5530" stroke-width="3" stroke-linecap="round"/>' +
        '<rect x="4" y="71" width="66" height="31" rx="3.5" fill="#d6a866" stroke="#7a5530" stroke-width="1.6"/>' +
        '<path d="M8 78h58M8 94h58" stroke="#b8894a" stroke-width=".7" opacity=".6"/>' +
        '<text class="sqh-sign-text" x="37" y="84.5" font-size="8.6" text-anchor="middle"><tspan x="37">NOT A GUY</tspan><tspan x="37" dy="10.5">IN A SUIT</tspan></text>' +
      "</g>" + '<ellipse cx="60" cy="124" rx="7.5" ry="7" fill="' + SKIN + '"/>' + stroke("M55 119l-3-2M54.5 124l-3.5-.5M55 129l-3 1.5", 4, SKIN) + "</g>";
    const scratchArm = '<g class="sqh-scratch">' +
      stroke("M100 140Q60 134 64 104Q68 84 86 70", 14, "var(--sqh-fur)") +
      '<g class="sqh-scratch-hand"><ellipse cx="90" cy="66" rx="7" ry="6.5" fill="' + SKIN + '"/>' +
        stroke("M86 61l-1-6M90.5 60l.5-6.5M95 61.5l2-5.5", 4.2, SKIN) + "</g></g>";
    // stars circling his head: each rides the same ellipse, a third of a lap apart
    const orbit = '<g class="sqh-orbit" fill="#ffe27a" stroke="#7a5a10" stroke-width=".6" stroke-linejoin="round">' +
      ["0s", "-0.4s", "-0.8s"].map((b) => '<path d="M0-5l1.4 3.6 3.6 1.4-3.6 1.4L0 5l-1.4-3.6L-5 0l3.6-1.4z">' +
        '<animateMotion dur="1.2s" repeatCount="indefinite" begin="' + b + '" path="M152 56a34 10 0 1 1-68 0a34 10 0 1 1 68 0"/></path>').join("") +
      "</g>";
    const head = '<g class="sqh-head">' + headFur() +
      // Halloween week: a witch's hat
      '<g class="sqh-hat" transform="translate(5.5 -4)"><path d="M96 66Q107 48 112 30Q117 22 123 27Q118 31 119 38Q122 52 129 66Z" fill="#3a1f5c" stroke="#1d0f2e" stroke-width=".8"/>' +
        '<path d="M98 60Q113 64 127 60L128.5 66Q113 69 96.5 66Z" fill="var(--sqh-scarf)"/>' +
        '<ellipse cx="112.5" cy="67" rx="27" ry="5" fill="#2a1640" stroke="#1d0f2e" stroke-width=".8"/></g>' +
      FACE +
      '<path class="sqh-brows" d="' + BROW + '" fill="var(--sqh-fur-dark)"/>' +
      '<path class="sqh-brows-sad" d="M91 95Q102 90 113 83Q118 85 123 83Q134 90 145 95Q138 98 129 93Q118 89 107 93Q98 98 91 95Z" fill="var(--sqh-fur-dark)"/>' +
      '<g class="sqh-e-open"><g class="sqh-blink">' +
        '<ellipse cx="107" cy="96.5" rx="4.8" ry="4.4" fill="#fff"/><ellipse cx="129" cy="96.5" rx="4.8" ry="4.4" fill="#fff"/>' +
        '<g class="sqh-pupils"><ellipse cx="107.4" cy="97.2" rx="2.9" ry="3.2" fill="#24160f"/><ellipse cx="129.4" cy="97.2" rx="2.9" ry="3.2" fill="#24160f"/>' +
          '<circle cx="108.5" cy="96" r="1" fill="#fff"/><circle cx="130.5" cy="96" r="1" fill="#fff"/></g>' +
      "</g></g>" +
      stroke("M102.5 97.5q4.5 3 9 0M124.5 97.5q4.5 3 9 0", 2, "#24160f", ' class="sqh-e-shut"') +
      stroke("M102.5 99q4.5-5 9 0M124.5 99q4.5-5 9 0", 2.2, "#24160f", ' class="sqh-e-happy"') +
      '<g class="sqh-e-dizzy" fill="none" stroke="#24160f" stroke-width="1.3" stroke-linecap="round">' + spiral(107, 96.5) + spiral(129, 96.5) + "</g>" +
      '<g class="sqh-cheeks" fill="var(--sqh-cheek)"><ellipse cx="99" cy="108" rx="3.8" ry="2.4"/><ellipse cx="137" cy="108" rx="3.8" ry="2.4"/></g>' +
      NOSE +
      '<g class="sqh-m-smile">' + SMILE + '<ellipse cx="118" cy="118" rx="2.8" ry="1.1" fill="#e0736a"/></g>' +
      '<ellipse class="sqh-m-o" cx="118" cy="117" rx="3" ry="3.4" fill="#6e2a22"/>' +
      '<g class="sqh-m-grin"><path d="M106.5 114Q118 126 129.5 114Z" fill="#7d3328"/><path d="M109 114.5h18l-1 2.5h-16z" fill="#fff"/></g>' +
      stroke("M109.5 117q2-2 4.2 0t4.2 0 4.2 0 4.2 0", 1.8, "#5a3422", ' class="sqh-m-wavy"') +
      stroke("M111 119.5q7-5 14 0", 2, "#5a3422", ' class="sqh-m-frown"') +
      '<circle class="sqh-snot" cx="123.5" cy="109" r="3" fill="#bfe8ff" fill-opacity=".75" stroke="#fff" stroke-width=".6"/>' +
      '<ellipse class="sqh-mist" cx="104" cy="120" rx="9" ry="5.5" fill="#fff" fill-opacity=".7"/>' +
      '<path class="sqh-sweat" d="M151 74c2 3 3.2 5 3.2 6.6a3.2 3.2 0 0 1-6.4 0c0-1.6 1.2-3.6 3.2-6.6z" fill="#8fd3ff" stroke="#fff" stroke-width=".6"/>' +
      '<g class="sqh-puffs">' + PUFFS.map((p) => '<circle class="sqh-puff" cx="' + p[0] + '" cy="' + p[1] + '" r="3.2" fill="var(--sqh-fur)" ' +
        'style="--dx:' + p[2] + "px;--dy:" + p[3] + 'px"/>').join("") + "</g>" +
      '<text class="sqh-bang" x="154" y="64" font-size="24" text-anchor="middle">!</text>' +
      '<text class="sqh-q" x="154" y="62" font-size="20" text-anchor="middle">?</text>' +
      '<g class="sqh-zzz"><text class="sqh-zz" x="141" y="66" font-size="9">z</text><text class="sqh-zz" x="148" y="58" font-size="11">z</text>' +
        '<text class="sqh-zz" x="156" y="49" font-size="13">Z</text></g>' +
      '<g class="sqh-notes"><text class="sqh-note" x="78" y="70" font-size="13">♪</text><text class="sqh-note" x="154" y="62" font-size="15">♫</text>' +
        '<text class="sqh-note" x="98" y="50" font-size="11">♪</text></g>' +
      orbit + "</g>";
    // his fingers hooked over the top of the rim, right of his head
    const grip = stroke("M151 26.5l-1 8.5M155.5 26l-.6 9M160 26l.2 8.6M164.5 27l.8 7.8", 4.4, SKIN) +
      stroke("M150 27.5h15", 2, "var(--sqh-skin-dark)", ' opacity=".5"');

    return '<svg viewBox="0 0 170 210" aria-hidden="true" focusable="false">' +
      "<defs>" +
        '<linearGradient id="sqh-sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#0c1c3d"/>' +
          '<stop offset="0.6" stop-color="#1b3a6b"/><stop offset="1" stop-color="#2d5a7a"/></linearGradient>' +
        '<clipPath id="sqh-clip"><rect x="-300" y="-300" width="470" height="510"/></clipPath>' +
        '<clipPath id="sqh-holeclip"><path d="' + HOLE + '"/></clipPath>' +
      "</defs>" +
      '<path class="sqh-burst" d="' + star(150, 115, 14, 84, 56) + '" fill="#ffe27a"/>' +
      '<path class="sqh-burst" d="' + star(150, 115, 12, 56, 36) + '" fill="#fff8d8"/>' +
      '<g class="sqh-cracks" fill="none" stroke="var(--sqh-rim)" stroke-opacity=".5" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round">' +
        CRACKS_A.map((d) => crack(d, "a")).join("") + CRACKS_B.map((d) => crack(d, "b")).join("") +
      "</g>" +
      '<g transform="rotate(-14 32 60)"><text class="sqh-word w1" x="32" y="60" font-size="12" text-anchor="middle">knock</text></g>' +
      '<g transform="rotate(10 36 166)"><text class="sqh-word w2" x="36" y="166" font-size="16" text-anchor="middle">KNOCK!</text></g>' +
      '<g class="sqh-hole">' +
        '<path class="sqh-hole-fill" d="' + HOLE + '" fill="url(#sqh-sky)" stroke="#000" stroke-opacity=".75" stroke-width="6" stroke-linejoin="round"/>' +
        sky + glow +
        '<g clip-path="url(#sqh-holeclip)">' + moving(body) +
          '<path d="' + HOLE + '" fill="none" stroke="#000" stroke-opacity=".45" stroke-width="9"/></g>' +
        '<path class="sqh-rim" d="' + HOLE + '" fill="none" stroke="var(--sqh-rim)" stroke-opacity=".6" stroke-width="1.6" stroke-linejoin="round"/>' +
      "</g>" +
      // the fist that punches through first
      '<g clip-path="url(#sqh-clip)"><g class="sqh-fist">' +
        stroke("M176 118L112 114", 17, "var(--sqh-fur)") +
        dot(103, 113, 11.5) + dot(93.5, 105.5, 4.3) + dot(92.5, 112.5, 4.4) + dot(93.5, 119.5, 4.3) + dot(99, 124.5, 3.6) +
        stroke("M97 104q2 9 0 18", 1.3, "var(--sqh-skin-dark)") +
      "</g></g>" +
      '<g clip-path="url(#sqh-clip)">' + moving(waveArm + pagerArm + signArm + head + scratchArm + grip, " sqh-drop") + "</g>" +
      '<g class="sqh-shards" fill="#1b2b24" stroke="var(--sqh-rim)" stroke-opacity=".55" stroke-width=".8" stroke-linejoin="round">' +
        SHARDS.map((s) => '<path class="sqh-shard" d="' + s[0] + '" style="--dx:' + s[1] + "px;--dy:" + s[2] + "px;--r:" + s[3] + 'deg"/>').join("") +
      "</g></svg>";
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
        '<span class="sqh-avatar">' + squatch() + "</span>" +
        '<span class="sqh-who"><b>Squatch helper</b><span>Pager and T-Deck help</span></span>' +
        '<button type="button" class="sqh-tool sqh-reset">Start over</button>' +
        '<button type="button" class="sqh-tool sqh-min" aria-label="Close the chat">' + ICON.down + "</button>" +
      "</div>" +
      '<div class="sqh-log" aria-live="polite">' +
        '<div class="sqh-msg assistant intro"><p>Hi! I\'m the Squatch helper. What\'s going on with your pager or T-Deck?</p>' +
          '<div class="sqh-chips">' +
            '<button type="button" data-ask="My computer doesn\'t see the pager when I try to install.">Installer can\'t find it</button>' +
            '<button type="button" data-ask="How do I update my firmware?">Updating</button>' +
            '<button type="button" data-ask="My pager won\'t turn on or start properly.">Won\'t start</button>' +
            '<button type="button" data-ask="I have an idea for a feature.">I have an idea</button>' +
          "</div></div>" +
      "</div>" +
      '<form class="sqh-form">' +
        '<label class="sqh-sr" for="sqh-input">Your message</label>' +
        '<textarea id="sqh-input" rows="1" maxlength="2000" placeholder="Type your question…" autocomplete="off"></textarea>' +
        '<button type="submit" class="sqh-send" aria-label="Send">' + ICON.send + "</button>" +
      "</form>" +
      '<p class="sqh-fine">Answers come from Claude, an AI, and can be wrong. Never share backups, keys or channel secrets. ' +
        '<a href="/help">More help</a> &middot; <a href="/privacy#helper">Privacy</a></p>' +
    "</div>" +
    '<div class="sqh-hello" hidden>Need a hand?</div>' +
    '<button type="button" class="sqh-close" aria-label="Send the sasquatch away until the page reloads">' + ICON.x + "</button>" +
    '<button type="button" class="sqh-squatch" aria-label="Chat with the Squatch helper" aria-expanded="false" aria-controls="sqh-panel">' +
      scene() + "</button>";

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
    react("thinking");
    try {
      const r = await fetch("/api/help/chat", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ messages }),
      });
      const j = await r.json().catch(() => ({}));
      wait.remove();
      if (!r.ok) {
        react("oops");
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
        react("answered");
        if (j.handoff) {
          pendingHandoff = j.handoff;
          bubble("assistant", "", "note", "<p>I've passed this to the developer. Want a reply? " +
            '<a href="/help#handoff">Leave your email on the help page</a>; this chat goes with you.</p>');
        }
      }
    } catch (e) {
      react("oops");
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

  // ---- how he moves: moods, gags and what he does when you do things --------------------
  const BROKE = "sm-helper-broke";    // the full break-in plays once per tab
  const still = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  const close = root.querySelector(".sqh-close");
  const pupils = root.querySelector(".sqh-pupils");
  const headEl = root.querySelector(".sqh-head");
  const scene_ = button.querySelector("svg");
  // One mood at a time; each is a class on .sqh that the stylesheet turns into a face
  // and a movement.
  const MOODS = ["surprised", "happy", "sleep", "startle", "dizzy", "pager", "sneeze", "thinking", "tickle", "look", "dance",
    "sign", "worried", "yawn"];
  const OWN_EYES = ["pager", "look", "thinking", "sleep", "dizzy", "sneeze", "worried"];   // these steer his eyes themselves
  let leaving = false, hovering = false, settled = false, current = "", moodTimer = 0, lastGag = "", gags = 0;
  let lastActive = Date.now();

  const later = (ms, fn) => setTimeout(fn, ms);
  const pick = (list) => list[Math.floor(Math.random() * list.length)];

  function look(x, y) {
    if (pupils) pupils.style.transform = "translate(" + (x * 2.2).toFixed(2) + "px," + (y * 2.2).toFixed(2) + "px)";
  }
  const lookHome = () => (panel.hidden ? look(0, 0) : look(-1, 0.2));

  function mood(name, ms) {
    clearTimeout(moodTimer);
    MOODS.forEach((m) => root.classList.remove(m));
    current = name || "";
    // the spinning eyes and circling stars only run while he's dizzy
    if (scene_.pauseAnimations) { if (name === "dizzy") scene_.unpauseAnimations(); else scene_.pauseAnimations(); }
    if (!name) { lookHome(); return; }
    void root.offsetWidth;             // the same mood twice in a row starts over
    root.classList.add(name);
    if (ms) moodTimer = later(ms, () => { if (current === name) mood(""); });
  }

  // What he says, in the bubble beside his head (a click on it opens the chat).
  let sayTimer = 0;
  function say(text, ms) {
    if (leaving || root.hidden || !panel.hidden) return;
    clearTimeout(sayTimer);
    hello.textContent = text;
    hello.classList.remove("gone");
    hello.hidden = false;
    hello.style.animation = "none";
    void hello.offsetWidth;
    hello.style.animation = "";
    sayTimer = later(ms || 2000, hideHello);
  }
  function hideHello() {
    if (hello.hidden) return;
    hello.classList.add("gone");
    later(260, () => { if (hello.classList.contains("gone")) hello.hidden = true; });
  }

  let excitedTimer = 0;
  function excite() {
    root.classList.add("excited");
    clearTimeout(excitedTimer);
    excitedTimer = later(1000, () => root.classList.remove("excited"));
  }
  function setOpen(open) {
    panel.hidden = !open;
    root.classList.toggle("open", open);
    button.setAttribute("aria-expanded", String(open));
    if (open) {
      hideHello();
      if (current === "sleep") mood("");
      load();
      log.scrollTop = log.scrollHeight;
      if (window.matchMedia("(hover: hover)").matches) input.focus();
    }
    if (!OWN_EYES.includes(current)) lookHome();
  }
  function onSquatch() {
    if (leaving) return;
    excite();
    hideHello();
    if (current !== "thinking") mood("happy", 900);
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

  // Heading for his X? He'd rather you didn't.
  close.addEventListener("pointerenter", () => {
    if (leaving || still || !settled || (current && current !== "happy" && current !== "tickle")) return;
    mood("worried", 0);
    look(-0.3, -1);
    say(pick(["Wait, don't go!", "Was it something I said?", "Nooo…", "I'll be quiet, promise."]), 1800);
  });
  close.addEventListener("pointerleave", () => { if (current === "worried" && !leaving) { mood("happy", 900); hideHello(); } });

  // The X: a happy goodbye, back into the hole, the hole seals, gone until a reload.
  close.addEventListener("click", () => {
    if (leaving) return;
    setOpen(false);
    mood("happy", 0);
    say(pick(["Bye!", "Later!", "Back to the woods!"]), 900);
    leaving = true;
    root.classList.remove("enter", "enter-quick", "duck");
    root.classList.add("leave");
    later(still ? 0 : 1600, () => { root.hidden = true; mood(""); });
  });

  // While the chat thinks he scratches his head; a reply makes him happy.
  function react(what) {
    if (still || leaving) return;
    if (what === "thinking") { mood("thinking", 0); look(0.5, -1); }
    else if (what === "answered") mood("happy", 1400);
    else mood("surprised", 1000);
  }

  // His eyes follow the pointer, unless a gag has them busy.
  let lookFrame = 0;
  if (!still) {
    document.addEventListener("pointermove", (e) => {
      if (lookFrame || !panel.hidden || e.pointerType === "touch" || OWN_EYES.includes(current)) return;
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

  // Touch his face and he giggles.
  let tickledAt = 0;
  headEl.addEventListener("pointerenter", () => {
    if (still || leaving || !settled || current === "sleep") return;
    const t = Date.now();
    if (t - tickledAt < 2500 || (current && current !== "happy")) return;
    tickledAt = t;
    mood("tickle", 1300);
    say(pick(["hehe", "That tickles!", "Hey!", "Boop!", "Hehehe, stop!"]), 1300);
  });

  // Nobody's moved for a while: he dozes off. Anything at all wakes him with a start.
  const YAWN_AFTER = 25000, SLEEP_AFTER = 40000;
  let yawned = false;
  function active() {
    lastActive = Date.now();
    yawned = false;
    if (current === "sleep") {
      mood("startle", 1100);
      later(120, () => say(pick(["I'm up! I'm up!", "Wasn't sleeping!", "Huh? Who's there?"]), 1700));
    }
  }
  ["pointermove", "pointerdown", "keydown", "wheel", "touchstart"].forEach((ev) =>
    window.addEventListener(ev, active, { passive: true }));
  setInterval(() => {
    if (!settled || leaving || current || hovering || !panel.hidden || still) return;
    const idleFor = Date.now() - lastActive;
    if (idleFor > SLEEP_AFTER) { mood("sleep", 0); look(0, 0); }
    else if (idleFor > YAWN_AFTER && !yawned) {
      yawned = true;
      mood("yawn", 2400);
      say(pick(["*yawwwn*", "So… sleepy…", "*big yawn*"]), 1900);
    }
  }, 3000);

  // Fling the page about and he gets dizzy.
  let lastY = window.scrollY, lastT = performance.now(), flung = 0, dizzyUntil = 0;
  window.addEventListener("scroll", () => {
    const t = performance.now(), dy = Math.abs(window.scrollY - lastY), dt = Math.max(16, t - lastT);
    lastY = window.scrollY;
    lastT = t;
    active();
    flung = dy / dt > 2 ? flung + dy : Math.max(0, flung - 200);
    if (flung > 1500 && settled && !leaving && t > dizzyUntil && panel.hidden && !still && current !== "startle") {
      dizzyUntil = t + 9000;
      flung = 0;
      mood("dizzy", 2600);
      look(0, 0);
      later(200, () => say(pick(["Whoa…", "Slow down!", "The trees are spinning…"]), 1800));
    }
  }, { passive: true });

  // Little reactions to what you do on the page.
  document.addEventListener("copy", () => {
    if (!settled || leaving || (current && current !== "happy")) return;
    mood("happy", 1000);
    say(pick(["Copy that. Over.", "10-4, copied!", "Copy that!"]), 1800);
  });
  let bottomSaid = false;
  window.addEventListener("scroll", () => {
    const doc = document.documentElement;
    if (bottomSaid || !settled || leaving || current || doc.scrollHeight < window.innerHeight * 2.5) return;
    if (window.innerHeight + window.scrollY < doc.scrollHeight - 4) return;
    bottomSaid = true;
    mood("surprised", 700);
    later(720, () => {
      if (current) return;
      mood("happy", 1100);
      say(pick(["You read it all?! Respect.", "The bottom! Just footer down here.", "Made it! Want a snack?"]), 2400);
    });
  }, { passive: true });
  let awayAt = 0;
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) { awayAt = Date.now(); return; }
    if (!awayAt || Date.now() - awayAt < 20000 || !settled || leaving || current === "sleep") return;
    mood("happy", 1100);
    later(150, () => say(pick(["Oh! You're back!", "Missed you!", "Welcome back to the woods!"]), 1800));
  });
  // Type "squatch" anywhere outside a text box: the secret dance.
  let typed = "";
  document.addEventListener("keydown", (e) => {
    const t = e.target;
    if (t && (t.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName))) return;
    if (!e.key || e.key.length !== 1) return;
    typed = (typed + e.key.toLowerCase()).slice(-7);
    if (typed === "squatch" && settled && !leaving) {
      typed = "";
      mood("dance", 3600);
      say("You found the secret dance!", 2600);
    }
  });
  // Off to install: he cheers you on.
  document.addEventListener("click", (e) => {
    const go = e.target.closest && e.target.closest('a[href="/install"], a[href="/t-deck"], .qs-btn');
    if (!go || !settled || leaving) return;
    mood("happy", 1200);
    say(pick(["Let's gooo!", "Yes! Do it!", "Welcome to the mesh!"]), 1500);
  });
  // Halloween week: the hat comes out.
  const now = new Date();
  if ((now.getMonth() === 9 && now.getDate() >= 20) || (now.getMonth() === 10 && now.getDate() <= 1)) root.classList.add("spooky");

  // Every so often, a little something of his own.
  const GAGS = {
    pager() {
      mood("pager", 3200);
      look(0.2, 1);
      say(pick(["Texting the forest…", "Anyone on the mesh?", "Checking my messages…"]), 2300);
      later(3250, () => {
        if (current) return;
        mood("happy", 1200);
        say(pick(["Delivered. 3 hops!", "Sent! No bars needed.", "Heard by 4 repeaters!"]), 2000);
      });
    },
    sneeze() {
      mood("sneeze", 2300);
      say("Ah… ah…", 1000);
      later(1080, () => say("CHOO!", 900));
      later(2350, () => {
        if (current) return;
        mood("happy", 900);
        say(pick(["'Scuse me.", "Pine pollen. Sorry.", "Bless me."]), 1500);
      });
    },
    duck() {
      root.classList.add("duck");
      later(1900, () => {
        root.classList.remove("duck");
        if (current || leaving) return;
        mood("happy", 900);
        say(pick(["Boo!", "Peekaboo!", "Still here!"]), 1300);
      });
    },
    sign() {
      const lines = pick([["NOT A GUY", "IN A SUIT"], ["MESH", "> CELL"], ["NO BARS?", "NO PROBLEM"], ["WILL WAVE", "FOR HOPS"],
        ["HONK IF", "YOU'RE A NODE"], ["REAL. ALSO", "BLURRY."]]);
      const spans = root.querySelectorAll(".sqh-sign-text tspan");
      spans[0].textContent = lines[0];
      spans[1].textContent = lines[1];
      mood("sign", 3600);
      look(-0.6, 0.1);
    },
    look() {
      mood("look", 2100);
      [[-1, 0], [1, 0.1], [-1, -0.2], [0.7, 0]].forEach((p, i) => later(i * 380, () => { if (current === "look") look(p[0], p[1]); }));
      say(pick(["Did someone say mesh?", "Was that a repeater?", "I heard a ping…"]), 2100);
    },
  };
  function scheduleGag() {
    // a few close together, then he calms down so a long read isn't a show
    later(gags < 4 ? 12000 + Math.random() * 10000 : 35000 + Math.random() * 25000, () => {
      if (leaving) return;
      if (!current && !hovering && panel.hidden && !document.hidden && !root.classList.contains("duck")) {
        const names = Object.keys(GAGS).filter((n) => n !== lastGag);
        lastGag = pick(names);
        gags++;
        GAGS[lastGag]();
      }
      scheduleGag();
    });
  }

  // "Need a hand?" once per tab, once he's out.
  function greet(at) {
    let greeted = false;
    try { greeted = sessionStorage.getItem(HELLO) === "1"; } catch (e) { /* fine */ }
    if (greeted || pageChat) return;
    later(at, () => {
      if (root.hidden || leaving || !panel.hidden) return;
      try { sessionStorage.setItem(HELLO, "1"); } catch (e) { /* fine */ }
      const h = new Date().getHours();
      say(h >= 22 || h < 5 ? pick(["Up late? Me too.", "Night owl? Need a hand?"]) : "Need a hand?", 9000);
    });
  }

  // The entrance: the whole break-in on the first page of a visit, a quick pop-out after.
  function enter() {
    let first = true;
    try { first = sessionStorage.getItem(BROKE) !== "1"; sessionStorage.setItem(BROKE, "1"); } catch (e) { /* fine */ }
    if (still) { root.classList.add("idle"); settled = true; greet(800); return; }
    root.classList.add(first ? "enter" : "enter-quick");
    if (first) {
      later(3200, () => { if (!leaving) mood("surprised", 650); });     // oh! somebody's there
      later(3850, () => { if (!leaving) mood("happy", 1300); });
    } else {
      later(800, () => { if (!leaving) mood("happy", 1000); });
    }
    later(first ? 4550 : 1700, () => {
      root.classList.remove("enter", "enter-quick");
      if (leaving) return;
      root.classList.add("idle");
      settled = true;
      scheduleGag();
    });
    greet(first ? 3900 : 1500);
  }

  // For trying his moods out by hand: ?sqh=debug in the address puts them on window.sqh.
  if (/[?&]sqh=debug\b/.test(location.search)) window.sqh = { mood, say, react, gags: GAGS, root };

  // Show him once his stylesheet is in (or after a moment regardless).
  const css = document.createElement("link");
  css.rel = "stylesheet";
  css.href = "/assets/squatch-helper.css?v=13";
  let shown = false;
  const show = () => {
    if (shown) return;
    shown = true;
    root.hidden = false;
    if (scene_.pauseAnimations) scene_.pauseAnimations();
    enter();
  };
  css.addEventListener("load", show);
  later(2500, show);
  document.head.appendChild(css);
  document.body.appendChild(root);
})();
