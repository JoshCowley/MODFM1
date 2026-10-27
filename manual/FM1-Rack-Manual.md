# FM1 Rack 1.1 Manual

Oct 26, 2026 · Josh Cowley

*Text copy of the live manual: https://claude.ai/artifact/8cELcnQ8vCjJQCCqp3HngM*

FM1 Rack turns the M-VAVE FM-1 into a small modular synth: you slot Mutable Instruments modules into a fixed voice chain, patch them with a 12-row matrix, and play them through four macros. This manual covers the 1.1 browser build, which runs the original module code.

## Quick start

You can go from switch-on to a saved patch of your own in five steps.

1. **Switch on.** Open the [FM1 Rack browser build](https://claude.ai/artifact/9mWJtyZhwxNKFmqPwhjQ3t) and tap **Switch on** on the screen. The FM1 RACK logo plays, then the HOME page opens on the factory patch GLASS BELLS.
2. **Play.** Tap the keys; tapping lower on a key plays louder. On a computer, the bottom letter row (Z to /) plays the lower keys and Q to O plays the top octave.
3. **Shape the sound.** On HOME, Knobs 1–4 are the four macros. Each macro dial shows where it is routed underneath.
4. **Look inside.** Press **EDIT** to see the source module moving, **ENV** twice for the shaper, **LFO** for modulation, **FX** for effects. Turn **ALGORITHM** to browse other modules for that slot, then press the same button again to load one.
5. **Keep it.** Press **SAVE**, spell a name with **SELECT** and **Knob 1**, choose a user slot with **Knob 2**, and press **SAVE** again.

Turn **PRESETS** at any time to step through the 12 factory patches and your own.

## The panel

FM1 Rack uses the FM-1 exactly as printed: four encoders on the left, the screen, four knobs and twelve buttons on the right, and two rows of keys. Coloured buttons open a slot; the colour matches that slot's pages on screen.

*Diagram: the FM-1 panel map (8 encoders, 14 buttons, 27 keys). It's interactive in the live manual.*

| Control | Turn or tap | Press again, or hold |
| --- | --- | --- |
| MASTER | Output volume | — |
| SELECT | Next or previous page of the current slot; matrix row; step cursor on SEQ; letter position when saving. On HOME it flips to the rack view | — |
| PRESETS | Step through the 12 factory patches, then your user patches | — |
| ALGORITHM | Open the module list for the slot on screen and browse it. On SEQ it transposes the pattern; on STEP, turning left clears the step | Press that slot's button to load the highlighted module |
| KNOB 1–4 | The four values on screen. On HOME, the four macros | Hold HOME first to assign the knob to a macro (macro learn) |
| OCT − / OCT + | Shift the keys down or up an octave (±3). The button lights when shifted | — |
| FX | FX 1 | Again for FX 2 |
| SCL | CHORD page: scale chords. SELECT to page 2 for scale lock | Lit green while chords or scale lock are on |
| ENV | Amp: ADSR, voice and mod envelope pages (SELECT pages) | Again for the shaper slot (SHP) |
| LFO | MOD A | Again for MOD B, again for LFO 1–3 |
| EDIT | Source slot | — |
| GLO | Global settings | — |
| HOME | Perform page (macros and scope) | Again for the rack overview. Hold for macro learn |
| SAVE | Open the naming screen | Again to store |
| ARP | Mod matrix | — |
| SEQ | Sequencer: the 16-step grid and pattern settings | Again for STEP (the step under the cursor). Hold to lock knob values to that step |
| PLAY | Hold: keys latch on and off | Again to release all |
| REC | Arm the phrase recorder. On the SEQ pages: record live into the running pattern | Again to close the loop, a third time to clear it |
| Keys | Play. Tap lower on a key for more velocity | — |

## The screen

Every slot page has the same four parts, so once you can read one module you can read them all.

![The FILTER page with its four parts numbered](img/anatomy.png)

1. **Header.** The module's pixel icon, the slot tag in the slot's colour (SRC, SHP, MOD A, MOD B, FX 1, FX 2), the module name, and a dot for each page. The bright dot is the page you're on; turn **SELECT** to move between pages.
2. **Live band.** An animation of what the module is doing right now, driven by the engine: waveforms for sources, ripples for Rings, a frequency curve for the filter, grains for Clouds, rolling marbles, the Grids drum map with its playhead. Each module's band is pictured in the Modules section.
3. **Cells.** One cell per knob. The big number is the value you set. When the matrix or a macro is moving that parameter, a bright marker on the bar shows where it really is, and the small number on the right gives its live value.
4. **Footer.** CPU and RAM use, one light per voice (it glows with that voice's envelope), then the phrase recorder (a ring that fills as the loop plays), the sequencer's steps while it runs, or the last note played, and a stereo output meter.

A white message box slides up for a moment after loading a module, changing patch or saving. On HOME the band is an output scope instead, and the cells are the four macro dials.

![Every page, labelled with the button that opens it](img/g-pages.png)

*This picture shows 1.0. In 1.1, SCL opens CHORD, SEQ opens the sequencer, and the shaper moved under ENV.*

## Signal chain

Each note runs through its own SRC → SHP → AMP chain. The voices are mixed, then pass through FX 1 and FX 2 to the output. Two MOD slots and the built-in sources feed the matrix, which can reach any parameter in the chain.

*Diagram: the FM1 Rack signal chain (6 slots, 1 matrix). It's interactive in the live manual.*

The dashed line is the matrix reaching back into every slot. Every slot except SRC can be empty. The gate bus decides when Peaks, Tides and Marbles fire: on each note, on Grids hits, or on a division of the GLOBAL tempo.

## Pages

There are eleven kinds of page. Every one is reached from a single button, and the button lights while you're on it.

### HOME: perform

The page FM1 Rack opens on. The top strip is a live output scope. Below it are the four macros M1–M4 as dials; under each dial is the first destination it drives, with “+2” if it drives more. The dial glows when it moves. Along the bottom, seven small meters show MOD A, MOD B and LFO 1–3 at work.

Turn **Knob 1–4** to play the macros. The last macro you touched is the one macro learn will assign (see Modulation).

### HOME again: rack

Press **HOME** a second time (or turn **SELECT** on HOME) for the whole rack at a glance: each slot as a tile with its module's icon, dots travelling along the audio path, a pulsing speaker at the output, and the MOD slots and matrix underneath with their route count. The AMP tile shows how many voices the patch has.

### Slot pages

**EDIT** (source), **ENV** pressed twice (shaper), **LFO** (MOD A, MOD B) and **FX** (FX 1, FX 2) open slot pages. Each has the four parts described in The screen. Modules with more than four parameters have more pages; turn **SELECT** to move through them.

**Swapping a module:** on a slot page, turn **ALGORITHM**. A list opens with every module that fits that slot, its icon, and how many voices you'd have with it loaded. Modules that won't fit show why (“✕ CPU” or the RAM they need). Turn to highlight one; the bottom line describes it. Press the slot's own button to load it. Turning any other control closes the list without changing anything, so you can browse safely while playing.

### ENV: amp, then the shaper

Three pages for what every voice shares:

- **ADSR:** attack, decay, sustain, release. The band draws the envelope, and each sounding voice rides it as a dot.
- **VOICE:** VEL AMT (how much velocity changes level), GLIDE (portamento, mono patches only), SPREAD (stereo spread of the voices), LEVEL (patch volume; 50 is unity, 100 is +12 dB). The band shows six voice meters with the notes they're playing.
- **MOD ENV:** M ATTACK, M DECAY, M LOOP. A second, attack–decay envelope per voice for the matrix. With M LOOP on, it cycles while the key is held.

Press **ENV** a second time for the SHP slot (Rings, Warps or Filter, or empty), and again to come back to AMP. Turn **ALGORITHM** there to swap the shaper and press **ENV** to load it.

### LFO: LFO 1–3

Press **LFO** a third time. Three free LFOs for the matrix, one page each (SELECT). The band shows all three: the shape on the left and the live trace on the right, with the selected LFO brighter.

| Parameter | What it does |
| --- | --- |
| SHAPE | SINE, TRI, SAW, SQUARE, S&H (stepped random) or DRIFT (smooth random) |
| RATE | 0.03 to 30 Hz when SYNC is FREE |
| SYNC | FREE, or locked to 1/16, 1/8, 1/4, 1/2, 1 BAR or 2 BARS of the GLOBAL tempo |
| RETRIG | ON restarts the cycle when you play after all keys were up |

### ARP: matrix

Twelve rows, six on screen. **SELECT** picks a row; **Knob 1** source, **Knob 2** destination, **Knob 3** amount (−100 to +100), **Knob 4** via. A small meter on each row shows the route's live output; dots travel along active rows. A destination shown in red belongs to a module that's no longer loaded: the row sleeps until that module comes back.

### SCL: chords

Page 1: CHORD, INVERT, SPREAD, STRUM. Page 2: LOCK, KEY, SCALE (scale lock). The band is a two-octave keyboard: scale notes are tinted, the chord you play lights up note by note, and its name appears beside it with a ring that pulses on each new chord. See **Chords and scale lock** under Playing.

### SEQ: sequencer

The first press shows the pattern: a 16-step grid with a gliding playhead, and RUN, LENGTH, RATE and SWING on the knobs. Press **SEQ** again for STEP, which edits the step under the cursor. See **Step sequencer** under Playing.

### GLO: global

Settings that belong to the instrument, not the patch; they're remembered between visits. See MIDI and GLOBAL settings for each one. The band shows a four-beat metronome, the tempo, and MIDI status.

### SAVE

See Patches.

## Modules

Fourteen modules fill the six slots. Ten are Mutable Instruments designs running Emilie Gillet's original code; FILTER, DELAY, REVERB and CHORUS are FM1 Rack's own. Values run 0–100 unless a range is given; ± values run −100 to +100.

### Sources (SRC, press EDIT)

![Plaits, Braids and Elements on screen](img/g-sources.png)

#### Plaits

The macro oscillator: 24 models from analog shapes to speech and drums, each played with three macro controls. **Animation:** the waveform, reshaped by HARMO, TIMBRE and MORPH; noise models show a drifting dust field, drum models a burst per note.

| Parameter | What it does |
| --- | --- |
| MODEL | One of 24 models (below) |
| HARMO | Harmonics: the model's main timbral axis, such as detune, chord type or FM ratio |
| TIMBRE | Brightness or the model's second axis |
| MORPH | The model's third axis, such as waveform blend or decay |
| LPG | OFF: the AMP envelope shapes every note. ON: Plaits' low-pass gate plucks each note, shaped by DECAY and COLOUR |
| DECAY | Length of the low-pass gate pluck (LPG ON) |
| COLOUR | From a plain level fade to a closing low-pass filter (LPG ON) |
| LEVEL | Output level of the source |

| Models | Character |
| --- | --- |
| VA VCF, PAIR VA | Analog-style oscillators, filtered or as a pair of classic waveforms |
| PHASE D, WAVESHP, TERRAIN | Phase distortion, waveshaping and wave-terrain oscillators |
| 6OP FM 1–3, 2OP FM | Six-operator FM in three banks of patches, and simple two-operator FM |
| ADDITIVE, WAVETBL, FORMANT | Harmonic, wavetable and granular formant oscillators |
| CHORDS, STR MACH, CHIPTUNE | Chords, string-machine ensemble and chip-style voices |
| SPEECH | Vowels, formants and words |
| SWARM, NOISE, PARTICLE | A swarm of detuned voices, filtered noise and dust |
| STRING, MODAL | Plucked string and struck modal resonator |
| BASS DR, SNARE, HI-HAT | Analog-style drum voices |

#### Braids

The older macro oscillator: 47 shapes from classic saws to physical models, with a bit crusher. Cheap on CPU, so it allows the most voices. **Animation:** three braided strands; lower BITS steps them, lower RATE turns them into stairs.

| Parameter | What it does |
| --- | --- |
| MODEL | 47 shapes: analog (CSAW to //uu), digital filters (ZLPF–ZHPF), formants (VOSM, VOWL, VFOF), additive and FM (HARM, FM, FBFM, WTFM), physical models (PLUK, BOWD, BLOW, FLUT, BELL, DRUM), percussion (KICK, CYMB, SNAR), wavetables (WTBL–WTx4), noise (NOIS–PRTC) and QPSK |
| TIMBRE | The shape's first parameter |
| COLOR | The shape's second parameter |
| DRIFT | Slow random pitch wander, for an analog feel |
| BITS | Bit depth, 2 to 16 |
| RATE | Sample rate, 4 kHz to 48 kHz |
| SIGN | Adds a soft, asymmetric drive |
| LEVEL | Output level of the source |

#### Elements

A full modal voice: a bow, a breath and a mallet exciting a resonating body. Rich and heavy: most patches get one voice. **Animation:** a vibrating string between two bridges; the bow, breath and mallet appear when used.

| Parameter | What it does |
| --- | --- |
| BOW | Bow level |
| BLOW | Breath level |
| STRIKE | Mallet level |
| CONTOUR | Shape of the exciter's envelope, from percussive to swelling |
| GEOMETRY | Shape of the resonator: string, bar, plate, bell |
| BRIGHT | Brightness of the resonator |
| DAMPING | How fast the body stops ringing |
| POSITION | Where on the body it's excited |
| FLOW | Turbulence of the breath |
| MALLET | Mallet character |
| BOW TMB | Roughness of the bow |
| SPACE | Stereo width, rising into reverb |

### Shapers (SHP, press ENV twice)

The source plays into the shaper, one copy per voice. The slot can be left empty.

![Rings, Warps and Filter on screen](img/g-shapers.png)

#### Rings

A resonator the source excites: strings, modal bodies and FM voices that ring on after the note. **Animation:** ripples spread from each note; more STRUCT adds rings, more DAMPING lets them travel further.

| Parameter | What it does |
| --- | --- |
| MODEL | MODAL, SYMPATH (sympathetic strings), STRING, FM VOICE, SYM QNT (quantised sympathetic strings), STR+VERB |
| STRUCT | Structure: the spread of the partials or the chord of the strings |
| BRIGHT | Brightness |
| DAMPING | Higher values ring longer |
| POSITION | Where the body is excited, from muted to hollow |
| EXCITE | How hard the source drives the resonator |
| MIX | Source alone (0) to resonator alone (100) |
| SPREAD | Stereo width, from Rings' odd and even outputs |

#### Warps

A cross-modulator: two signals combined by one sweeping algorithm, up to a vocoder. **Animation:** the carrier above, the modulator below, the result between them.

| Parameter | What it does |
| --- | --- |
| ALGO | Sweeps through Warps' algorithms: crossfade, folding, analog and digital ring modulation, XOR, comparator, and vocoder at the top |
| TIMBRE | The algorithm's parameter |
| CARRIER | SINE, TRI or SAW: the shape of Warps' own oscillator when MOD IN is OSC |
| MOD IN | OSC: Warps' oscillator plays the note and your source modulates it. NOISE: your source is the carrier and noise the modulator. USB IN: audio from a computer over USB, on the FM-1 only; in the browser the source passes through |
| CAR LVL | Level into the carrier input |
| MOD LVL | Level into the modulator input |
| MIX | Source alone (0) to Warps alone (100) |

#### Filter

A resonant state-variable filter with drive, opened by the AMP envelope. **Animation:** the filter's frequency curve with a moving spectrum underneath; the dotted gold line marks the cutoff, live.

| Parameter | What it does |
| --- | --- |
| TYPE | LP, BP or HP |
| CUTOFF | Cutoff frequency |
| RESO | Resonance |
| DRIVE | Saturation before the filter |
| ENV AMT | ± How far the AMP envelope moves the cutoff |
| KEY TRK | How far the cutoff follows the keyboard |

### Modulators (MOD A and MOD B, press LFO)

Modulators make no sound. Their outputs appear in the matrix as sources named after the slot: “A TIDES” is Tides in MOD A, “B PEAKS” is Peaks in MOD B. Their GATE or CLOCK setting picks a trigger from the gate bus: NOTE (any key), GRID (Grids hits), or 1/16, 1/8, 1/4, BAR of the GLOBAL tempo.

![Peaks, Tides, Marbles and Grids on screen](img/g-mod.png)

#### Peaks

An envelope or LFO. **Animation:** a scrolling trace of its output. Output: **PEAKS**.

| Parameter | What it does |
| --- | --- |
| FUNC | ENV (attack–decay, fired by GATE), LFO, or TAP (an LFO whose speed follows the time between gates) |
| K1 | ENV: attack. LFO: rate. TAP: shape |
| K2 | ENV: decay. LFO and TAP: shape |
| GATE | What fires or syncs it |

#### Tides

A slope generator from Tides 2: one-shot or looping shapes with a curve and a slope. **Animation:** one cycle of the shape on the left, the live output scrolling on the right. Outputs: **TIDES** (swings both ways) and **TIDE+** (positive only).

| Parameter | What it does |
| --- | --- |
| MODE | AD (rise and fall on each gate), LOOP (free-running), AR (rise, hold while the gate is high, fall) |
| FREQ | Speed |
| SHAPE | Curve, from exponential to logarithmic |
| SLOPE | ± Balance between the rise and the fall |
| SMOOTH | ± Below zero smooths the shape; above zero folds it |
| RANGE | LO (slow) or MID (faster) |
| GATE | What fires it in AD and AR |

#### Marbles

Random notes and gates from Marbles' T and X sections, with déjà vu loops. **Animation:** each new note drops a marble at its pitch and rolls it left, so you can watch the melody. Outputs: **MARB X** (a random voltage of 0–2 V; on PITCH at +100 each volt is one octave) and **MARB T** (a pulse on each random gate).

| Parameter | What it does |
| --- | --- |
| RATE | Speed of the random gates when CLOCK is FREE |
| SPREAD | How widely the notes are spread |
| BIAS | Leans the gates and notes one way |
| STEPS | Low values glide; high values snap the notes to C major |
| DEJA VU | 0 is fully random, 50 repeats the loop exactly, above 50 shuffles it |
| LENGTH | Loop length: 1 to 16 steps |
| JITTER | Timing randomness |
| CLOCK | FREE, or 1/16, 1/8, 1/4 of the GLOBAL tempo |

#### Grids

The topographic drum map: a library of rhythms you move through with X and Y. **Animation:** the three lanes (BD, SD, HH) across 32 steps with a moving playhead; dot size is each step's weight and lit dots are the ones that fire at the current FILL. Outputs: **GRID** (a short trigger on each hit) and **ACCENT** (accented hits). Its hits also drive the GRID gate.

| Parameter | What it does |
| --- | --- |
| X, Y | Position on the drum map |
| FILL | Density: how many steps fire |
| OUT | Which lane drives the outputs: BD, SD, HH or KIT (all three) |
| CHAOS | Random variation |

### Effects (FX 1 and FX 2, press FX)

The voices are mixed, then run through FX 1 and then FX 2. Either can be empty. Each effect can be loaded in only one of the two at a time; the module list marks the one already in use.

![Clouds, Delay, Reverb and Chorus on screen](img/g-fx.png)

#### Clouds

The granular texture processor: it records what you play and replays it as clouds of grains, stretched, pitched or frozen. **Animation:** each grain as a short bar, placed by POSITION, lengthened by SIZE and drifting with PITCH; frozen grains turn white and stop.

| Parameter | What it does |
| --- | --- |
| POSITION | Where in the recent past the grains are taken from |
| SIZE | Grain length |
| PITCH | ± Transposition, up to two octaves either way |
| DENSITY | How many grains |
| TEXTURE | Grain envelope, from hard to soft and smeared |
| BLEND | Dry (0) to wet (100) |
| SPREAD | Stereo spread of the grains |
| FEEDBACK | Feeds the output back into the buffer |
| REVERB | Clouds' own reverb |
| MODE | GRANULR, STRETCH (time stretch and pitch shift), LOOPDLY (looping delay), SPECTRL (spectral) |
| FREEZE | ON holds the buffer: play over a frozen texture |

#### Delay

A stereo ping-pong delay locked to the GLOBAL tempo. **Animation:** the echo taps as bars, each lower by FEEDBACK; every note sends echoes bouncing between left and right.

| Parameter | What it does |
| --- | --- |
| TIME | 1/16, 1/8, dotted 1/8, 1/4 or 1/2 of the tempo |
| FEEDBACK | How many repeats |
| TONE | Darker to brighter repeats |
| MIX | Level of the echoes |

#### Reverb

The Clouds reverb as its own effect. **Animation:** sound bouncing round a room; the dots dim faster with more DAMP.

| Parameter | What it does |
| --- | --- |
| SIZE | Length of the tail |
| DAMP | Darkens the tail |
| DIFFUSE | From distinct echoes to a smooth wash |
| MIX | Level of the reverb |

#### Chorus

A stereo chorus. **Animation:** three waves drifting against each other.

| Parameter | What it does |
| --- | --- |
| RATE | Speed of the sweep |
| DEPTH | How far it sweeps |
| WIDTH | Stereo width |
| MIX | Level of the chorus |

## Modulation

Everything that moves a sound goes through one 12-row matrix on the **ARP** page. Each row reads: *source* × *amount*, scaled by *via*, added to a *destination*. Rows that share a destination add together. The matrix runs about 2,000 times a second.

### Sources

| Source | Range | Notes |
| --- | --- | --- |
| M1–M4 | 0 to 1 | The four macros: Knob 1–4 on HOME, or MIDI CC |
| VELO | 0 to 1 | Per voice: how hard the note was played |
| KEY TRK | −1 to +1 | Per voice: note pitch, centred on C4, two octaves to full scale |
| RANDOM | −1 to +1 | Per voice: a new random value for each note |
| AMP ENV | 0 to 1 | Per voice: the ADSR |
| MOD ENV | 0 to 1 | Per voice: the attack–decay envelope on ENV page 3 |
| LFO 1–3 | −1 to +1 | The three built-in LFOs |
| A … / B … | depends | Outputs of the module in MOD A or MOD B (see Modulators) |

### Destinations

Every parameter that moves continuously can be a destination. They are named by slot and label, such as SRC.TIMBRE, SHP.CUTOFF or FX 1.BLEND, plus AMP.ATTACK to AMP.M DECAY, the LFO rates, **PITCH** (+100 = one octave up) and **PAN**. Switches and model choices (MODEL, LPG, MODE, FREEZE and so on) can't be modulated.

The list follows the rack: load a different module and its parameters appear. A row whose destination belongs to a module you've removed turns red and sleeps; it wakes again if that module returns.

### Amount and via

AMOUNT runs from −100 to +100; at 100 a full-scale source sweeps the whole range of the destination. VIA multiplies the row by a second source: route LFO 1 to PITCH at a small amount with VIA set to M1, and Knob 1 on HOME becomes the vibrato depth. Leave VIA at — for a plain route.

### Macro learn

The fastest way to build a patch. Hold **HOME** for half a second (or Shift-click it): a gold LEARN bar appears, naming the last macro you touched. Go to any slot, ENV or LFO page and turn a knob: instead of changing the parameter, you set how far that macro moves it. A matrix row is created the first time. Turn more knobs to give the same macro more targets. Tap **HOME** to finish. To learn for a different macro, nudge its knob on HOME first.

### Gate bus

Peaks, Tides and Marbles need something to fire them. Their GATE or CLOCK parameter chooses from the gate bus: **NOTE** (high while any key is held), **GRID** (each Grids hit), or **1/16**, **1/8**, **1/4** and **BAR**, which tick at the GLOBAL tempo.

## Playing

### Keys, velocity and octave

The FM-1 has 27 keys, F to G across two rows. In the browser, how low you tap a key sets its velocity: near the top is soft, near the bottom is loud. **VEL CURVE** on the GLO page makes the response softer or harder. **OCT −** and **OCT +** shift the keys by up to three octaves either way; the button stays lit while shifted.

How many notes play at once depends on the patch (see Voices and the budget). With one voice, playing legato glides from note to note at the AMP **GLIDE** setting.

### Hold

**PLAY** turns hold on (it lights green). Each key then latches: tap once to start a note, again to stop it. Press **PLAY** again to switch hold off and release everything. Held notes keep playing while you change pages and turn knobs, which is the easiest way to sound-design with both hands free.

### Chords and scale lock

**SCL** opens CHORD. Turn **CHORD** (Knob 1) away from OFF and every key plays a chord built from the KEY and SCALE on page 2. The key you press moves to the nearest scale note and becomes the root, and the other notes are stacked from the scale, so the chords always fit the key: in C major, C plays C, D plays Dm, B plays Bdim.

| Setting | What it does |
| --- | --- |
| CHORD | OFF (single notes), TRIAD, 7TH, SUS2, SUS4, ADD9, 6TH, 9TH, POWER (root, fifth, octave) or QUARTAL (stacked fourths) |
| INVERT | ROOT, 1ST, 2ND or 3RD: lifts the lowest notes up an octave |
| SPREAD | Above 33 the second-highest note drops an octave; above 66 the top note also rises an octave, for wider voicings |
| STRUM | Plays the notes low to high, up to 80 ms apart |
| LOCK (page 2) | Scale lock: with it on, single notes also snap to the nearest scale note (ties go down). Chords follow the scale either way |
| KEY, SCALE (page 2) | C to B; major, minor, dorian, mixolydian, pentatonic major and minor, harmonic minor, whole tone |

Chords work from the keys, MIDI notes, the phrase recorder and the sequencer's step entry. The CHORD and scale settings stay with the instrument, not the patch, and are remembered between visits. The SCL lamp stays green while chords or scale lock are on. A chord needs a voice per note: if the patch has fewer, the CHORD page warns (for example “2 VOICES!”) and newer notes take over older voices.

### Phrase recorder

**REC** records what you play and loops it, so your hands are free to shape the sound.

1. Press **REC**. It blinks red: armed.
2. Play keys and turn knobs. Recording starts with the first key or knob move.
3. Press **REC** again. The loop closes, rounded to 1, 2, 4 or 8 bars of the GLOBAL tempo, and plays back straight away. REC turns green, and the footer shows the bar count with a ring that fills as the loop goes round.
4. Press **REC** a third time to stop and clear the loop.

The recorder captures keys, MIDI notes, macro moves and parameter changes on any page. Recording stops by itself after 16 seconds. Changing patch doesn't clear the loop, so you can audition one phrase through several patches.

### Step sequencer

Press **SEQ**. The grid shows 16 steps in two rows of eight: a pitch mark for each note (brighter is louder), a gold dot on steps with locks, a tail on tied steps and small dots for CHANCE. While it runs, the playing step flashes pink, a playhead glides along under the grid, and the keybed lights pink under the notes.

**Writing a pattern.** Move the cursor with **SELECT**. Play a key: the note goes into the cursor step and the cursor moves on. Hold several keys before letting go to write a chord into one step, or turn CHORD on and one key writes the whole chord. Turn SELECT past a step to leave it empty.

**Running it.** On SEQ, the knobs are **RUN** (STOP or PLAY; **Space** on a computer), **LENGTH** (1 to 16 steps), **RATE** (1/8, 1/16, 1/16T or 1/32 of the GLOBAL tempo) and **SWING** (delays every second step by up to half a step). Starting also restarts the gate bus, so Grids and the BAR gate line up with step 1. The pattern keeps running while you change pages and patches. MIDI Start and Stop run it too. **ALGORITHM** on SEQ transposes the whole pattern a semitone per click.

**Editing a step.** Press **SEQ** again for STEP, which edits the step under the cursor:

| Knob | What it does |
| --- | --- |
| TRIG | ON or OFF. OFF silences the step but keeps its notes |
| VELO | The step's velocity |
| LENGTH | 1/8, 1/4, 1/2, 3/4 or FULL of a step, or TIE: the note holds until the next step that plays, and tied steps with the same notes join into one long note |
| CHANCE | How often the step plays, from never to every time |

Turn **ALGORITHM** left on STEP to clear the step's locks, and again to clear its notes.

**Parameter locks.** Put the cursor on a step, then hold **SEQ** for half a second (or Shift-click it): a gold LOCK bar appears. Go to any slot, ENV or LFO page, or to HOME for the macros, and turn knobs. The values are stored for that step only, and the cell shows the locked value with a gold L. Press **SEQ** to finish. When that step plays, its locked values take over, and on the next step they go back to where the knobs are. A step with locks but no notes changes the sound without playing a note; it shows as a gold diamond.

**Live recording.** On SEQ or STEP, press **REC**. The sequencer starts if it was stopped, and a red dot blinks in the header. Notes you play land on the nearest step, replacing what was there. Knob turns on slot, ENV, LFO and HOME pages are recorded as locks on the step that is playing, so you can record a filter sweep step by step. Press **REC** again to stop recording; the pattern keeps playing. Away from the SEQ pages, REC is the phrase recorder as before.

**Saving.** The pattern is part of the patch: SAVE stores it, and loading a patch loads its pattern. ACID DRIFT and STRING MACH come with one.

## Patches

A patch is the whole rack: which module is in each slot, every parameter, the ENV and LFO pages, all 12 matrix rows, the four macro positions and the sequencer pattern with its locks. GLOBAL and CHORD settings stay with the instrument, not the patch.

### Factory patches

Twelve patches, each built to show off a pairing. Their levels were balanced by rendering them through the engine, so you can switch between them without reaching for MASTER.

| Patch | Chain | Macros M1 · M2 · M3 · M4 | Play it |
| --- | --- | --- | --- |
| GLASS BELLS | Plaits 2-op FM → Rings modal → Clouds. Tides drifts the strike point; Peaks adds a velocity-scaled morph | Timbre · Rings brightness · Clouds blend · Rings structure | Chords |
| ACID DRIFT | Braids saw → resonant low-pass → Delay → Chorus. Grids accents drive the filter; Tides fires on Grids hits | Cutoff · Resonance · Delay feedback · Timbre | Low notes |
| BOWED SPACE | Elements bowed → Reverb. Marbles wanders the bow position | Bow · Brightness · Reverb mix · Geometry | Long single notes |
| STRING MACH | Plaits string machine → Chorus → Reverb. LFO 1 and LFO 2 move timbre and morph | Timbre · Morph · Chorus depth · Reverb mix | Slow chords |
| SPEECH DUST | Plaits speech → Clouds. Marbles picks the words | Harmonics · Clouds density · Clouds blend · Marbles rate | Hold one note with PLAY |
| KARPLUS | Braids pluck → Rings string → Delay. Velocity brightens | Damping · Brightness · Delay feedback · Position | Picked notes, soft and hard |
| FOLDED BASS | Braids wavefolder → low-pass. The mod envelope sweeps the fold | Timbre · Color · Cutoff · Drive | Low notes; set GLO VOICES to 1 for glide |
| WARPED VOX | Plaits formant → Warps vocoder → Reverb. Tides moves the vowel | Warps algorithm · Warps timbre · Harmonics · Reverb mix | Chords |
| GRID GATE | Braids wavetable → low-pass → Delay → Reverb. Grids chops the level | Grids X · Grids Y · Grids fill · Timbre | Hold a chord with PLAY |
| FROZEN CLOUD | Plaits chords → Clouds stretch. LFO 2 drifts the position over two bars | Clouds size · Texture · Blend · Timbre | Chords, then FREEZE on FX 1 page 3 |
| TIDAL FM | Plaits 6-op FM → low-pass → Chorus → Delay. Tides sweeps the cutoff per note; Peaks pans | Harmonics · Morph · Cutoff · Delay feedback | Chords |
| INIT | Plaits virtual analog, nothing else | Timbre · Morph | A blank start |

ACID DRIFT (a 16th-note acid line with cutoff and resonance locks) and STRING MACH (four slow chords) also come with a sequencer pattern: load one, press **SEQ** and turn Knob 1 to PLAY.

### Saving your own

1. Press **SAVE**. The naming screen shows the patch's current name, a row of letter tiles and the 16 user slots.
2. Turn **SELECT** to move between letters, and **Knob 1** or **ALGORITHM** to change the letter under the cursor (space, A–Z, 0–9, −, #, .).
3. Turn **Knob 2** to pick a user slot. It starts on the first empty one; a filled slot shows “REPLACES” and the name you'd overwrite.
4. Press **SAVE** again to store, or **HOME** to cancel.

User patches appear after the factory ones as U01 to U16 when you turn PRESETS. Factory patches are never overwritten: saving always writes to a user slot. In the browser, user patches and GLOBAL settings are kept by this browser on this device, the way the FM-1 keeps them in its flash. A private window forgets them when it closes.

## Voices and the budget

The FM-1's chip can only do so much arithmetic per second, so the rack keeps a running budget and tells you what each choice costs before you make it.

### How the sums work

The budget is **80 CPU units** and a **256K RAM pool**. Every module has an estimated CPU cost and RAM need. The CPU figures are estimates; they'll stay estimates until each module is measured on the FM-1's chip.

- **Per-voice modules** (SRC, SHP and the AMP's 1 unit) run once for every voice, so their cost is multiplied.
- **Shared modules** (MOD A, MOD B, FX 1, FX 2) run once, whatever the voice count.
- **Voices** = (80 − shared cost) ÷ per-voice cost, rounded down, capped at 6 (or at the GLOBAL **VOICES** setting).

| Module | CPU | RAM |  | Module | CPU | RAM |
| --- | --- | --- | --- | --- | --- | --- |
| Plaits | 9 per voice | 8K |  | Peaks | 1 | 1K |
| Braids | 4 per voice | 2K |  | Tides | 1 | 1K |
| Elements | 40 per voice | 64K |  | Marbles | 3 | 4K |
| Rings | 14 per voice | 40K |  | Grids | 1 | 2K |
| Warps | 10 per voice | 8K |  | Clouds | 22 | 118K |
| Filter | 2 per voice | 1K |  | Delay | 3 | 64K |
| AMP | 1 per voice | — |  | Reverb | 6 | 32K |
|  |  |  |  | Chorus | 2 | 4K |

**Worked example, GLASS BELLS.** Plaits 9 + Rings 14 + AMP 1 = 24 per voice. Tides 1 + Peaks 1 + Clouds 22 = 24 shared. (80 − 24) ÷ 24 = 2.3, so 2 voices, using 72 of 80 units and 168K of RAM.

### What the factory patches get

| Voices | Patches |
| --- | --- |
| 6 | ACID DRIFT, STRING MACH, FOLDED BASS, GRID GATE, TIDAL FM, INIT |
| 5 | SPEECH DUST, FROZEN CLOUD |
| 4 | KARPLUS |
| 3 | WARPED VOX |
| 2 | GLASS BELLS |
| 1 | BOWED SPACE (Elements is the heaviest module by far) |

### Where you see it

- **Footer, on every page:** a CPU bar and a RAM bar (they turn red above 72 units or 85% of the pool), six voice lamps (one for each voice you have, glowing with that voice's envelope) and the stereo output meter.
- **RACK page:** the AMP tile shows the voice count, for example “AMP · 2V”.
- **Module picker:** every candidate shows the voices you'd have after loading it, or ✕ with the reason it won't fit (“✕ CPU”, “✕ 118K RAM”). Modules that don't fit can't be loaded. Make room by emptying a slot or choosing a lighter module elsewhere.
- **AMP page 2 (VOICE):** a larger view of the voices and the note each one is playing.

### When you play more notes than there are voices

A new note takes a free voice first. If none are free it takes the voice that was released longest ago, and if every voice is still held it takes the oldest held note.

With one voice (Elements patches, or VOICES set to 1) the rack plays **mono legato**: a note played while another is held changes pitch without restarting the envelopes, and **GLIDE** on AMP page 2 slides between them. Glide only works in mono.

## GLOBAL settings and MIDI

Press **GLO** for the instrument-wide settings. They aren't part of any patch, and they're remembered between visits. The visual on page 1 flashes the beat (gold on the downbeat), shows the tempo and reports MIDI: how many inputs are connected, “NO MIDI INPUT”, or “MIDI BLOCKED HERE” if the browser refused access. The MIDI icon lights green while messages arrive.

### Page 1

| Setting | Range | What it does |
| --- | --- | --- |
| TEMPO | 60–180 BPM, default 112 | Drives the gate bus (GRID, 1/16, 1/8, 1/4, BAR), synced LFOs, Delay time, the Marbles clock and the phrase recorder's bar length |
| MIDI CH | OMNI, 1–16 | Which channel the rack listens to. OMNI takes every channel |
| VOICES | AUTO, 1–6 | AUTO gives as many voices as the budget allows, up to 6. A number caps it lower; 1 gives mono legato with glide |
| CLOCK | INT, MIDI | INT uses TEMPO. MIDI follows incoming MIDI clock |

### Page 2

| Setting | Range | What it does |
| --- | --- | --- |
| VEL CURVE | 0–100, default 50 | 50 is linear. Higher values make soft playing louder (a light touch); lower values need a firmer hit to get loud |
| ANIMATION | FULL, CALM, STILL | FULL redraws at about 30 frames a second. CALM slows the motion to under half speed at 20 frames. STILL freezes the moving graphics and updates values only. If your system asks for reduced motion, the rack starts on CALM |
| LED BRIGHT | 0–100 | Brightness of the button lamps |

### MIDI in

Plug in any class-compliant USB MIDI keyboard or controller. In the browser the rack asks for MIDI access when you switch it on; on the FM-1 it would arrive over the USB port.

| Message | Effect |
| --- | --- |
| Note on / off | Plays notes, with velocity through VEL CURVE and scale lock. MIDI notes light the matching on-screen keys |
| CC 1 (mod wheel) | Macro M1 |
| CC 21, 22, 23, 24 | Macros M1, M2, M3, M4 |
| CC 64 (sustain) | Holds notes until the pedal lifts |
| CC 123 | All notes off |
| Clock (with CLOCK = MIDI) | Tempo follows the incoming clock, measured over one beat and kept within 60–180 BPM. The visual shows “BPM EXT” once it has locked |
| Start | Restarts the gate bus from beat 1, so Grids and the BAR gate line up with your sequencer, and starts the step sequencer from step 1 if the pattern has steps. Stop stops it |

Moving a macro from MIDI also selects that macro on the PERFORM page, so you can see it move. MIDI out isn't part of 1.0.

## Computer keyboard and mouse

These apply to the browser build. On the FM-1 itself you'd use its keys and knobs.

### Notes

The three letter rows cover all 27 keys of the FM-1's keybed, F3 to G5, laid out like a piano. Computer keys play at a fixed medium velocity; tap the on-screen keys lower down to play louder.

| Row | Keys | Notes |
| --- | --- | --- |
| Bottom (white keys) | Z X C V B N M , . / | F3 G3 A3 B3 C4 D4 E4 F4 G4 A4 |
| Middle (black keys) | S D F · H J · L ; ' | F♯3 G♯3 A♯3 · C♯4 D♯4 · F♯4 G♯4 A♯4 |
| Top (carries on upward) | Q W E R T Y U I O | B4 C5 C♯5 D5 D♯5 E5 F5 F♯5 G5 |
| Number row | − = | Octave down, octave up (±3) |

The gaps in the middle row (G and K) sit where a piano has no black key.

**Space** starts and stops the step sequencer from any page.

### Knobs

- **Drag** up or down on a knob to turn it.
- **Scroll** over a knob with a mouse wheel or trackpad.
- **Tab** to a knob, then use the **arrow keys**: up or right turns clockwise, down or left anticlockwise. While a knob has keyboard focus the letter keys don't play notes; click anywhere outside the knobs to play again.

SELECT, PRESETS, ALGORITHM and Knobs 1–4 are endless encoders, as on the FM-1, so their pointer just spins. MASTER is a normal knob with a fixed range.

### Buttons

- **Click** a button to press it.
- **Hold HOME** for half a second, or **Shift-click HOME**, to start macro learn. Click HOME again to leave it.
- Ctrl, Alt and Cmd combinations are left alone, so browser shortcuts keep working.

## Troubleshooting

| What's happening | What to try |
| --- | --- |
| No sound after **Switch on** | Turn **MASTER** up (a toast shows the volume). Check the tab isn't muted and the computer's output. Watch the output meter at the bottom right of the screen: if it moves, the rack is playing and the problem is further down the line |
| The switch says “Couldn't start” | The browser couldn't load the audio engine. Reload the page and try again; use a current Chrome, Edge, Firefox or Safari |
| A patch is much quieter than the others | Check **LEVEL** on SRC page 2 and AMP page 2, and **VEL AMT** if you're playing softly. Factory patches are balanced so this usually means a level was turned down |
| Distortion | The output meter turns red near full scale. Lower **MASTER** or the patch LEVEL, or reduce feedback on Delay, Clouds or Rings |
| A module won't load | The picker says why: “✕ CPU” (not enough processing for even one voice), “✕ 64K RAM” (not enough memory left) or “✕ IN FX 1” (that effect is already in the other FX slot; each effect can only be loaded once). Empty a slot or swap in something lighter |
| Chords lose notes | The patch has fewer voices than notes. See the voice lamps in the footer and **Voices and the budget**; lighter modules give more voices |
| Notes keep sounding | **PLAY** (hold) may be on (its lamp is lit): press latched keys again to release them, or press PLAY to turn hold off. A phrase loop may be playing (REC lamp lit): press REC to stop and clear it. From MIDI, lift the sustain pedal or send CC 123 |
| Keys play the wrong notes | **SCL** chords or scale lock may be on (the SCL lamp stays lit green), or the octave is shifted (an OCT lamp is lit). A running sequence (the SEQ lamp stays lit) also plays notes: press Space or turn RUN to STOP |
| Computer keys don't play | A knob has keyboard focus. Click anywhere outside the knobs. Keys held with Ctrl, Alt or Cmd are ignored |
| “NO MIDI INPUT” | Connect the controller before switching on, or replug it; the count updates when devices appear |
| “MIDI BLOCKED HERE” | The browser, or the page the rack is shown in, refused MIDI access. Not every browser offers Web MIDI; a Chromium-based browser is the safest bet. The on-screen and computer keys still work |
| MIDI clock is ignored | Set GLOBAL **CLOCK** to MIDI. “BPM EXT” appears once a beat of clock has arrived. Tempos outside 60–180 BPM are clamped |
| Marbles shows X 0 at first | Normal. It needs a few clock ticks before its first random value |
| Grids or BAR gates seem out of time with a sequencer | Send MIDI Start; it restarts the gate bus at beat 1 |
| User patches have gone | They live in this browser's site storage. A private window, cleared site data or a different browser or device won't have them. If a save shows “SAVED FOR THIS VISIT ONLY”, the browser blocked storage |
| The graphics feel busy or the laptop runs warm | Set GLOBAL **ANIMATION** to CALM or STILL. The sound is unaffected |

## Hardware status, credits and licences

### Where 1.0 stands

This is the **browser build**. The sound engine is real: the same C++ module code that would run on the FM-1, compiled to WebAssembly and run in an AudioWorklet at 48 kHz in blocks of 24 samples. The screen, keys and knobs are a faithful stand-in for the FM-1's 240×240 display and controls, so the interface you learn here is the one the firmware would have.

It has **not yet been built for or run on the FM-1**. What's still to do:

- **Port the engine** onto the Felucca platform layer (audio driver, display, keybed, knobs, flash storage) as in the FM1 Modular plan.
- **Measure real costs.** The CPU units in **Voices and the budget** are estimates. They need re-measuring on the AC791N, which the FM1 Modular notes record running at 240 MHz with 578 KB of SRAM and a 1 MB flash image. Clouds and Elements are the modules most likely to need trimming.
- **Fit the flash image.** Plaits' wavetables and Braids' tables are the large items to check against the 1 MB image.
- **Move user patches** from browser storage to the FM-1's flash.

### Credits

- **Plaits, Braids, Elements, Rings, Warps, Clouds, Peaks, Tides 2, Marbles and Grids** are Émilie Gillet's original Mutable Instruments code from the open-source eurorack repository, with stmlib. The REVERB slot uses the reverb from Clouds and the FILTER uses stmlib's state-variable filter.
- **DELAY and CHORUS**, the gate bus, mod matrix, macros, voice allocation and the whole interface were written for this rack.
- **Felucca** is the open firmware platform the FM-1 port builds on, via FM1 Modular.
- The screen font is **DotGothic16** by Fontworks.
- Built with clang/LLVM, wasi-libc and libc++.

### Licences

| Part | Licence |
| --- | --- |
| Mutable Instruments STM32 module code and stmlib | MIT |
| Grids (an AVR project in the same repository) | GPL-3.0 |
| Felucca platform | GPL-3.0 |
| DotGothic16 | SIL Open Font License |
| LLVM libc++ | Apache 2.0 with LLVM exception |

Because Grids and Felucca are GPL-3.0, the rack as a whole is distributed under GPL-3.0, with its source. The source pack (engine, interface and build scripts) accompanies this manual.

### Names and trademarks

This project isn't affiliated with or endorsed by Mutable Instruments or M-VAVE. **“Mutable Instruments” is a registered trademark**, and the repository's guidelines for derivative works ask that the name not be used on derivative works and recommend against keeping the original module names. The module names are used in 1.0 so you can tell where each sound comes from. **Before any public release, the modules should be renamed on screen** (for example MACRO OSC for Plaits, RESONATOR for Rings) and the credit kept here, in the manual.
