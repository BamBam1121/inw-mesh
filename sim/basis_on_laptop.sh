#!/bin/sh
# The theme maker's pictures (squatch_sim ... basis), for the T-Deck and the pager:
# built here, run on the laptop (Smart App Control blocks new programs on this PC),
# fetched into .pio/sim/basis/<board>/. tools/theme_atlas.py packs them for the site.
set -e
cd "$(dirname "$0")/.."
L=camde@100.99.121.13
for BOARD in tdeck pager; do
  if [ "$BOARD" = pager ]; then python sim/build.py --pager | tail -1; EXE=.pio/sim/squatch_sim_pager.exe
  else python sim/build.py | tail -1; EXE=.pio/sim/squatch_sim.exe; fi
  scp -q "$EXE" "$L:C:/Users/camde/squatch_sim/basis_$BOARD.exe" 2>/dev/null
  ssh "$L" "cd /d C:\Users\camde\squatch_sim && (if exist basis_$BOARD rmdir /s /q basis_$BOARD) && basis_$BOARD.exe basis_$BOARD basis > basis_$BOARD.log 2>&1 & tar -cf basis_$BOARD.tar basis_$BOARD" 2>/dev/null
  rm -rf ".pio/sim/basis/$BOARD" && mkdir -p .pio/sim/basis
  scp -q "$L:C:/Users/camde/squatch_sim/basis_$BOARD.tar" .pio/sim/basis/ 2>/dev/null
  tar -xf ".pio/sim/basis/basis_$BOARD.tar" -C .pio/sim/basis && mv ".pio/sim/basis/basis_$BOARD" ".pio/sim/basis/$BOARD" && rm ".pio/sim/basis/basis_$BOARD.tar"
  echo "$BOARD: $(ls .pio/sim/basis/$BOARD | wc -l) pictures"
done
