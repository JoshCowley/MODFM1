#!/bin/sh
# The FM1 Rack firmware's sound side checked on a computer (rack_host.c): the engine built as the FM-1's
# (-DRACK_FM1 -DRACK_LITE, the arena as fm1.toml sets it). Needs a C and C++ compiler and the Mutable code:
#   EURORACK=/path/to/eurorack sh build_host.sh && build/host/rack_host
set -e
cd "$(dirname "$0")"
EURORACK="${EURORACK:-../../../../eurorack}"
ENG=../../../engine
OUT=build/host
mkdir -p $OUT
python3 make_presets.py --full
ARENA=$(sed -n 's/.*-DRACK_ARENA_KB=\([0-9]*\).*/\1/p' fm1.toml)
CXX="${CXX:-c++} -O2 -ffp-contract=off -fno-exceptions -fno-rtti -std=gnu++11 -w -DTEST -DRACK_FM1 -DRACK_LITE -DRACK_ARENA_KB=$ARENA -DRACK_RINGS16 -I$ENG/shim -I$EURORACK"
for src in $(grep -vE '^(elements|warps|clouds|marbles)/' $ENG/sources.txt) ; do
  o="$OUT/$(echo "$src" | sed 's#/#_#g; s#\.cc$##').o"
  [ -f "$o" ] || { echo "c++ $src"; $CXX -c "$EURORACK/$src" -o "$o"; }
done
$CXX -c $ENG/grids_nodes.cc -o $OUT/x_grids_nodes.o
$CXX -c $ENG/rack.cc -o $OUT/rack.o
$CXX -c ../common/rk.cc -o $OUT/rk.o
${CC:-cc} -O2 -ffp-contract=off -std=gnu99 -Wall -Wno-unused-function -c rack_host.c -o $OUT/rack_host.o
${CXX%% *} -o $OUT/rack_host $OUT/*.o -lm
echo "built $OUT/rack_host"
