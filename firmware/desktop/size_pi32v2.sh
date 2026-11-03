#!/bin/sh
# Compiles the FM-1 engine and every Mutable module with JieLi's own compiler for the FM-1's core (pi32v2,
# clang 4.0.1), links it with unused code removed, and reports how much flash and RAM it needs, per module.
# Needs the JieLi Linux toolchain (Felucca's tools/get_toolchain.sh; Linux x86-64, or Docker on a Mac) and the
# Mutable code:
#   JIELI_TOOLCHAIN=~/.jieli/toolchain EURORACK=/path/to/eurorack sh firmware/desktop/size_pi32v2.sh
# The toolchain has no C++ standard library headers; hw/cxx/ has the few the Mutable code needs.
set -e
cd "$(dirname "$0")"
TC="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
EURORACK="${EURORACK:-../../../eurorack}"
ENG=../../engine
OUT=build/pi32v2
mkdir -p $OUT
CXX="$TC/pi32v2/bin/clang++ -target pi32v2 -isystem ../hw/cxx -isystem $TC/pi32v2/include -Os -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -ffast-math -std=gnu++11 -DTEST -DRACK_FM1 -I$ENG/shim -I$EURORACK -w"
while read -r src; do
  [ -z "$src" ] && continue
  o="$OUT/$(echo "$src" | sed 's#/dsp/##; s#/#_#g; s#\.cc$##').o"
  [ -f "$o" ] || { echo "cc $src"; $CXX -c "$EURORACK/$src" -o "$o"; }
done < $ENG/sources.txt
$CXX -c $ENG/grids_nodes.cc -o $OUT/x_grids_nodes.o
$CXX -c $ENG/marbles_scales.cc -o $OUT/x_marbles_scales.o
$CXX -c $ENG/rack.cc -o $OUT/rack.o
$CXX -c ../hw/size_entry.cc -o $OUT/size_entry.o
# a plain layout: code and constants in flash, data and bss in RAM (sizes only; not a bootable image)
printf 'SECTIONS { . = 0x02000120; .text : { *(.text*) *(.rodata*) } . = 0x01C08000; .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n' > $OUT/size.ld
$TC/pi32v2/bin/ld -o $OUT/rack.elf -T $OUT/size.ld --gc-sections -e fm1_entry -Map $OUT/rack.map \
  $OUT/size_entry.o $OUT/rack.o $(ls $OUT/*.o | grep -v -e '/rack.o$' -e size_entry) -L$TC/pi32v2/lib -lm -lc -lcompiler-rt
python3 - "$OUT/rack.map" <<'PY'
import re, sys, collections
m = open(sys.argv[1]).read()
acc = collections.Counter(); ram = 0
for mm in re.finditer(r'^ (\.\S+)\s*\n?\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S+)', m, re.M):
    sec, sz, f = mm.group(1), int(mm.group(2), 16), mm.group(3).split('/')[-1]
    if sec.startswith(('.data', '.bss')): ram += sz; continue
    if not sec.startswith(('.text', '.rodata')): continue
    names = ['plaits', 'braids', 'elements', 'rings', 'warps', 'clouds', 'tides2', 'marbles', 'peaks', 'stmlib']
    mod = 'engine (rack.cc)' if f == 'rack.o' else 'C library' if f.startswith('lib') else next((n for n in names if f.startswith(n)), f)
    if f.startswith('x_'): mod = 'grids' if 'grids' in f else 'marbles'
    acc[(mod, 'tables' if 'resources' in f or f.startswith('x_') or sec.startswith('.rodata') else 'code')] += sz
mods = sorted({k[0] for k in acc}, key=lambda x: -(acc[(x, 'code')] + acc[(x, 'tables')]))
print(f"{'':18s}{'code':>9s}{'tables':>9s}{'total':>9s}   (KB, flash)")
for md in mods:
    c, t = acc[(md, 'code')] / 1024, acc[(md, 'tables')] / 1024
    if c + t >= 0.5: print(f"{md:18s}{c:9.1f}{t:9.1f}{c + t:9.1f}")
tot = sum(acc.values()) / 1024
print(f"{'flash, all':18s}{'':18s}{tot:9.1f}   Felucca's app area is 568 KB (firmware/app.ld XIP), for everything")
print(f"{'RAM (data+bss)':18s}{'':18s}{ram / 1024:9.1f}   of which the arena is the RACK_ARENA_KB setting")
PY
