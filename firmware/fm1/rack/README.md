# FM1 Rack (Lite) firmware: first stage

**Status: builds and passes fm1-base's checks; its sound side passes the computer check; it has not run on an
FM-1.** Don't install it until `fm1/bench` has run on your FM-1 and its FPU check said OK: this firmware plays the
engine in the audio interrupt, and the benchmark is what tells us that's affordable.

## What it does

The 57 factory presets that use only Lite modules (Plaits, Braids, Rings, filter, Tides, Peaks, Grids, delay,
reverb, chorus), played from the keys (with velocity) and from USB or TRS MIDI.

| Control | Does |
|---|---|
| PRESET | next / previous preset; it loads when you stop turning |
| ALGORITHM | next / previous category (INIT, BASS, LEAD, PAD, KEYS, FX, OTHER) |
| SELECT | voices, 1-6 (poly presets) |
| K1-K4 | macros 1-4 (also MIDI CC 20-23) |
| OCT- / OCT+ | octave, -3 to +3 |
| HOME | all notes off |
| SAVE | remember the preset, octave and voices for the next power-on |
| MASTER | volume |

**After the benchmark (3 Nov 2026):** one FM-1 core played about 2 voices (`../../results/README.md`). The
second core now renders half the voices (the screen shows 2 CORES; 1 CORE* if fm1-base gave it up), and poly
presets start with their own voices (up to 6) on two cores, 4 on one, unless SAVE remembered another number (SELECT asks for up to 6;
the firmware sheds what doesn't fit). A preset that
overloads the core even at one voice (Rings' MODAL measured 102 %) is muted until you pick another, with
**TOO HEAVY FOR FM-1** on the screen, so the screen and controls keep working.

MIDI in: notes, pitch bend (±2 semitones), CC 120/123 (all off), CC 20-23 (macros). The keys also go out over
USB-MIDI. The voice modes play as on the page: MONO, LEGATO, and UNI 2 / UNI 4 (each key plays 2 or 4 detuned
voices). If a block of sound takes over 90 % of its time, the firmware drops a voice and shows how many it shed.

![FM1 Rack's screen, from fm1-base's host simulator](screen.png)

The screen takes the page's look (`web/src/2-screen.js`): its colours and 12 x 12 module icons (`icons.h`, converted
from the page's table). A header with the source's icon, the category in a coloured tab and the preset; a well with
the output's scope over the page's dot grid; the chain as icon tiles (orange source, teal shaper, purple modulators,
blue effects; red: asked for but no room in the arena); the macros as cells with rounded bars; a footer with CPU and
RAM bars, a light per voice lit by its envelope, the cores and voice mode (SHED n when voices were dropped, HEAVY when
muted), the octave (green while keys are held) and the output meter.

Not here yet, still to port from the page: editing modules and parameters, the matrix, the sequencer, arpeggiator,
chords, tunings, user presets and the library.

## How it works

- `rack_core.c`: presets (INIT, then the preset's differences, then each module's parameter ranges, packed in
  `presets.h` by `make_presets.py`: 22 KB), a note queue from the main loop to the audio interrupt, the voice modes,
  the overload shedding, and the resampler: the engine renders 48 kHz, the codec plays 44.1 kHz, so the output is
  resampled (linear) each block. Loading a preset makes the audio interrupt play silence while the main loop loads.
- `app.c`: the screen and controls, on fm1-base (the template's patterns).
- `../common/rk.cc`, `../common/rt.cc`: the engine's C names, and the C++ runtime and maths fm1-base lacks (as in
  `fm1/bench`).

## Checked

`sh build_host.sh && build/host/rack_host` (needs `EURORACK`): every preset loaded as the firmware loads it sounds
the same as the preset's own calls, sample for sample; the resampler keeps the pitch and has no step at block
edges; every preset makes sound through the firmware's audio path; the unison modes play 2 and 4 notes per key.

fm1-base's build (with placeholder SDK files here, so its package was deleted): image 544 KB of the 568 KB app area,
no soft-double routines, pool within its headroom.

## Memory: the big buffers sized to the preset

The arena is 308 KB (what fm1-base's pool leaves). After a preset loads, `rk_fm1_fit` (rack.cc `fm1_fit`) sizes the
big buffers to what the preset can use and lays the arena out again, so the room left goes to voices:

- **the delay line** to the preset's delay time (plus TAPE's wow), not 1 s; the full second when a matrix row can
  move its time;
- **the reverb's pre-delay lines** to the preset's pre-delay (a power of two), not 170 ms; **the shimmer line** only
  when SHIMMER is reachable;
- **Rings' own reverb** (64 KB a voice) only when STR+VERB is reachable (the shim's `rings/dsp/fx/fx_engine.h`
  leaves a null buffer alone; render_voice keeps the model off STR+VERB then).

These four change nothing in the sound: with the same voices, every preset comes out sample for sample as before
(one, CH DUSTY KEYS, differs by 1e-5 at most: TAPE's wow reads a shorter line at a float position).

- **Rings' strings in 16-bit** (`-DRACK_RINGS16`, the shim's `rings/dsp/string.h`): 8 strings of float delay lines
  were 99.6 KB a voice, now 49.8 KB. This one changes the sound slightly: on the computer, against the float strings,
  the difference is 55-76 dB below the signal for the Rings presets, and their decays end at the same level (no
  hanging tails). Full scale is +/-4 (the strings peaked at 2.05).

Voices the arena now fits (computer check; the CPU may still shed some on the FM-1): LD RESO STRING and PD SYMPATH 1 to 3; KARPLUS gets its Rings
back (2 voices; before, its delay left no room and Rings was unloaded); PD SHIMMER 1 to 5; PD GLASS AIR and BS DUB
ROUND 1 to 4; PD DARK DRONE and FX TIDAL DRONE 1 to 3 (their delay is a full second); LD SAW SOLO, LD SYNC SCRM, LD CHIP
SQ, LD PWM SOLO, LD SQUARE CHIP, PD EVOLVING WT, CH PLUCK HARP, CH HOUSE STAB, CH STRING PLK, FX RISER, FX LASER ZAP, FX SAMPLEHOLD, FX NOISE SWEEP,
FX METAL RING, GRID GATE and TIDAL FM to 6.

The page's FM-1 preview (GLOBAL > HARDWARE) still uses the full sizes, so it now predicts fewer voices than the
firmware plays.

## Build and install

As `fm1/bench` (its README), with this folder: `tools/build.py ../MODFM1/firmware/fm1/rack`, then
`python3 tools/fm1_install.py ../MODFM1/firmware/fm1/rack/build/fm1rack.fwsc`. Identity `FM-1_7800`
(`id_prefix` picked for this project, not registered anywhere).
