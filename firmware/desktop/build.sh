#!/bin/sh
# Builds the engine natively, three trace players and a pretend FM-1 board:
#   build/rack_cli       the browser configuration (float buffers, 6 voices of anything)
#   build/rack_cli_fm1   the FM-1 configuration (-DRACK_FM1, arena of $ARENA_KB, default 340 = the page's 384K
#                        FREE RAM less the engine's 44 KB of fixed memory)
#   build/rack_cli_fm1_big  the FM-1 configuration with an 8 MB arena, for checking the arena changes nothing else
#   build/fake_board     hw/fm1_app.cc (the firmware's app glue) on a pretend board: plays a chord, writes a WAV
# Needs a C++ compiler (clang++ or g++) and a checkout of github.com/pichenettes/eurorack with stmlib:
#   EURORACK=/path/to/eurorack ARENA_KB=340 sh firmware/desktop/build.sh
set -e
cd "$(dirname "$0")"
ENG=../../engine
EURORACK="${EURORACK:-../../../eurorack}"
ARENA_KB="${ARENA_KB:-340}"
CXX="${CXX:-c++}"
FLAGS="-O2 -ffast-math -fno-exceptions -fno-rtti -DTEST -I$ENG/shim -I$EURORACK -w"
mkdir -p build/obj
while read -r src; do
  [ -z "$src" ] && continue
  o="build/obj/$(echo "$src" | sed 's#/dsp/##; s#/#_#g; s#\.cc$##').o"
  [ -f "$o" ] || { echo "cc $src"; $CXX $FLAGS -c "$EURORACK/$src" -o "$o"; }
done < $ENG/sources.txt
$CXX $FLAGS -c $ENG/grids_nodes.cc -o build/obj/x_grids_nodes.o
$CXX $FLAGS -c $ENG/marbles_scales.cc -o build/obj/x_marbles_scales.o
$CXX $FLAGS -c $ENG/rack.cc -o build/rack.o
$CXX $FLAGS -DRACK_FM1 -DRACK_ARENA_KB=$ARENA_KB -c $ENG/rack.cc -o build/rack_fm1.o
$CXX $FLAGS -c rack_cli.cc -o build/cli.o
$CXX $FLAGS -DRACK_FM1 -c rack_cli.cc -o build/cli_fm1.o
$CXX build/cli.o build/rack.o build/obj/*.o -o build/rack_cli
$CXX $FLAGS -DRACK_FM1 -DRACK_ARENA_KB=8192 -c $ENG/rack.cc -o build/rack_fm1_big.o
$CXX build/cli_fm1.o build/rack_fm1.o build/obj/*.o -o build/rack_cli_fm1
$CXX build/cli_fm1.o build/rack_fm1_big.o build/obj/*.o -o build/rack_cli_fm1_big
$CXX $FLAGS -DRACK_FM1 -c ../hw/fm1_app.cc -o build/fm1_app.o
$CXX $FLAGS -DRACK_FM1 -c fake_board.cc -o build/fake_board.o
$CXX build/fake_board.o build/fm1_app.o build/rack_fm1.o build/obj/*.o -o build/fake_board
echo "built build/fake_board (the app glue on a pretend FM-1)"
echo "built build/rack_cli, build/rack_cli_fm1 (arena ${ARENA_KB} KB) and build/rack_cli_fm1_big"
