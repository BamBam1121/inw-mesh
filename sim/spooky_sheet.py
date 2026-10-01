"""Turn the simulator's Halloween pictures (sim/spooky_on_laptop.sh) into PNGs and two
contact sheets: every costume in every place, and each costume saying hello.

usage: python sim/spooky_sheet.py DIR
"""
import glob, os, sys
from PIL import Image, ImageDraw

d = sys.argv[1]
for f in glob.glob(os.path.join(d, "*.ppm")):
    Image.open(f).save(f[:-4] + ".png")
    os.remove(f)

COSTUMES = ["zombie", "witch", "vampire", "ghost", "pumpkin", "skeleton", "mummy"]
PLACES = ["street", "graveyard", "patch", "woods"]


def sheet(names, cols, out):
    ims = [(n, Image.open(os.path.join(d, n + ".png"))) for n in names if os.path.exists(os.path.join(d, n + ".png"))]
    if not ims:
        return
    w, h = ims[0][1].size
    rows = (len(ims) + cols - 1) // cols
    s = Image.new("RGB", (cols * (w + 8) + 8, rows * (h + 22) + 8), (48, 48, 48))
    dr = ImageDraw.Draw(s)
    for k, (n, im) in enumerate(ims):
        x, y = 8 + (k % cols) * (w + 8), 8 + (k // cols) * (h + 22)
        dr.text((x, y), n, fill=(230, 230, 230))
        s.paste(im, (x, y + 14))
    s.save(os.path.join(d, out))
    print(out, len(ims), "pictures")


sheet(["sp_%s_%s" % (c, p) for c in COSTUMES for p in PLACES], 4, "sheet_all.png")
sheet(["sphello_%s" % c for c in COSTUMES], 4, "sheet_hello.png")
