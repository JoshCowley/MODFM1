# FM1 Rack firmware (in progress)

**Status: not FM1 Rack firmware yet, and nothing here has run on an FM-1.** The one thing that builds into an
installable package is `fm1/bench/`, a test firmware that times the engine (you build it yourself, with fm1-base).
This folder is steps 2 and 3 of the roadmap in the developer guide: the engine made portable, shrunk to a
memory budget, built on a computer and tested. Step 1 (building Felucca and proving you can flash and recover
an FM-1) and steps 4 onwards (running on the board) are still to do.

## What's here

| Path | What it is |
|---|---|
| `../engine/rack.cc` | The engine. One source for both builds: the browser's `rack.wasm` (default) and the firmware-ready build (`-DRACK_FM1`) |
| `engine/rack.h` | The engine's C interface, for the platform layer |
| `desktop/build.sh` | Builds the engine natively, the trace players and a pretend FM-1 board |
| `desktop/build_wasm32.sh` | Builds the FM-1 trace player for a 32-bit target, for exact memory numbers |
| `desktop/size_pi32v2.sh` | Compiles everything with JieLi's FM-1 compiler and reports flash and RAM per module |
| `hw/cxx/` | The few C++ standard headers the Mutable code needs, which JieLi's toolchain doesn't have |
| `desktop/rack_cli.cc` | Plays a trace (engine calls with times) through the engine and reports levels, memory and voices |
| `desktop/fake_board.cc` | A pretend FM-1: drives `hw/fm1_app.cc` with 64-frame audio blocks, keys and MIDI, writes a WAV |
| `hw/fm1_platform.h` | The seam between FM1 Rack and the board: what the app gets called with, and what the board must provide |
| `hw/fm1_app.cc` | The app glue: call queue into the audio interrupt, 16-bit audio, keys and MIDI notes. Screen, sequencer, chords and storage are still to port |
| `tests/` | Traces of the factory presets, the players and the test runner |
| `fm1/bench/` | The benchmark firmware (fm1-base app): FPU check and module timings on the FM-1 |
| `hw/libc/` | C headers for fm1-base builds, which link no C library |
| `PORTING.md` | What's left, in order, with the open questions |

## The FM-1 build of the engine

`-DRACK_FM1` changes three things; everything else is the browser engine, line for line.

- **One memory arena.** The large buffers come from one static block of `RACK_ARENA_KB` (default 340 KB), laid out
  for the modules actually loaded: effects first, then as many voices as fit (up to 6). If the effects or a
  single voice don't fit, it unloads modules in this order: FX 2, FX 1, the shaper, the source. It never writes
  outside the arena. `fm1_status()` reports what it did.
- **Delay 1 s, 16-bit.** The delay line is 1 s (it was 2 s) and the delay and chorus lines store 16-bit samples.
- **No browser parts.** Nothing depends on WebAssembly; the exports are plain C functions.

Memory, measured from the 32-bit build (the FM-1's core is 32-bit):

| | Fixed | Per voice |
|---|---|---|
| Engine (voices, matrix, LFOs, Peaks, Tides, Marbles, Grids, filter, Clouds' processor, master) | 44 KB | |
| Plaits | | 24.7 KB |
| Braids | | 16.6 KB |
| Elements | | 174.9 KB |
| Rings | | 171.9 KB |
| Warps | | 71.0 KB |
| Clouds | 179.9 KB | |
| Delay (1 s stereo, 16-bit) | 187.5 KB | |
| Reverb (with shimmer and pre-delay) | 72 KB | |
| Chorus | 16 KB | |

So 384 KB of free RAM (the page's middle setting) means a 340 KB arena. The FM-1's real free RAM is still
unknown: your notes say about 578 KB of SRAM in total (unverified), and the firmware, screen buffer, audio
buffers and stack need some of it. Change `RACK_ARENA_KB` once that's measured.

The page's FM-1 preview (GLOBAL > HARDWARE) uses the same numbers and the same unloading order, and the
tests check the two agree for every factory preset.

## Build and test

You need a C++ compiler, Node 18+, and a checkout of the Mutable Instruments code with its stmlib submodule:

```
git clone --recursive https://github.com/pichenettes/eurorack ../eurorack
EURORACK=$PWD/../eurorack sh firmware/desktop/build.sh
node firmware/tests/run_tests.mjs
```

For exact 32-bit memory numbers, also build the wasm32 engine objects and the 32-bit player (needs clang 18 with
the wasm32-wasi libraries, as for the browser build):

```
EURORACK=$PWD/../eurorack sh engine/build.sh
sh firmware/desktop/build_wasm32.sh
node firmware/tests/run_tests.mjs
```

The tests:

1. **Native engine = browser engine**: all 42 factory presets through `rack_cli` and through `web/rack.wasm`
   (worst difference 0.05% of loudness).
2. **The arena changes nothing else**: the FM-1 build with room for everything matches the page's FM-1
   preview (set_hw), preset by preset, each from a clean start (worst difference 0.51%).
3. **FM-1 at 340 KB**: every preset stays inside the arena with no NaN, and the voices that fit and the
   modules unloaded match what the page predicts (0 mismatches). At 340 KB, GLASS BELLS and KARPLUS lose their
   Rings; BOWED SPACE, LD BREATH and PD SYMPATH fit one voice.
4. **App glue**: `hw/fm1_app.cc` on the pretend board plays a chord from the keys and a MIDI note.

`tests/record_calls.mjs` re-records the traces from the page (with Playwright) when presets or the page change;
then run `tests/make_trace.mjs`.

## Flash: measured with the FM-1's own compiler

`desktop/size_pi32v2.sh` compiles everything with JieLi's pi32v2 compiler and the FM-1's float flags and links it:
**961 KB** with every module, **362 KB** for the Lite set (`RACK_EXTRA=-DRACK_LITE`), against 568 KB for a whole
firmware. See PORTING.md for the breakdown.

## The benchmark firmware

`fm1/bench/` is a test firmware on fm1-base that checks the FPU and times FM1 Rack's Lite engine on the FM-1 (105
cases). It builds (551 KB) and passes fm1-base's checks, and its computer check (`fm1/bench/build_host.sh`) passes;
it hasn't run on an FM-1 yet. Its README has the steps.

## What the tests can't tell you

- **CPU.** `rack_cli` reports how long rendering takes on your computer. That says nothing about the FM-1's
  pi32v2 core; the page's CPU numbers are measured ratios on an assumed scale.
- **Sample rate.** The engine runs at 48 kHz. The stock FM-1 reportedly runs at 44,118 Hz.
- **Anything about the board**: screen, keys, knobs, flash, USB. That's step 1 and step 4 onwards.
