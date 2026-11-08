# FM1 Rack: test kit (built 3 November 2026)

Two packages, built from the FM1 Rack sources with JieLi's toolchain and fm1-base (commit 2d8ed74). Every fm1-base
build check passed, the three JieLi SDK files matched fm1-base's reference hashes exactly, and both computer checks
("all passed") ran first. **Neither has run on an FM-1 yet: this kit is for that first test.**

| File | What | Identity |
|---|---|---|
| `packages/fm1bench.fwsc` | The benchmark: FPU check + every module timed, then 26 of the new factory presets (the heaviest), on one core and, for the 6-voice ones, two. Silent. **Install first.** Rebuilt 8 Nov with the current engine at -O2 | FM-1_7700 |
| `packages/fm1rack.fwsc` | FM1 Rack Lite, first stage: 57 presets (new set, Nov 2026), keys + MIDI, macros on K1-K4. Rebuilt 3 Nov after the benchmark: the second core renders half the voices (up to 6 to start on two cores, 4 on one); a lighter engine; the big buffers sized to each preset and Rings' strings in 16-bit (more voices for the delay, reverb and Rings presets); the page's graphics on the screen; menus stages 1-3 (module pages and picker, AMP, LFOs, matrix, rack; SAVE with 64 user presets and the library; SEL scales and chords, the arpeggiator, 13 tunings, GLOBAL with MIDI in and out options; OSC, a second oscillator per voice with a mixer; macro LEARN), stage 4 (CPU and RAM in a status strip at the top, sliders with a halo in the modulation's colour, LEARN on two EDIT taps, the matrix on LFO, the sequencer: 8 banks, GEN, SONG, MSEQ, step entry and REC), moving visualisers; the engine at -O2 again (8 Nov: speed over size), 2.9 KB of RAM freed; a preset too heavy even at 1 voice is muted with a message | FM-1_7800 |
| `packages/fm1bench-os.fwsc` | The benchmark with the engine compiled for size (-Os): to measure what the 36 KB of flash it saves costs in CPU | FM-1_7700 |

SHA-256 (also in `packages/SHA256SUMS`):

```
8f908266b984f93836686607797776e6a85001d9a1dd22d4e38c8b006d499b55  fm1bench.fwsc
2ff039126e28bb3d8f803600a515fe57a91c821f363f3af95bb3810f0b98f6e0  fm1rack.fwsc
153a8747d621d29d05262be0cb01b6b16336c67a8dd2af42915e3c8be73e02c1  fm1bench-os.fwsc
```

## Step 0: the recovery round trip (once, before anything here)

FM-1 plugged **directly** into the Mac by USB (no hub). In Chrome or Edge: https://hugelton.github.io/Felucca/ →
install Felucca, play it, then **Return to official V15**. This proves you can get back to stock.

## Step 1: set up the installer (once)

In Terminal on the Mac:

```
cd MODFM1/firmware/packages      # wherever you downloaded the repo
shasum -a 256 -c SHA256SUMS   # both lines must say OK
pip3 install mido python-rtmidi
git clone https://github.com/charlesvestal/fm1-base ~/fm1-base
```

Close anything else that uses MIDI (DAWs, browser tabs with Web MIDI) before installing.

## Step 2: the benchmark

```
python3 ~/fm1-base/tools/fm1_install.py fm1bench.fwsc
```

1. Note the top line of the screen. Expected: **FPU OK: HARDWARE FLOAT, EXACT**. A photo is fine. If you see a crash
   screen instead, stop here and go back to stock (below).
2. Let it run (about a minute).
3. Open `../fm1/bench/reader.html` in Chrome, press **Listen**, then press **PLAY** on the FM-1. Save the CSV.

## Step 3: FM1 Rack Lite (only if step 2 said FPU OK)

```
python3 ~/fm1-base/tools/fm1_install.py fm1rack.fwsc
```

Play presets (PRESET knob; ALGORITHM changes category), try K1-K4, SELECT for voices. Note the CPU % and any dropout
count on the screen, and any preset that sounds wrong. Controls are in `../fm1/rack/README.md`.

## Getting back to stock

In order of what to try (from fm1-base and Felucca; not yet tried with these two firmwares):

1. Felucca's web installer again, then Return to official V15.
2. fm1-base's installer page, "Back to the stock firmware".
3. After two failed starts the FM-1 comes up in safe mode (no audio, USB on), which takes an update.
4. After four, the chip's own update mode: M-VAVE's M-UPGRADE (Downloads > PC Software) restores stock.

## How this was built (for the record)

- Toolchain: JieLi Linux toolchain 20260730.1 (pkgman.jieliapp.com), run natively on x86-64.
- SDK files: `uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin` from AC79 SDK V1.2.1 (revision d179b44). JieLi's
  GitHub copy of that SDK is no longer public, so they came from **JieLi's own Gitee repository**
  (gitee.com/Jieli-Tech, "珠海杰理科技", JieLi's official account), at the same revision. All three are byte-identical
  to fm1-base's reference (SHA-256 checked). `../tools/build_packages.sh` fetches from there.
- Mutable code: pichenettes/eurorack f4cfa76. fm1-base: 2d8ed74.
- `build-checks.txt` has fm1-base's check lines for both builds.
