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

MIDI in: notes, pitch bend (±2 semitones), CC 120/123 (all off), CC 20-23 (macros). The keys also go out over
USB-MIDI. The voice modes play as on the page: MONO, LEGATO, and UNI 2 / UNI 4 (each key plays 2 or 4 detuned
voices). If a block of sound takes over 90 % of its time, the firmware drops a voice and shows how many it shed.

The screen shows the preset, its modules (red: asked for but no room in the memory arena), the macros, voices
playing and fitting, octave, the audio CPU load and dropouts, the output level and the keys held.

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

## Known limits, from the computer check

With the arena at 308 KB (what fm1-base's pool leaves), presets with both the delay and the reverb play one voice
(the two effects take 260 KB), and KARPLUS loses Rings to its delay. The page's FM-1 preview showed the same kind of
limit at 340 KB. Ways to more room, to decide after the benchmark: a shorter delay line, the reverb's buffers in
16-bit, or Rings' own polyphony.

## Build and install

As `fm1/bench` (its README), with this folder: `tools/build.py ../MODFM1/firmware/fm1/rack`, then
`python3 tools/fm1_install.py ../MODFM1/firmware/fm1/rack/build/fm1rack.fwsc`. Identity `FM-1_7800`
(`id_prefix` picked for this project, not registered anywhere).
