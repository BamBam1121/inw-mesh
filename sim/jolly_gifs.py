"""Squatchmas frames from the simulator -> GIFs and contact sheets.

usage: python sim/jolly_gifs.py DIR jolly|jollyfx
  jolly:   xm_OUTFIT_PLACE.ppm -> sheet.png (outfits down, places across);
           xmwalk_PLACE_NNN.ppm -> walk_PLACE.gif (33 ms a frame)
  jollyfx: xfx_KIND_NNN.ppm -> fx_KIND.gif (20 ms a frame, the first and last held)
"""
import glob, os, re, sys
from PIL import Image

d, mode = sys.argv[1], sys.argv[2]


def load(p):
    return Image.open(p).convert("RGB")


def gif(frames, path, ms, hold_first=0, hold_last=0):
    # GIF times are in 10 ms steps: a frame interval between two of them is played as a
    # repeating mix (25 ms: 20, 30; 42 ms: 40, 40, 50), so the speed comes out right.
    pal = [f.convert("P", palette=Image.ADAPTIVE, colors=255) for f in frames]
    pat = {25: [20, 30], 42: [40, 40, 50]}.get(ms, [ms])
    durs = [pat[i % len(pat)] for i in range(len(pal))]
    if hold_first: durs[0] = hold_first
    if hold_last: durs[-1] = hold_last
    pal[0].save(path, save_all=True, append_images=pal[1:], duration=durs, loop=0, optimize=False, disposal=1)


if mode == "jolly":
    outfits = ["santa", "elf", "reindeer", "sweater", "snowman", "nutcracker", "woolies"]
    places = ["cabin", "treefarm", "lake", "tower"]
    first = load(glob.glob(os.path.join(d, "xm_*.ppm"))[0])
    w, h = first.size
    sheet = Image.new("RGB", (len(places) * (w + 6) + 6, len(outfits) * (h + 6) + 6), (40, 40, 40))
    for r, o in enumerate(outfits):
        for c, p in enumerate(places):
            f = os.path.join(d, "xm_%s_%s.ppm" % (o, p))
            if os.path.exists(f):
                sheet.paste(load(f), (6 + c * (w + 6), 6 + r * (h + 6)))
    sheet.save(os.path.join(d, "sheet.png"))
    for p in places:
        fs = sorted(glob.glob(os.path.join(d, "xmwalk_%s_*.ppm" % p)))
        if fs:
            gif([load(f) for f in fs], os.path.join(d, "walk_%s.gif" % p), 42)
    print(d, "sheet + walks")
else:
    kinds = sorted({re.match(r"xfx_(\w+?)_\d+\.ppm", os.path.basename(f)).group(1) for f in glob.glob(os.path.join(d, "xfx_*.ppm"))})
    for k in kinds:
        fs = sorted(glob.glob(os.path.join(d, "xfx_%s_*.ppm" % k)))
        gif([load(f) for f in fs], os.path.join(d, "fx_%s.gif" % k), 25, hold_first=500, hold_last=700)
    print(d, "gifs:", ", ".join(kinds))
for f in glob.glob(os.path.join(d, "*.ppm")):
    os.remove(f)
