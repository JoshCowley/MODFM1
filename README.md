# MODFM1 · FM1 Rack 1.0

A sound-design firmware design for the M-VAVE FM-1 pocket synth. You build a voice by slotting open-source eurorack modules into a fixed chain (source → shaper → amp, two modulators, two effects), patch them with a 12-row mod matrix, and play them through four macros. This repo holds the 1.0 browser build, which runs the original module code compiled to WebAssembly.

**Status:** plays in a browser. It has not yet been built for or run on the FM-1 hardware.

## Run it in your browser

The page loads its sound engine with `fetch`, so it has to be served over http. Double-clicking `index.html` won't start the engine.

1. Download the repo (**Code → Download ZIP**) and unzip it.
2. Open Terminal in the `web` folder and run `python3 -m http.server 8000`
3. Go to http://localhost:8000 in Chrome, Edge, Firefox or Safari and press **Switch on**.

Keys: `Z`–`/` and `Q`–`O` play notes, `-` and `=` shift the octave. MIDI keyboards work in browsers that offer Web MIDI.

## What's here

| Folder | Contents |
| --- | --- |
| `web/` | The page (`index.html`), the AudioWorklet (`worklet.js`), the engine (`rack.wasm`), the factory patches and the page sources in `src/`. Rebuild the page with `python3 build.py "<manual url>" --standalone` |
| `engine/` | The C++ rack engine (`rack.cc`) and `build.sh`, which rebuilds `rack.wasm` from scratch with clang 18 for wasm32-wasi. Set `EURORACK` to a checkout of github.com/pichenettes/eurorack |
| `sound/` | The 12 factory patches and a Node harness that renders each one through `rack.wasm`: `node sound/harness.js` |
| `manual/` | The full user manual in Markdown, with screen images |

## Credits

- Plaits, Braids, Elements, Rings, Warps, Clouds, Peaks, Tides 2, Marbles and Grids: Émilie Gillet's original Mutable Instruments code, with stmlib. This project isn't affiliated with or endorsed by Mutable Instruments or M-VAVE.
- The hardware port will build on the Felucca firmware platform.
- Screen font: DotGothic16 by Fontworks (SIL Open Font License), loaded from Google Fonts.

## Licence

GPL-3.0 (see `LICENSE`). The Mutable Instruments STM32 module code and stmlib are MIT; Grids and the Felucca platform are GPL-3.0, so the project as a whole is GPL-3.0.

"Mutable Instruments" is a registered trademark. The module names are kept on screen for now; renaming them is on the to-do list before any public release.
