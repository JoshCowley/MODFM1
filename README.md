# MODFM1 · FM1 Rack 1.9.1

A sound-design firmware design for the M-VAVE FM-1 pocket synth. You build a voice by slotting open-source eurorack modules into a fixed chain (source → shaper → amp, two modulators, two effects), patch them with a 12-row mod matrix, and play them through four macros, scale chords (SCL), a Clipper-style sequencer (SEQ, PLAY, REC: up to 64 steps, a piano roll, live and free-take recording, hold layers, parameter locks, ratchets, fills, spice and pattern generators), an arpeggiator with a Turing mode (ARP), modulation lanes, undo / morph / randomize tools, 13 tunings, a master EQ / compressor / limiter and full MIDI in and out over USB. 1.4 adds deeper editing pages on every menu: a filter with LP24, notch and peak types, its own ADSR, velocity, saturation styles and audio-rate FM; envelope delay, hold and curves; pitch envelope; LFO fade, phase, polarity and one-shot; the modules' hidden settings (Braids' AD envelope, Plaits' envelope amounts and AUX, Rings' internal exciter, Clouds' quality, Marbles' scales); and more delay, reverb and chorus controls. 1.5 adds granular controls to Clouds (tempo-synced grains, spray, pitch scatter with quantise, size scatter, scan and a wet-only tone) and lets the LFOs reach every effect parameter, switches included, with two direct targets per LFO. 1.6 reworks SEL (scale, root, chord and voicing on one page, Melodee-style QNT, and ChordStep's chord zone on the lowest keys) and adds Melodee-style pattern banks A-H, a 16-row song with transpose, jam-to-song, capture and quantize-on-take. 1.7 adds a library of 30 categorised presets (Bass, Pads, Lead, Chord, FX). 1.8 gives SAVE 64 named user slots (4 pages of 16, each with a category, stored compactly) and a second SAVE page that browses every preset by category, with load, copy, rename and delete. 1.9 adds an FM-1 hardware preview (GLOBAL > HARDWARE, on by default): each module's real memory size and measured CPU cost are checked against an assumed FM-1 budget, voices drop to what fits, patches that can't fit are flagged, and the delay is capped at 1 s in 16-bit. 1.9.1 adds `firmware/`: the engine made ready for FM-1 firmware (one memory arena, voices limited to what fits, 1 s 16-bit delay), built and tested on a computer, with the board interface and app glue; the page's FM-1 preview now matches that engine exactly. This repo holds the 1.9.1 browser build, which runs the original module code compiled to WebAssembly.

**Status:** plays in a browser. It has not yet been built for or run on the FM-1 hardware.

## Run it in your browser

The page loads its sound engine with `fetch`, so it has to be served over http. Double-clicking `index.html` won't start the engine.

1. Download the repo (**Code → Download ZIP**) and unzip it.
2. Open Terminal in the `web` folder and run `python3 -m http.server 8000`
3. Go to http://localhost:8000 in Chrome, Edge, Firefox or Safari and press **Switch on**.

Keys: `Z`–`/` and `Q`–`O` play notes, `-` and `=` shift the octave, `Space` starts and stops the sequencer. MIDI keyboards, controllers and DAWs work both ways in browsers that offer Web MIDI (Chrome, Edge).

## What's here

| Folder | Contents |
| --- | --- |
| `web/` | The page (`index.html`), the AudioWorklet (`worklet.js`), the engine (`rack.wasm`), the factory patches and the page sources in `src/`. Rebuild the page with `python3 build.py "<manual url>" --standalone` |
| `engine/` | The C++ rack engine (`rack.cc`) (the same source builds the browser engine and, with `-DRACK_FM1`, the firmware-ready one) and `build.sh`, which rebuilds `rack.wasm` from scratch with clang 18 for wasm32-wasi. Set `EURORACK` to a checkout of github.com/pichenettes/eurorack |
| `sound/` | The 12 factory patches and a Node harness that renders each one through `rack.wasm`: `node sound/harness.js` |
| `firmware/` | The engine made ready for FM-1 firmware (`-DRACK_FM1`), a desktop build, tests over all 42 factory presets, the board interface and app glue, and the porting checklist. Not flashable: see `firmware/README.md` |
| `manual/` | The full user manual in Markdown, with screen images |

## Credits

- Plaits, Braids, Elements, Rings, Warps, Clouds, Peaks, Tides 2, Marbles and Grids: Émilie Gillet's original Mutable Instruments code, with stmlib. This project isn't affiliated with or endorsed by Mutable Instruments or M-VAVE.
- The hardware port will build on the Felucca firmware platform.
- Many 1.2 and 1.3 workflows, including the sequencer, follow ideas from Clipper by isod89 (after SLOOP and Felucca), rebuilt here from its descriptions; no Clipper code is used.
- Screen font: DotGothic16 by Fontworks (SIL Open Font License), loaded from Google Fonts.

## Licence

GPL-3.0 (see `LICENSE`). The Mutable Instruments STM32 module code and stmlib are MIT; Grids and the Felucca platform are GPL-3.0, so the project as a whole is GPL-3.0.

"Mutable Instruments" is a registered trademark. The module names are kept on screen for now; renaming them is on the to-do list before any public release.


## Hosting on Cloudflare Pages

The `web/` folder is the whole site, already built: `index.html` (the synth), `worklet.js`, `rack.wasm`, and `manual/` (the manual as a web page). There is no build step.

**Connect the repo (once):**

1. In the Cloudflare dashboard, go to **Workers & Pages → Create → Pages → Connect to Git**.
2. Authorise GitHub and pick **JoshCowley/MODFM1** (a private repo is fine).
3. Settings: **Framework preset** None · **Build command** leave empty · **Build output directory** `web` · **Root directory** leave empty.
4. Save and deploy. Every push to `main` redeploys; other branches get preview URLs.

`wrangler.toml` holds the same settings (`pages_build_output_dir = "web"`), so you can also deploy from a computer with `npx wrangler pages deploy` or preview with `npx wrangler pages dev web`. `web/_headers` serves `rack.wasm` as `application/wasm` with a short cache, so updates reach players within the hour.

The page needs HTTPS for its audio engine (AudioWorklet) and Web MIDI, which Cloudflare provides. Web MIDI works in Chromium browsers (Chrome, Edge); Safari plays the synth without MIDI.

**Updating the site:** edit `web/src/`, then from `web/` run `python3 build.py "manual/" --standalone` to rebuild `index.html`, and push. `python3 build.py "manual/" --single` writes `fm1-rack-single.html`, one file with the engine embedded that opens by double-clicking.
