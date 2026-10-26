#!/bin/sh
# Build rack.wasm from scratch: every Mutable source in sources.txt, the two
# extracted data files, then rack.cc, linked as a wasm32-wasi reactor.
# Needs clang 18 plus Ubuntu's wasi-libc and libc++-18-dev-wasm32 packages.
set -e
cd "$(dirname "$0")" && . ./flags.sh
mkdir -p obj
while read -r src; do
  [ -z "$src" ] && continue
  o="obj/$(echo "$src" | sed 's#/dsp/##; s#/#_#g; s#\.cc$##').o"
  [ -f "$o" ] || { echo "cc $src"; $CXX -c "$EURORACK/$src" -o "$o"; }
done < sources.txt
$CXX -c grids_nodes.cc -o obj/x_grids_nodes.o
$CXX -c marbles_scales.cc -o obj/x_marbles_scales.o
$CXX -c rack.cc -o obj/rack.o
clang++ --target=wasm32-wasi --sysroot=/usr -O2 -mexec-model=reactor -Wl,--no-entry -Wl,--export-dynamic -Wl,--gc-sections -Wl,-z,stack-size=1048576 -Wl,--initial-memory=33554432 -L/usr/lib/wasm32-wasi -L/usr/lib/llvm-18/lib/wasm32-wasi obj/*.o -lc++ -lc++abi -o rack.wasm 2>&1 | grep -v warning || true
ls -la rack.wasm
echo "Copy rack.wasm into ../web/ to use it."
