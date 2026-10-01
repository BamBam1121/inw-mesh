#!/bin/sh
# The Halloween theme's screen changes (squatch_sim ... spookyfx), for the T-Deck and
# the pager: built here, run on the laptop, fetched into .pio/sim/spookyfx/<board>/
# with a contact sheet (sheet.png: one row per change, eight moments across).
set -e
cd "$(dirname "$0")/.."
L=camde@100.99.121.13
for BOARD in tdeck pager; do
  if [ "$BOARD" = pager ]; then python sim/build.py --pager | tail -1; EXE=.pio/sim/squatch_sim_pager.exe
  else python sim/build.py | tail -1; EXE=.pio/sim/squatch_sim.exe; fi
  scp -q "$EXE" "$L:C:/Users/camde/squatch_sim/fx_$BOARD.exe" 2>/dev/null
  ssh "$L" "cd /d C:\Users\camde\squatch_sim && (if exist fx_$BOARD rmdir /s /q fx_$BOARD) && fx_$BOARD.exe fx_$BOARD spookyfx > fx_$BOARD.log 2>&1 & tar -cf fx_$BOARD.tar fx_$BOARD" 2>/dev/null
  rm -rf ".pio/sim/spookyfx/$BOARD" && mkdir -p .pio/sim/spookyfx
  scp -q "$L:C:/Users/camde/squatch_sim/fx_$BOARD.tar" .pio/sim/spookyfx/ 2>/dev/null
  tar -xf ".pio/sim/spookyfx/fx_$BOARD.tar" -C .pio/sim/spookyfx && mv ".pio/sim/spookyfx/fx_$BOARD" ".pio/sim/spookyfx/$BOARD" && rm ".pio/sim/spookyfx/fx_$BOARD.tar"
  python - ".pio/sim/spookyfx/$BOARD" <<'PY'
import os, sys
from PIL import Image
d = sys.argv[1]
kinds = ["forward", "back", "unlock", "lock", "wake", "sleep", "powerdown"]
first = Image.open(os.path.join(d, "fx_forward_0.ppm"))
w, h = first.size
sheet = Image.new("RGB", (8 * (w + 4) + 4, len(kinds) * (h + 4) + 4), (60, 60, 60))
n = 0
for r, k in enumerate(kinds):
    for i in range(8):
        f = os.path.join(d, "fx_%s_%d.ppm" % (k, i))
        if os.path.exists(f):
            sheet.paste(Image.open(f), (4 + i * (w + 4), 4 + r * (h + 4))); n += 1
sheet.save(os.path.join(d, "sheet.png"))
print(d, n, "frames")
PY
done
