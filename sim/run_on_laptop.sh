#!/bin/sh
# Build the simulator here, run it on the laptop (this PC's Smart App Control
# blocks new unsigned programs; the laptop's Windows 10 has none), fetch frames.
set -e
cd "$(dirname "$0")/.."
python sim/build.py $1 | tail -1
EXE=.pio/sim/squatch_sim${1:+_pager}.exe
ssh -o ConnectTimeout=15 camde@100.99.121.13 "if not exist C:\Users\camde\squatch_sim mkdir C:\Users\camde\squatch_sim" 2>/dev/null
scp -q "$EXE" camde@100.99.121.13:C:/Users/camde/squatch_sim/squatch_sim.exe 2>/dev/null
ssh camde@100.99.121.13 "cd /d C:\Users\camde\squatch_sim && (if exist shots rmdir /s /q shots) && squatch_sim.exe shots > run.log 2>&1" 2>/dev/null
rm -rf .pio/sim/shots && mkdir -p .pio/sim/shots
scp -q "camde@100.99.121.13:C:/Users/camde/squatch_sim/shots/*" .pio/sim/shots/ 2>/dev/null
python - <<'PY'
from PIL import Image, ImageDraw
import glob, os
os.chdir(".pio/sim/shots")
for f in glob.glob("*.ppm"): Image.open(f).save(f.replace(".ppm", ".png")); os.remove(f)
order = ["lock", "home_messages", "home_contacts", "home_map", "home_tools", "home_settings", "chats", "thread_inw", "thread_trailhead"]
for theme in ["squatch", "blocks", "hero", "aurora", "halloween"]:
    ims = [(n, Image.open("%s_%s.png" % (theme, n))) for n in order if os.path.exists("%s_%s.png" % (theme, n))]
    if not ims: continue
    w, h = ims[0][1].size
    sheet = Image.new("RGB", (3 * (w + 10) + 10, 3 * (h + 26) + 10), (50, 50, 50))
    d = ImageDraw.Draw(sheet)
    for k, (n, im) in enumerate(ims):
        x = 10 + (k % 3) * (w + 10); y = 10 + (k // 3) * (h + 26)
        d.text((x, y), theme + " " + n, fill=(230, 230, 230)); sheet.paste(im, (x, y + 16))
    sheet.save("../sheet_%s.png" % theme)
print(len(glob.glob("*.png")), "screens")
PY
