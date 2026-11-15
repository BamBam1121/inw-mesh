#!/bin/sh
# The Squatchmas theme's pictures and GIFs (squatch_sim ... jolly / jollyfx), for the
# T-Deck and the pager: built here, run on the laptop (Smart App Control blocks new
# programs on this PC), fetched into .pio/sim/jolly/<board>/<mode>/, then made into
# GIFs and contact sheets by sim/jolly_gifs.py.
#   sh sim/jolly_on_laptop.sh [jolly|jollyfx|both] [tdeck|pager|both]
set -e
cd "$(dirname "$0")/.."
L=camde@100.99.121.13
MODES=${1:-both}; [ "$MODES" = both ] && MODES="jolly jollyfx"
BOARDS=${2:-both}; [ "$BOARDS" = both ] && BOARDS="tdeck pager"
for BOARD in $BOARDS; do
  if [ "$BOARD" = pager ]; then python sim/build.py --pager | tail -1; EXE=.pio/sim/squatch_sim_pager.exe
  else python sim/build.py | tail -1; EXE=.pio/sim/squatch_sim.exe; fi
  scp -q "$EXE" "$L:C:/Users/camde/squatch_sim/xm_$BOARD.exe" 2>/dev/null
  for MODE in $MODES; do
    D=xm_${BOARD}_$MODE
    ssh "$L" "cd /d C:\Users\camde\squatch_sim && (if exist $D rmdir /s /q $D) && xm_$BOARD.exe $D $MODE > $D.log 2>&1 & tar -czf $D.tgz $D $D.log" 2>/dev/null
    rm -rf ".pio/sim/jolly/$BOARD/$MODE" && mkdir -p ".pio/sim/jolly/$BOARD"
    scp -q "$L:C:/Users/camde/squatch_sim/$D.tgz" ".pio/sim/jolly/$BOARD/" 2>/dev/null
    tar -xzf ".pio/sim/jolly/$BOARD/$D.tgz" -C ".pio/sim/jolly/$BOARD" && mv ".pio/sim/jolly/$BOARD/$D" ".pio/sim/jolly/$BOARD/$MODE"
    rm ".pio/sim/jolly/$BOARD/$D.tgz"
    python sim/jolly_gifs.py ".pio/sim/jolly/$BOARD/$MODE" "$MODE"
  done
done
