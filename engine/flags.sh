# Compiler flags for the wasm32-wasi build. Set EURORACK to your checkout of
# https://github.com/pichenettes/eurorack (with the stmlib submodule).
EURORACK="${EURORACK:-../../eurorack}"
CXX="clang++ --target=wasm32-wasi --sysroot=/usr -nostdinc++ -isystem /usr/lib/llvm-18/include/wasm32-wasi/c++/v1 -isystem /usr/include/wasm32-wasi -O2 -fno-exceptions -fno-rtti -ffast-math -DTEST -Ishim -I$EURORACK -Wno-everything"
