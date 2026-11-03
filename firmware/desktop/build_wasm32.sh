#!/bin/sh
# Builds build/rack_cli_fm1.wasm: the FM-1 trace player compiled for a 32-bit target (wasm32), so its memory
# numbers match a 32-bit chip like the FM-1's (the native build on a 64-bit computer has bigger pointers).
# Run engine/build.sh first: this reuses its wasm32 objects in engine/obj. tests/run_tests.mjs runs it with Node.
set -e
cd "$(dirname "$0")"
ENG=../../engine
. $ENG/flags.sh
ARENA_KB="${ARENA_KB:-340}"
mkdir -p build
( cd $ENG && $CXX -DRACK_FM1 -DRACK_ARENA_KB=$ARENA_KB -c rack.cc -o ../firmware/desktop/build/rack_fm1_32.o )
$CXX -DRACK_FM1 -c rack_cli.cc -o build/cli_fm1_32.o
printf 'extern "C" { void abort(void); void* __cxa_allocate_exception(unsigned long) { abort(); return 0; } void __cxa_throw(void*, void*, void (*)(void*)) { abort(); } }\n' > build/nothrow.cc
$CXX -c build/nothrow.cc -o build/nothrow.o
clang++ --target=wasm32-wasi --sysroot=/usr -O2 -Wl,-z,stack-size=1048576 -L/usr/lib/wasm32-wasi -L/usr/lib/llvm-18/lib/wasm32-wasi \
  build/cli_fm1_32.o build/rack_fm1_32.o build/nothrow.o $(ls $ENG/obj/*.o | grep -v '/rack.o$') -lc++ -lc++abi -o build/rack_cli_fm1.wasm 2>&1 | grep -v warning || true
ls -la build/rack_cli_fm1.wasm
