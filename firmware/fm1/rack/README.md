# FM1 Rack (Lite) firmware

**Status: builds and passes fm1-base's checks; its sound side passes the computer check; it has not run on an
FM-1.** Don't install it until `fm1/bench` has run on your FM-1 and its FPU check said OK: this firmware plays the
engine in the audio interrupt, and the benchmark is what tells us that's affordable.

## What it does

The 57 factory presets that use only Lite modules (Plaits, Braids, Rings, filter, Tides, Peaks, Grids, delay,
reverb, chorus), played from the keys (with velocity) and from USB or TRS MIDI.

Stages 1 to 3 of the menus (sound editing; save and the library; scales, chords, the arpeggiator, tunings and GLOBAL)
are in: the page's own pages, buttons and knobs (`menus.c`; the parameters come from the page's definitions through
`make_ui.py` -> `ui_defs.h`).

| Button (printed) | Page |
|---|---|
| HOME | home: the preset, scope, chain, macros. Press again: the RACK overview; again: home (and all notes off) |
| EDIT | the source (Plaits or Braids) |
| ENV | AMP (envelopes, voice mode), then the shaper (Rings or filter) |
| LFO | MOD A, MOD B, then the three LFOs (and each LFO's two direct targets) |
| FX | FX 1, then FX 2 |
| ARP | the 12-row matrix, then the arpeggiator |
| SEL | scale + chord, then the keys (QNT, harmony, zone, strum) |
| GLO | GLOBAL: settings, playing, tuning, MIDI, master, system |
| SAVE | the SAVE page: name, slot and category; SAVE again stores it (and it comes back at power-on). OCT+ the library |
| SEQ, PLAY, REC | not yet (stage 4): "NEXT STAGE OF THE PORT" |

| Knob | Home | A module's page, AMP, LFO | Matrix |
|---|---|---|---|
| PRESET | presets, factory then user (load when you stop turning) | the same | the same |
| ALGORITHM | category | the module picker (press the slot's button to load, HOME to close) | - |
| SELECT | voices, 1-6 | the page within | the row |
| K1-K4 | macros 1-4 (also MIDI CC 20-23) | the page's four parameters | source, destination, amount, via |
| OCT- / OCT+, MASTER | octave, volume | | |

**Save and the library** (`save.c`, after the page's SAVE): 64 user presets, 8 to a store object (A/B sectors,
torn-write safe, in the data area at 0xC8000-0xD7FFF). A user preset keeps the factory preset it started from (for
what the menus don't reach: master EQ, tuning), the six modules and their parameters, AMP, the LFOs, the 18 routes,
the macros and the tempo, a 10-character name and a category: 422 bytes.

| SAVE page | NAME + SLOT | LIBRARY (OCT+ from NAME) |
|---|---|---|
| SELECT | the letter (past the last: the library) | back to NAME (left) |
| ALGORITHM, K1 | change the letter (A-Z 0-9 - # .) | the row (PRESET too) |
| K2 / K3 / K4 | the slot / the category / the page of 16 | the tab (ALL USR BS PD LD CH FX OTH) / the action / - |
| SAVE | store it there (renaming: keep the new name) | back to NAME |
| OCT+ / OCT- | the library / - | do the action (LOAD, COPY, RENAME, DELETE: twice) / back |
| HOME | close | close |

**The wells move** (`vis.c`, after the page's visualisers): Plaits' wave by model (a field of dots for the noise models,
bursts for the drums), Braids' three stepped waves by bit depth and rate, Rings' ripples on each note, the filter's
response curve, Peaks' and Tides' outputs scrolling, Grids' three lanes with the playhead, the delay's repeats, the
reverb's cloud, the chorus' two voices, the LFOs' shapes, AMP's envelope and the voices' levels. The well redraws about
30 times a second, the rest ten. Messages show as the page's toast, a light box over the lower part of the screen.

![Save](screen-save.png) ![Library](screen-library.png)

A module's page shows the page's cells: label, value, a bar (ticks for a list), and in the slot's colour the value
modulation has moved it to. The well shows the output (AMP's first page: its envelope). Loading a module stops the
sound for a moment (the arena is laid out again) and says how many voices fit; an edit that needs a bigger buffer (a
longer delay, STR+VERB, SHIMMER) fits it again when the knobs rest ("MEMORY FITTED"). Edits are not kept when the
preset changes unless saved (SAVE). `sim_menus.txt`, `sim_save.txt`, `sim_vis.txt` and `sim_play*.txt` drive the pages in fm1-base's host simulator
(`tools/build.py ../MODFM1/firmware/fm1/rack --host`, then `build/host/fm1rack_host sim_menus.txt OUTDIR`).

![A module's page](screen-module.png) ![The matrix](screen-matrix.png)

**After the benchmark (3 Nov 2026):** one FM-1 core played about 2 voices (`../../results/README.md`). The
second core now renders half the voices (the screen shows 2 CORES; 1 CORE* if fm1-base gave it up), and poly
presets start with their own voices (up to 6) on two cores, 4 on one, unless SAVE remembered another number (SELECT asks for up to 6;
the firmware sheds what doesn't fit). A preset that
overloads the core even at one voice (Rings' MODAL measured 102 %) is muted until you pick another, with
**TOO HEAVY FOR FM-1** on the screen, so the screen and controls keep working.

MIDI in (on GLOBAL > MIDI CH, or all): notes (through SEL and the arp, as the keys), pitch bend (± GLOBAL > BEND),
sustain (CC 64), program change (GLOBAL > PROG CHG: factory presets, then user), CC 120/123 (all off), CC 20-23
(macros), clock (GLOBAL > CLOCK MIDI: the arp's steps and the tempo). MIDI out over USB (GLOBAL > MIDI OUT, off by
default as on the page): the notes played (chords and arp included; MIDI in isn't echoed), the clock on the internal
tempo, and with +CC the macros as CC 21-24. The voice modes play as on the page: MONO, LEGATO, and UNI 2 / UNI 4 (each key plays 2 or 4 detuned
voices). If a block of sound takes over 90 % of its time, the firmware drops a voice and shows how many it shed.

![FM1 Rack's screen, from fm1-base's host simulator](screen.png)

The screen takes the page's look (`web/src/2-screen.js`): its colours and 12 x 12 module icons (`icons.h`, converted
from the page's table). A header with the source's icon, the category in a coloured tab and the preset; a well with
the output's scope over the page's dot grid; the chain as icon tiles (orange source, teal shaper, purple modulators,
blue effects; red: asked for but no room in the arena); the macros as cells with rounded bars; a footer with CPU and
RAM bars, a light per voice lit by its envelope, the cores and voice mode (SHED n when voices were dropped, HEAVY when
muted), the octave (green while keys are held) and the output meter.

**Stage 3: playing** (`play.c`, `vis3.c`, after the page's 5b-seq.js, 5c-tools.js and 6-engine.js). SEL, GLO and ARP
are settings, not part of a preset: they stay when the preset changes and are saved on their own about 3 seconds
after the last change (the settings record grew: stage 2's preset, octave and voices carry over).

- **SEL**: SCALE (9), ROOT, CHORD (OFF, TRIAD, 7TH, 9TH, 11TH, SUS4, 6TH, POWER: the scale's own chord on each key in
  KEY harmony, a chromatic one in FREE), VOICING (CLOSE, LEAD: the inversion nearest the last chord, OPEN, INV1, INV2,
  +OCT); then QNT (SNAP down to the scale, ALL up to it, WHITE: the scale on the white keys, C4 the root), HARMONY,
  ZONE (with chords on, the seven lowest keys F3-B3 add 6, m7, M7, 9 or force DIM, MIN, MAJ; MIN + MAJ: sus) and
  STRUM (up to 80 ms between notes). The well: two octaves lighting the chord, its name (Cmaj7, Am7/G...), the scale;
  page 2 maps the FM-1's 27 keys to the notes they play. The keys in the scale glow on the keybed; SEL stays half lit
  while chords or QNT are on.
- **ARP**: MODE (UP, DOWN, UPDN, DNUP, RANDOM, ORDER, CHORD, REPEAT, UP+8, CONV, DIVG, PINKY, THUMB, TURING), RATE
  (1/4 to 1/32 with triplets), OCTAVE (1-4), GATE; HOLD (latch), MUTATE and LOOP (the Turing register), SWING. On the
  internal tempo or MIDI clock. The well: the pattern as bars by pitch, the step playing, the register's bits.
- **GLOBAL**: TEMPO, MIDI CH, VOICES (AUTO or 1-6: the same as SELECT on home), CLOCK; VEL CURVE, ANIMATION (the wells:
  full, calm, still), LED BRIGHT (the unlit buttons' glow); TUNING (13: equal, just, Pythagorean, meantone, Werckmeister
  III, Kirnberger III, rast, bayati, slendro, pelog, 19-, 24-, 31-EDO), T ROOT, A4 HZ, BEND; PROG CHG, MIDI OUT, OUT CH,
  LOCAL; LOW / MID / HIGH EQ and COMP (the master, over every preset); SYSTEM (measured: CPU history, arena, voices,
  cores). Each page's well draws it: a metronome, the velocity curve, each degree's cents against equal temperament,
  MIDI traffic, the EQ curve, the CPU graph.
- Home shows small tabs over the scope when SEL, the arp or a tuning is on.

How: the main loop pushes keys through rack_core's queue; the audio interrupt turns them into notes (`play.c`), counts
the notes sounding (two keys may share one), runs the strums and the arp by the codec's frame and applies what falls
due at the start of each block (5.8 ms). Checked in the simulator (`sim_play.txt`, `sim_play2.txt`): C5 plays C-E-G
with TRIAD, C-Eb-G with the zone's MIN key held, a strum spaces C4-E4-G4 by 35 ms, the arp steps, the settings come
back after a reboot. Not yet heard on an FM-1.

![SEL](screen-sel.png) ![ARP](screen-arp.png) ![Tuning](screen-tuning.png)

Flash: 515,824 of 581,632 bytes (64 KB free) after stage 3, which cost 18.3 KB; the two size changes from the report
(unused tables dropped with `-fdata-sections`, Tides built with only the modes FM1 Rack uses: 67 KB) made the room.

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
