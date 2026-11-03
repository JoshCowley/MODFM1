# Porting FM1 Rack to the FM-1: what's left

In order. Don't start a step until the one before passes its check. The developer guide has the background.

## 0. Before anything touches the device

- [ ] Recovery kit: a forced-download dongle (FM-1 Transporter or similar), a full backup of your own FM-1 read
      out with it, and a restore you have actually tested.
- [ ] Official V15 firmware and M-VAVE's updater, to get back to stock.
- [ ] Only flash files you built yourself from a known commit. Nothing in this repo is a firmware file.

## 1. Build Felucca unchanged and flash it (roadmap step 1)

- [ ] First, the no-build way: Felucca's web installer (https://hugelton.github.io/Felucca/, Chrome or Edge, USB)
      installs its release, and its **Return to official V15** button goes back. Felucca's README says no extra
      hardware is needed. Install, play it, return to V15, install again.
- [ ] Then your own build: toolchain and SDK as in Felucca's BUILDING.md (JieLi pi32v2 clang 4.0.1, AC79 SDK
      `AC79NN_SDK_V1.2.1_2023-12-13` from Gitee; on a Mac the toolchain runs in Docker). Build Felucca unchanged
      (`./build.sh`), install it with `python3 tools/fm1_install.py build/felucca.fwsc`, return to V15.
- Check: Felucca runs, and you can always get back to stock.

## Measured so far (November 2026)

- **The compiler works.** JieLi's clang 4.0.1 for pi32v2 (the toolchain from Felucca's `tools/get_toolchain.sh`)
  compiles `rack.cc` with `-DRACK_FM1`, all 83 Mutable source files and the two extracted data files with no changes, once given the few C++
  headers it lacks (`hw/cxx/`). `desktop/size_pi32v2.sh` does this and links with unused code removed.
- **Felucca's licence**: GPL-3.0-only; its board layer (felucca-bsp) is MPL-2.0 and its six engines are MIT
  (Felucca's LICENSING.md). Forking it for FM1 Rack (GPL-3.0) works.
- **Felucca's memory map** (`firmware/app.ld`, a firmware that runs on the FM-1): 568 KB of flash for the app's code
  and constants (XIP), and 512 KB of SRAM laid out as 72 KB for data, 352 KB for big buffers (POOL), 24 KB for code
  that runs from RAM, 8 KB for plugins, then stacks. The engine's 43 KB of fixed memory belongs in the 72 KB; the
  arena belongs in POOL, which the screen buffers also use, so `RACK_ARENA_KB` will be under 352.
- **Flash is the blocker.** The engine with every module needs **1,093 KB** of flash on pi32v2: almost twice the
  568 KB Felucca's whole app gets, before any screen, fonts or UI code. By module (code + tables):

  | Module | KB | Module | KB |
  |---|---|---|---|
  | Elements | 384.5 (364 of it tables) | Peaks | 52.2 (tables) |
  | Plaits | 171.7 | Rings | 49.1 |
  | Marbles | 82.3 | Tides | 37.1 (tables) |
  | Clouds | 81.7 | C library | 12.8 |
  | Braids | 77.5 | stmlib, Grids | 5.3 |
  | Warps | 76.9 | Engine (rack.cc) | 62.0 |

  Ways down, to decide before step 3: leave out Elements (−384 KB); keep only the tables FM1 Rack actually uses
  from Peaks, Marbles and Tides (they link whole tables files, though FM1 Rack uses only some of each: the saving
  isn't measured yet); then choose between Warps, Clouds and Marbles. The page's FM-1 preview should then offer only
  the modules that ship.

## 2. Answer the open questions (measure on the board, with Felucca)

| Question | Why it matters | Where it goes |
|---|---|---|
| Free RAM for the app (Felucca's map gives POOL 352 KB; how much of it the screen needs) | Sets `RACK_ARENA_KB` and the page's FREE RAM setting | build flags, `web/src/1-data.js` RAM_OPTS |
| Codec sample rate, and can it run at 48 kHz? | The engine is 48 kHz; at 44,118 Hz everything is ~1.5 semitones flat | `hw/fm1_app.cc` (`fm1_app_init`) |
| Audio block size and the time budget per block | CPU limit | page CPU_OPTS, voice limits |
| Hardware floating point? Speed of `sinf`, `expf`, `tanhf`, `powf` | The engine is float throughout | may need lookup tables |
| Second core usable? | Voices on one core, effects on the other | engine split |
| Cycle counter readable? | CPU measurement | `fm1_hal_cycles()` |
| Screen size and controller | Page drawing | `fm1_hal_display_rect()` |
| Flash region for patches and its erase size | Storage | `fm1_hal_flash_*()` |

## 3. Engine on the board (roadmap step 4)

- [ ] Fork Felucca; keep `hal/` and `loader/`; add `engine/rack.cc` (with `-DRACK_FM1 -DRACK_ARENA_KB=<measured>`)
      and the Mutable sources listed in `engine/sources.txt`, plus `firmware/hw/fm1_app.cc`.
- [ ] Write `fm1_hal_*` over Felucca's hal/ (`hw/fm1_platform.h` lists them) and call `fm1_app_audio()` from its
      audio interrupt, `fm1_app_key/knob/button/midi()` from its input handling.
- [ ] Start with Plaits only. Check: right pitch on the keys; CPU measured per block.
- [ ] Then each module in turn, measuring CPU after each (Elements and Rings are the heaviest).
- [ ] Linker map: does it fit the ~600 KB app slot (reported)? Plaits' and Braids' tables are the biggest.
- [ ] If CPU runs out: drop voices first; consider Rings' and Elements' own polyphony (one Part, up to 4 voices,
      instead of one Part per voice) to save memory and time.

## 4. The rest of the instrument (roadmap steps 6 to 8)

- [ ] Screen pages: port `web/src/2-screen.js`, `3-vis.js`, `4-pages.js`. Redraw only what changes.
- [ ] Controls: `web/src/5-controls.js` (encoder roles, buttons, keys, OCT, layers).
- [ ] Sequencer, banks and song (`5d-sequencer.js`): run it inside the audio callback, counted in samples.
- [ ] Scale, chords and the voicing zone (`5b-seq.js`).
- [ ] Storage: a binary patch format (about 1 KB with a pattern), 64 slots, atomic bank writes.
- [ ] USB-MIDI in and out, TRS in.

## 5. Release (roadmap step 9)

See the guide's Testing and Releasing sections: desktop regression (`node firmware/tests/run_tests.mjs`), audio
capture against the desktop render, clock jitter, power-loss save, a recovery drill, GPL-3.0 source with every build.
