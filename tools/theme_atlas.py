"""Packs the simulator's "basis" pictures into what the website's theme maker loads, and
checks the sum the page does against real renders.

    sh sim/basis_on_laptop.sh          # makes .pio/sim/basis/<board>/b<look>_<screen>_<kk>.png
    python tools/theme_atlas.py OUTDIR # checks, then writes OUTDIR/theme-<board>-<look>.png + themes.json

How the page draws a screen in someone's colours: every pixel is

    fixed + sum over the 15 palette colours of (weight_i x colour_i)

per channel, in the screen's own 5-6-5 bits. The simulator draws each screen in fifteen
dark colours (picture 00), then again with one colour at a time nearly white (pictures
01-15): how much a pixel brightens with colour i is that colour's weight there, and what
is left of picture 00 once the dark colours' share is taken off is `fixed` (what no theme
changes, such as avatars). An atlas is `fixed` and the 15 weights of one look, one screen
after another across, one picture under another down.
"""
import json, os, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
BASIS = os.path.join(HERE, "..", ".pio", "sim", "basis")
LOOKS = ["squatch", "blocks", "hero", "aurora"]
SCREENS = {"tdeck": ["lock", "home", "chats", "thread", "settings"], "pager": ["lock", "home", "chats", "thread"]}
COLOURS = ["bg", "panel", "line", "green", "greenDim", "txt", "dim", "amber", "red", "white",
           "bubbleIn", "bubbleOut", "blue", "focus", "mentionBg"]
PALETTES = [  # themes.h, the same order
    [0x060a09, 0x0b120e, 0x16241c, 0x3dffa8, 0x1f6f4e, 0xb9d4c6, 0x5f8074, 0xe6b955, 0xff5a5a, 0xe8f2ed, 0x161d1a, 0x0f3d33, 0x4da3ff, 0x122a21, 0x3a3012],
    [0x0e1622, 0x2b2118, 0x45362a, 0x6cc24a, 0x3f7a2c, 0xe8e0d0, 0x9a8f7a, 0xf2b233, 0xd9412e, 0xffffff, 0x3a3a3a, 0x2f5a22, 0x5aa9e6, 0x3b2d20, 0x4a3a12],
    [0x081420, 0x10263a, 0x1d3a52, 0xf2c14e, 0x2e7d4f, 0xe6edf2, 0x7f98ab, 0xff9f43, 0xe84a5f, 0xffffff, 0x13283a, 0x1e4a33, 0x5cc8ff, 0x163248, 0x3a2d10],
    [0x040716, 0x0b1230, 0x18214a, 0x72f5c8, 0x6a4fc2, 0xd6e4ff, 0x7c86b8, 0xffc46b, 0xff6b8b, 0xffffff, 0x121a3e, 0x1b3a4a, 0x7db8ff, 0x141d45, 0x2c2250],
]
LIGHT = [0xf4efe6, 0xe9e2d4, 0xcfc6b4, 0xb3261e, 0x7a5c58, 0x2a2622, 0x6f675c, 0x8a5a00, 0xc62828, 0x000000, 0xe2dccf, 0xf3c9c4, 0x1f5fbf, 0xead9c8, 0xf7e2a6]
FULL = np.array([31, 63, 31], dtype=np.int32)


def anything(look):
    h = (0x9e3779b9 * (look + 7)) & 0xFFFFFFFF
    out = []
    for _ in range(15):
        h ^= (h << 13) & 0xFFFFFFFF; h ^= h >> 17; h ^= (h << 5) & 0xFFFFFFFF
        out.append(h & 0xFFFFFF)
    return out


def bits(img):
    """A picture in the screen's own bits: (h, w, 3) of 0..31, 0..63, 0..31."""
    a = np.asarray(img.convert("RGB"), dtype=np.int32)
    return (a * FULL + 127) // 255


def load(board, look, screen, k):
    return bits(Image.open(os.path.join(BASIS, board, "b%d_%s_%02d.png" % (look, screen, k))))


def palette_bits(p):
    """What the firmware makes of 0xRRGGBB (color565): the top 5, 6 and 5 bits."""
    return np.array([[(c >> 16) >> 3, ((c >> 8) & 255) >> 2, (c & 255) >> 3] for c in p], dtype=np.int32)


# The simulator's fifteen dark colours and its nearly-white (sim_main.cpp, "basis").
DARK = np.array([[1 + i % 3, 2 + i % 5, 1 + i % 2] for i in range(15)], dtype=np.int32)
BRIGHT = np.array([30, 60, 29], dtype=np.int32)


def split(board, look, screen):
    """A screen's `fixed` picture and its 15 weights (0..31, 0..63, 0..31 = none..all)."""
    dark = load(board, look, screen, 0)
    weights, rest = [], dark * FULL
    for i in range(15):
        lift = load(board, look, screen, 1 + i) - dark
        w = np.clip((lift * FULL + (BRIGHT - DARK[i]) // 2) // (BRIGHT - DARK[i]), 0, FULL)
        weights.append(w)
        rest = rest - w * DARK[i]
    return np.clip((rest + FULL // 2) // FULL, 0, FULL), weights


def compose(fixed, weights, p):
    out = fixed * FULL                                    # in 1/31sts (1/63rds for green), rounded once at the end
    for i in range(15):
        out = out + weights[i] * p[i]
    return np.clip((out + FULL // 2) // FULL, 0, FULL)


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else None
    worst = 0
    meta = {"colours": COLOURS, "looks": LOOKS, "builtin": ["%06x" * 15 % tuple(p) for p in PALETTES], "boards": {}}
    for board, screens in SCREENS.items():
        if not os.path.isdir(os.path.join(BASIS, board)):
            print(board, "has no pictures: run sim/basis_on_laptop.sh"); continue
        size = None
        used = {}
        for look in range(4):
            layers = []                                   # 16 rows of the atlas
            for screen in screens:
                fixed, weights = split(board, look, screen)
                size = fixed.shape[1], fixed.shape[0]
                over = int((sum(weights) > FULL + 2).any(axis=2).sum())
                # The real palettes: its own, the other three looks', a light one, anything.
                truths = [PALETTES[(look + j) % 4] for j in range(4)] + [LIGHT, anything(look)]
                errs = []
                for j, pal in enumerate(truths):
                    want = load(board, look, screen, 16 + j)
                    got = compose(fixed, weights, palette_bits(pal))
                    d = np.abs(got - want)
                    errs.append((int(d.max()), float((d > 1).any(axis=2).mean() * 100), float((d > 0).any(axis=2).mean() * 100)))
                mx = max(e[0] for e in errs)
                worst = max(worst, mx)
                print("%-5s %-7s %-8s worst error %2d of 31/63 steps; pixels off by more than one step: %s %%; off at all: %s %%%s"
                      % (board, LOOKS[look], screen, mx, " ".join("%.2f" % e[1] for e in errs), " ".join("%.1f" % e[2] for e in errs),
                         "   (%d pixels where the weights add to more than all)" % over if over else ""))
                layers.append([fixed] + weights)
                # Where each colour shows, for the page's "this colour is used here".
                for i in range(15):
                    used.setdefault(screen, [0] * 15)
                    used[screen][i] = max(used[screen][i], round(float((weights[i].max(axis=2) > 8).mean() * 100), 2))
            if outdir:
                w, h = size
                atlas = np.zeros((16 * h, len(screens) * w, 3), dtype=np.uint8)
                for s, col in enumerate(layers):
                    for k, a in enumerate(col):
                        atlas[k * h:(k + 1) * h, s * w:(s + 1) * w] = (a * 255 // FULL).astype(np.uint8)
                path = os.path.join(outdir, "theme-%s-%s.png" % (board, LOOKS[look]))
                Image.fromarray(atlas, "RGB").save(path, optimize=True)
                print("   wrote %s (%d kB)" % (path, os.path.getsize(path) // 1024))
        meta["boards"][board] = {"w": size[0], "h": size[1], "screens": screens, "shows": used}
    if outdir:
        json.dump(meta, open(os.path.join(outdir, "themes.json"), "w"), separators=(",", ":"))
    print("worst error anywhere: %d step%s" % (worst, "" if worst == 1 else "s"))


if __name__ == "__main__":
    main()
