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

# Second run: 3 November 2026 (lighter engine, two cores)

Raw data: [`fm1-bench-2026-11-03-b.csv`](fm1-bench-2026-11-03-b.csv) (116 cases; the reader page had received each
line twice, so the copy here keeps one of each). All OK: none silent, no NaN, FPU exact.

| | First run | Second run |
|---|---|---|
| Engine, no source | 32.7 % | **8.1 %** |
| VA VCF, 1 voice | 48.5 % | **21.1 %** |
| VA VCF, 6 voices, one core | 157.6 % | **95.0 %** |
| VA VCF, 6 voices, two cores | | **51.5 %** |
| Plaits, one voice (24 models) | 42-59 % | **15-32 %** (mean 23) |
| Braids, one voice (47 shapes) | 45-51 % | **11-16 %** (mean 13) |
| Rings MODAL, one voice | 102 % | **71 %** (its own share 58 %) |

- **Per voice:** about 6 % for the engine plus 15 % per VA voice on one core (5 voices under the 90 % line), and about
  7 % per voice on two cores (all 6 voices at 52 %). Two cores run 6 voices 1.84 times as fast as one.
- **Presets, 6 voices, two cores:** CH POLY STAB 59 %, ACID DRIFT 67 %, CH E-PIANO 76 %, STRING MACH 83 %,
  PD STRINGS 104 % (5 voices fit). On one core they were 92-171 %.
- **The limits now are the memory arena:** PD SYMPATH (88 %), PD SHIMMER (60 %) and LD SAW SOLO play one voice because
  their Rings or effects fill the 308 KB arena, not because of the CPU; so the second core has nothing to share there.
- FM1 Rack now starts poly presets with their own voices (up to 6) on two cores, 4 on one.

# Third run: 4 November 2026 (the engine at -Os)

Raw data: [`fm1-bench-2026-11-04-os.csv`](fm1-bench-2026-11-04-os.csv) (the reader page had received each line two or
three times; one of each kept). The same engine compiled for size (-Os) instead of speed (-O2), which frees about 36 KB
of flash for the menus. This build also has Rings' 16-bit strings. All 116 cases OK, FPU exact.

| | -O2 (second run) | -Os |
|---|---|---|
| Engine, no source | 8.1 % | 10.1 % |
| VA VCF, 1 voice | 21.1 % | 24.1 % |
| VA VCF, 6 voices, one core | 95.0 % | 109.3 % |
| VA VCF, 6 voices, two cores | 51.5 % | **59.1 %** |
| Plaits, one voice | 15-32 % | 18-39 % (+14 % on average) |
| Braids, one voice | 11-16 % | 16-21 % (+36 %) |
| Rings MODAL, one voice | 70.5 % | 72.2 % |
| 6-voice presets, two cores | 59-104 % | CH POLY STAB 64, ACID DRIFT 70, CH E-PIANO 84, STRING MACH 90, PD STRINGS 102 % |

About 15 % more CPU overall (Braids the most). On two cores six voices still fit for most presets; STRING MACH sits on
the 90 % line and PD STRINGS sheds to five voices, as before. **Decision: FM1 Rack uses -Os** (its image 541.6 KB of
the 568 KB app area, about 39 KB free for the next stages of the menus).

# Fourth run: 8 November 2026 (the engine at -O2 again, the new factory presets)

Raw data: [`fm1-bench-2026-11-08.csv`](fm1-bench-2026-11-08.csv) (one of each line kept). All cases OK, FPU exact. The
engine is back at -O2; the module cases are built on the same eight presets as before, then the 26 heaviest of the new
(Nov 2026) factory presets are played.

| | -Os (third run) | -O2 (this run) |
|---|---|---|
| Engine, no source | 10.1 % | 8.0 % |
| VA VCF, 6 voices, two cores | 59.1 % | **51.9 %** |
| Plaits, one voice | 18-39 % | 15-32 % (MODAL 32, ADDITIVE 30, PHASE D 28, CHORDS 27; CHIPTUNE 15) |
| Braids, one voice | 16-21 % | 11-17 % |
| Rings, its own share | | MODAL 58, SYMPATH 55, SYM QNT 55, STR+VERB 28, STRING 20, FM VOICE 15 % |
| Filter / Tides / Peaks / Grids | | 10.3 / **15.7** / 7.3 / 1.1 % |
| Delay / Reverb / Chorus | | 6.7 / 12.6 / 5.2 % |

Per voice (less the engine's 8 %): Plaits 7-24 %, Braids 3-9 %. Rings' resonators and Tides are the outliers.

**New presets over the firmware's 90 % line** (what this run found; the simulator can't): at one voice PD SYMPATHETIC
115 %, FX RESONATOR 99.6 %, FX SINGING BOWL 90 % (these would be muted as too heavy); with 6 voices on two cores
FX CHORD DRIFT 124, PD WARM STRINGS 125, PD ANALOG 122, PD ORGAN AIR 103, CH MARIMBA 101, CH SOFT PIANO 101,
PD SOFT SINE 97 % (these would shed voices).

**Changes after this run (8 Nov, not yet measured):**
- Presets: PD SYMPATHETIC became PD RESONANT (Rings STRING, not SYMPATH); FX RESONATOR uses Rings STRING (not MODAL);
  FX SINGING BOWL is Plaits MODAL alone (no Rings); PD WARM STRINGS and FX CHORD DRIFT lose the filter slot (STR MACH
  and CHORDS have their own tone controls); PD ANALOG, PD ORGAN AIR, PD SOFT SINE, CH MARIMBA, CH SOFT PIANO and
  FX SINGING BOWL start at 4 voices on the FM-1 (presets.json `fm1_voices`).
- Engine (-DRACK_FM1 only, so the page is unchanged): Tides runs once a block (only the block's last value was ever
  used), expected about 1 % instead of 16 %; Plaits renders a block in one call of 24 frames unless a note restarts
  in it (was two of 12).
- Not changed yet: fewer modes in Plaits MODAL and Rings MODAL (demos in the FM1-Rack folder for Josh to judge).
