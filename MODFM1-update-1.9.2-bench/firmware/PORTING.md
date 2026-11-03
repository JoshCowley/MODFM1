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
- **Flash, measured again (November 2026).** With the FM-1's float flags (`-mcpu=r3 -mfprev1`, below) and only the
  sections actually linked counted (the first count included discarded ones and soft-float calls), the engine with
  every module is **961 KB**, and the Lite set (`-DRACK_LITE`: no Elements, Warps, Marbles or Clouds) **362 KB**:

  | Module | KB | Module | KB |
  |---|---|---|---|
  | Elements | 378.4 (363 of it tables) | Rings | 35.3 |
  | Plaits | 148.1 | Tides | 32.1 (tables) |
  | Marbles | 78.5 | Peaks | 13.8 |
  | Braids | 75.5 | C library | 9.9 to 12.6 |
  | Warps | 68.5 | stmlib, Grids | 4.3 to 5.3 |
  | Clouds | 65.5 | Engine (rack.cc) | 43.0 (Lite) to 46.7 |

  The benchmark firmware (`fm1/bench`: fm1-base's platform and fonts, the Lite engine, its own screen and 37 KB of
  preset data) links to **551 KB of the 568 KB app area**. So the Lite set fits with room for FM1 Rack's UI only if
  that UI stays small; whether Warps, Clouds or Marbles also fit is for after the UI is ported.

- **The FM-1 has a single-precision FPU, by the evidence so far** (this corrects the note that was here before).
  Felucca-Engines' plugin guide says "The FM-1's DSP has no floating point unit", and Felucca's own code is fixed
  point. But fm1-base, whose firmwares run on the FM-1 (CTL-1, Radio and others), compiles everything with
  `-mcpu=r3 -mfprev1 -ffp-contract=off`, which its build.py calls "the AC79's single-precision FPU (the SDK's own
  flags)", and its build check fails any firmware that links soft-double routines. With those flags the engine's float
  arithmetic compiles to hardware instructions; the only software float left is double precision, which FM1 Rack's
  engine no longer uses on the FM-1 (the clock is whole 16ths plus a float fraction under `RACK_FM1`), and `fabs` on a
  float (now a float overload in `hw/cxx/cmath`). **Not yet confirmed on a board:** `fm1/bench` checks a known
  single-precision result at power-on and times every module.
- **Audio, from Felucca's porting guide:** 44.1 kHz, 128-frame DMA halves (2.9 ms each). The engine runs at 48 kHz:
  render about 139 frames per half and resample, or move the engine to 44.1 kHz.
- **Screen:** 240 x 240 RGB565 (ST7789), the size the browser version already draws.
- **felucca-bsp** (MPL-2.0, Felucca's `firmware/bsp`) is a standalone board layer with an example app (`examples/hwtest`,
  tested on an FM-1), its own packer (`tools/fm1pkg.py`), installer and rescue script. An FM1 Rack firmware can be
  built on it directly instead of on Felucca's whole app. Its `tools/get_sdk.sh` fetches the three SDK files from
  JieLi's GitHub repository and checks their hashes.

### The route this points to

1. **Measure first: `fm1/bench`.** A test firmware on fm1-base (its update entry, boot guard and safe mode) that
   checks the FPU and times one voice of every Plaits model, Braids shape and Rings model, the filter, Tides, Peaks,
   Grids, the effects, 1 to 6 voices and ten presets. It builds and passes fm1-base's checks; it hasn't run on an
   FM-1 yet. Its README has the build and install steps.
2. **If the FPU is confirmed:** the float modules run as they are; the numbers decide how many voices, which Plaits
   models need limits, and whether Rings (the heaviest Lite module) can be polyphonic.
3. **If it isn't:** the earlier route stands: integer modules as they are (Braids, Peaks, Grids), a fixed-point
   rewrite of FM1 Rack's own engine, fixed-point ports of chosen float models one at a time, or Felucca plugins.
4. **Base:** fm1-base (MPL-2.0) is the better base than Felucca's whole app: its platform layer, build checks,
   packaging, installer and safe mode are already what FM1 Rack needs, and its C contract (`app_render`, `app_frame`)
   is what `fm1/bench` already uses.

## 2. Answer the open questions (measure on the board, with Felucca)

| Question | Why it matters | Where it goes |
|---|---|---|
| Free RAM for the app (Felucca's map gives POOL 352 KB; how much of it the screen needs) | Sets `RACK_ARENA_KB` and the page's FREE RAM setting | build flags, `web/src/1-data.js` RAM_OPTS |
| Can the codec run at 48 kHz? (Felucca runs it at 44.1 kHz) | The engine is 48 kHz: resample, or move the engine | `hw/fm1_app.cc` (`fm1_app_init`) |
| Audio block size and the time budget per block | CPU limit | page CPU_OPTS, voice limits |
| Does the core run the `fprev1` float instructions? (fm1-base says yes, Felucca says no FPU) | Float modules need it | `fm1/bench` |
| Second core usable? | Voices on one core, effects on the other | engine split |
| Cycle counter readable? (fm1-base has `plat_ticks24()`, 24 ticks a microsecond) | CPU measurement | `fm1_hal_cycles()` |
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
