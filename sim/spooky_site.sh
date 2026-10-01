#!/bin/sh
# The Halloween theme's pictures for the website and README, from the pager simulator
# (run on the laptop): the lock animation, a still of it, and the home screen.
#   sh sim/spooky_site.sh "Squatch Mesh"   ->  .pio/sim/site/{anim,home}/
set -e
cd "$(dirname "$0")/.."
NAME="${1:-Squatch Mesh}"
L=camde@100.99.121.13
python sim/build.py --pager | tail -1
scp -q .pio/sim/squatch_sim_pager.exe "$L:C:/Users/camde/squatch_sim/site_pager.exe" 2>/dev/null
ssh "$L" "cd /d C:\Users\camde\squatch_sim && (if exist site_anim rmdir /s /q site_anim) && (if exist site_home rmdir /s /q site_home) && site_pager.exe site_anim spookyanim \"$NAME\" > site.log 2>&1 & site_pager.exe site_home homes \"$NAME\" >> site.log 2>&1 & tar -cf site.tar site_anim site_home" 2>/dev/null
rm -rf .pio/sim/site && mkdir -p .pio/sim/site
scp -q "$L:C:/Users/camde/squatch_sim/site.tar" .pio/sim/site/ 2>/dev/null
tar -xf .pio/sim/site/site.tar -C .pio/sim/site && rm .pio/sim/site/site.tar
echo "anim: $(ls .pio/sim/site/site_anim | wc -l) frames, home: $(ls .pio/sim/site/site_home | wc -l)"
