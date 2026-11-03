# Porting FM1 Rack to the FM-1: what's left

In order. Don't start a step until the one before passes its check. The developer guide has the background.

## 0. Before anything touches the device

- [ ] Recovery kit: a forced-download dongle (FM-1 Transporter or similar), a full backup of your own FM-1 read
      out with it, and a restore you have actually tested.
- [ ] Official V15 firmware and M-VAVE's updater, to get back to stock.
- [ ] Only flash files you built yourself from a known commit. Nothing in this repo is a firmware file.

## 1. Build Felucca unchanged and flash it (roadmap step 1)

- [ ] Toolchain and SDK as in Felucca's BUILDING.md (JieLi pi32v2 clang 4.0.1, AC79 SDK `AC79NN_SDK_V1.2.1_2023-12-13`).
- [ ] Build Felucca as it is; flash it; restore stock; flash it again.
- [ ] Check Felucca's licence before forking (the guide's Releasing section).
- Check: Felucca runs, and you can always get back to stock.

## 2. Answer the open questions (measure on the board, with Felucca)

| Question | Why it matters | Where it goes |
|---|---|---|
| Free RAM for the app | Sets `RACK_ARENA_KB` and the page's FREE RAM setting | build flags, `web/src/1-data.js` RAM_OPTS |
| Codec sample rate, and can it run at 48 kHz? | The engine is 48 kHz; at 44,118 Hz everything is ~1.5 semitones flat | `hw/fm1_app.cc` (`fm1_app_init`) |
| Audio block size and the time budget per block | CPU limit | page CPU_OPTS, voice limits |
| Hardware floating point? Speed of `sinf`, `expf`, `tanhf`, `powf` | The engine is float throughout | may need lookup tables |
| Second core usable? | Voices on one core, effects on the other | engine split |
| Cycle counter readable? | CPU measurement | `fm1_hal_cycles()` |
| Screen size and controller | Page drawing | `fm1_hal_display_rect()` |
| Flash region for patches and its erase size | Storage | `fm1_hal_flash_*()` |
| Does clang 4.0.1 compile rack.cc and the Mutable code? | It's a 2017 compiler | fix language features if not |

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
