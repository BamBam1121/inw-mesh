#!/bin/sh
# The Halloween theme's pictures (squatch_sim ... spooky), for the T-Deck and the pager:
# built here, run on the laptop (Smart App Control blocks new programs on this PC),
# fetched into .pio/sim/spooky/<board>/ as PNGs with a contact sheet each.
set -e
cd "$(dirname "$0")/.."
L=camde@100.99.121.13
for BOARD in tdeck pager; do
  if [ "$BOARD" = pager ]; then python sim/build.py --pager | tail -1; EXE=.pio/sim/squatch_sim_pager.exe
  else python sim/build.py | tail -1; EXE=.pio/sim/squatch_sim.exe; fi
  scp -q "$EXE" "$L:C:/Users/camde/squatch_sim/spooky_$BOARD.exe" 2>/dev/null
  ssh "$L" "cd /d C:\Users\camde\squatch_sim && (if exist spooky_$BOARD rmdir /s /q spooky_$BOARD) && spooky_$BOARD.exe spooky_$BOARD spooky > spooky_$BOARD.log 2>&1 & tar -cf spooky_$BOARD.tar spooky_$BOARD spooky_$BOARD.log" 2>/dev/null
  rm -rf ".pio/sim/spooky/$BOARD" && mkdir -p .pio/sim/spooky
  scp -q "$L:C:/Users/camde/squatch_sim/spooky_$BOARD.tar" .pio/sim/spooky/ 2>/dev/null
  tar -xf ".pio/sim/spooky/spooky_$BOARD.tar" -C .pio/sim/spooky && mv ".pio/sim/spooky/spooky_$BOARD" ".pio/sim/spooky/$BOARD" && rm ".pio/sim/spooky/spooky_$BOARD.tar"
  grep "^start" ".pio/sim/spooky/spooky_$BOARD.log" || true
  python sim/spooky_sheet.py ".pio/sim/spooky/$BOARD"
done
