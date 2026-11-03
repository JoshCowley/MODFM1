# FM1 Bench: timing FM1 Rack on a real FM-1

**Status: builds and passes fm1-base's own checks, has not yet run on an FM-1.** This is a test firmware, not FM1
Rack: it plays no sound and saves nothing. It answers two questions on the board itself:

1. **Does the FM-1's chip run hardware floating point?** Felucca's plugin guide says the FM-1 has no FPU. fm1-base,
   whose firmwares do run on the FM-1, builds everything with `-mcpu=r3 -mfprev1` (JieLi's single-precision FPU
   flags, "the SDK's own flags" in its build.py) and its build check refuses double precision. This firmware is
   built the same way, then checks a known single-precision sum at power-on.
2. **How much of the CPU does each module take?** It runs FM1 Rack's real engine (`engine/rack.cc`, built
   `-DRACK_FM1 -DRACK_LITE`) and times 105 cases: every Plaits model, every Braids shape, every Rings model, the
   filter, Tides, Peaks, Grids, the delay, reverb and chorus, 1 to 6 voices, and 10 factory presets; then, when the second core starts, 11 of them again on two cores (the 2 CORES group: the second core renders half the voices).

## What you see

The FPU check comes first, at the top of the screen: **FPU OK: HARDWARE FLOAT, EXACT** means the float instructions
ran and gave the IEEE result. (If the chip had no FPU, the firmware would stop at its first float instruction
instead, and fm1-base's crash screen would show.) Then the cases run, one after another, about a minute in all.
Each line shows:

- **CPU %**: how much of one core that case takes, at the engine's 48 kHz. 100 % is the whole core; over 100 %
  means it can't run in real time. It's the fastest of 5 timed runs (interrupts only ever add time).
- in brackets, for modules measured by difference, **its own share**: the case minus the same preset without it
  (Rings, the filter, Tides, Peaks, Grids, the effects).
- otherwise the **voices** playing (the arena may fit fewer than asked for).
- **SIL** or **NAN** if a case made silence or a bad number (none should).

PRESET or ALGORITHM scrolls; OCT- and OCT+ page; REC runs everything again. **PLAY sends every result over USB-MIDI**:
open `reader.html` in Chrome or Edge, press Listen, then PLAY on the FM-1, and save the table as CSV.

Results are what to send back, so the page's CPU model and the module choice for FM1 Rack Lite can use real numbers.

## Before you install it

Only after you have done the Felucca round trip (PORTING.md, step 1): Felucca installed with its web installer, then
**Return to official V15**, at least once. That proves your FM-1, cable and computer can get back to stock.

This firmware is built on fm1-base. From fm1-base's code and installer page (not yet tried with this firmware):
fm1-base firmwares keep Felucca's update entry, so a running one takes the next update over USB the way Felucca
does, and its installer's "Back to the stock firmware" writes M-VAVE's firmware back from it; after two failed starts
it comes up in a safe mode (no audio, USB on) that takes updates too, and after four, the chip's own update mode,
where M-VAVE's M-UPGRADE works. The benchmark runs the engine in the main loop, not the audio interrupt, so a slow case
only takes longer; it can't stall the sound, and it does 10 ms of sound per screen frame, so a step stays far
under the 8 s watchdog unless a case needs hundreds of cores.

## Build it (Mac or Linux)

You need Docker (on a Mac; build.py runs JieLi's Linux toolchain in a linux/amd64 container), Python 3, git, and
three checkouts side by side:

```
work/
  MODFM1/          this repo
  fm1-base/        git clone --recursive https://github.com/charlesvestal/fm1-base
  eurorack/        git clone --recursive https://github.com/pichenettes/eurorack
```

Then, from `fm1-base/`:

```
tools/get_toolchain.sh                     # JieLi's toolchain into ~/.jieli (prints JIELI_TOOLCHAIN=...)
bsp/tools/get_sdk.sh                       # the three AC79 SDK files, from JieLi's GitHub, hash-checked
export JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/.jieli/ac79-sdk
tools/build.py ../MODFM1/firmware/fm1/bench
```

It should end with `ok` checks (no soft-double routines, image about 551 KB of the 568 KB app area, pool within its
headroom) and `package .../build/fm1bench.fwsc, identity FM-1_7700`. Use only a package you built yourself from
fetched SDK files whose hashes passed.

Install it with fm1-base's installer (needs `pip install mido python-rtmidi`):

```
python3 tools/fm1_install.py ../MODFM1/firmware/fm1/bench/build/fm1bench.fwsc
```

To go back: install Felucca again, or M-VAVE's firmware with M-UPGRADE (or fm1-base's installer page, made by
`tools/make_site.py`, which has "Back to the stock firmware").

## Checked on a computer first

`sh build_host.sh && build/host/bench_host` (with `EURORACK` set if the checkout isn't beside MODFM1) builds the same
cases and engine with the computer's compiler and checks that every case makes sound, that each preset loaded as
INIT plus its differences (`cases.h`) sounds the same as the preset's own calls, sample for sample, and the FPU
check's expected value. Its timings are the computer's, not the FM-1's.

## Files

| File | What |
|---|---|
| `fm1.toml`, `app.h`, `app.c` | The fm1-base app: settings, screen, buttons, the SysEx dump |
| `bench.c` | The cases and how each is timed (shared with `bench_host.c`) |
| `cases.h` | The presets' engine calls, from `firmware/tests/presets-calls.json` by `make_cases.py` |
| `rk.cc` | The engine's C interface under names that don't clash with fm1-base's |
| `rt.cc` | What C++ needs and fm1-base doesn't link: a few runtime stubs and single-precision maths on fm1-base's `fastmath.h` |
| `make_units.py` | Run by the build: `units/`, one file per engine source |
| `../../hw/libc/` | C headers for a build with no C library |
| `reader.html` | Shows and saves the results the FM-1 sends |

## Known limits

- The maths functions (`sinf`, `expf`, `powf`...) are fastmath.h's approximations, not a full libm: the sound is
  close to the computer's, not identical. Timing is what matters here.
- `id_prefix = "FM-1_77"` was picked for this test and isn't registered anywhere.
- The FM-1's codec runs at 44.1 kHz and the engine at 48 kHz: the figures are for rendering 48,000 frames a second.
- The engine keeps some state (delay lines, the clock) from one case to the next; each case starts from the same
  random seed, so runs are comparable, but a case's sound can differ slightly from the same case run alone.
