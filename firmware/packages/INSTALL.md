# FM1 Rack: test kit (built 3 November 2026)

Two packages, built from the FM1 Rack sources with JieLi's toolchain and fm1-base (commit 2d8ed74). Every fm1-base
build check passed, the three JieLi SDK files matched fm1-base's reference hashes exactly, and both computer checks
("all passed") ran first. **Neither has run on an FM-1 yet: this kit is for that first test.**

| File | What | Identity |
|---|---|---|
| `packages/fm1bench.fwsc` | The benchmark: FPU check + 116 timed cases (105, then 11 on two cores). Silent. **Install first.** | FM-1_7700 |
| `packages/fm1rack.fwsc` | FM1 Rack Lite, first stage: 57 presets, keys + MIDI, macros on K1-K4. Rebuilt 3 Nov after the benchmark: the second core renders half the voices (up to 6 to start on two cores, 4 on one); a lighter engine; a preset too heavy even at 1 voice is muted with a message | FM-1_7800 |

SHA-256 (also in `packages/SHA256SUMS`):

```
4e9d0c01766231606212a09eac19c01db1409eff06719296ad55df8f77ce1d3e  fm1bench.fwsc
3da504c24869e73e4eb2138d7d253b7a38906d38351b7bd894608286c1ee6861  fm1rack.fwsc
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
