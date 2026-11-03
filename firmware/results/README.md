# FM1 Bench results: 3 November 2026

The first run of `fm1/bench` on a real FM-1 (installed from M-VAVE's V15 firmware). Raw data:
[`fm1-bench-2026-11-03.csv`](fm1-bench-2026-11-03.csv). CPU % is one core's time for the case, at the engine's
48 kHz; 100 % is the whole core. The firmware's overload line is 90 %.

## Headline

- **FPU OK: hardware float, exact.** The FM-1 runs single-precision float in hardware, so the Mutable modules run
  as they are; no fixed-point rewrite needed.
- **All 105 cases OK:** none silent, no NaN.
- **One core plays about 2 voices.** The engine costs about **27 %** before any voice, then about **22 %** per Plaits
  voice (fit to 1, 2, 4, 6 voices of VA VCF: 48.5, 70.3, 113.9, 157.6 %). Two voices sit at 70 %; three would be
  about 92 %, over the line.

## Per module

| Module | One voice, whole case | Its own share |
|---|---|---|
| Engine, no source (1 voice) | 32.7 % | |
| Plaits, 24 models | 42.1-59.0 % (mean 49.6; MODAL heaviest, CHIPTUNE lightest) | |
| Braids, 47 shapes | 45.3-50.8 % (mean 47.3) | |
| Rings MODAL | 101.8 % | 54.9 % |
| Rings SYMPATH / SYM QNT | 92.2 % | 45.3 / 45.4 % |
| Rings STR+VERB | 69.6 % | 22.8 % |
| Rings STRING | 62.0 % | 15.2 % |
| Rings FM VOICE | 59.8 % | 13.0 % |
| Filter | | 6.5 % |
| Tides / Peaks / Grids | | 6.9 / 3.6 / 0.3 % |
| Delay / Reverb / Chorus | | 7.1 / 11.1 / 6.8 % |

## Presets (voices as the arena allowed them)

| Preset | CPU | Voices |
|---|---|---|
| PD STRINGS | 240.5 % | 6 |
| STRING MACH | 208.1 % | 6 |
| ACID DRIFT | 204.9 % | 6 |
| CH POLY STAB | 198.9 % | 6 |
| CH E-PIANO | 196.5 % | 6 |
| PD SYMPATH | 103.1 % | 1 |
| FX METAL RING | 99.0 % | 2 |
| PD SHIMMER | 81.2 % | 1 |
| LD SAW SOLO | 72.2 % | 1 |

## What this means for FM1 Rack Lite

1. **Voices:** about 2 per core for Plaits or Braids with light effects. The rack firmware now starts poly presets
   at 2 voices (SELECT still asks for more; the firmware sheds them if they don't fit).
2. **Too heavy at one voice:** Rings MODAL (102 %), and on the line SYMPATH / SYM QNT (92 %) and PD SYMPATH (103 %).
   The rack firmware now mutes a preset that overloads the core even at one voice, and says so on screen, instead of
   starving the screen and controls.
3. **The fixed 27 % is the biggest single cost.** What it is made of hasn't been measured yet. To measure next, with
   a variant of the benchmark: the master EQ / compressor / limiter, the LFOs and matrix, the 48 kHz rate (the codec
   plays 44.1 kHz, so the engine renders about 9 % more frames than it plays), and the 256-frame block.
4. **The second core** (fm1-base's `cpu2`: a worker the render hands jobs to) could take half the voices. Not tried.

## Changes after this run (3 Nov 2026, not yet measured on the FM-1)

- **A lighter engine, same sound.** Two hot spots found by counting instructions on a computer (callgrind):
  the limiter read all 48 look-ahead samples for every sample (now a running maximum: the same peak), and every
  parameter read scanned all 18 matrix rows (now skipped for parameters no row reaches). All 57 presets come out
  bit for bit the same as before. On the computer, the engine with no voice went from 175 to 26 million
  instructions for 2 s, and one Plaits voice from 225 to 71 million. How much of that the FM-1 sees isn't known
  until the benchmark runs again: its float maths and library calls cost differently.
- **The second core.** With 2 or more voices, the second core renders the upper half of them while the first core
  renders the rest (the first also runs the modulators, effects and master). fm1-base's `cpu2_fallback`: a unit whose
  second core misbehaves carries on with one core. Its stack (2.5 KB usable, trapped below that) held the deepest
  voice measured on the computer (1.3 KB, 64-bit) with room to spare.
- **The benchmark** now adds 11 **2 CORES** cases (VA VCF x2, x4, x6 and the 6-voice presets) when the second core
  starts, so the next run shows both changes against this one.
