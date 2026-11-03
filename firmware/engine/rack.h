// FM1 Rack engine: the C interface a platform layer calls.
//
// The engine is engine/rack.cc, the same file the browser build compiles. Build it with -DRACK_FM1
// (and optionally -DRACK_ARENA_KB=<n>, default 340: the page's 384K FREE RAM setting less the engine's
// 44 KB of fixed memory) for the firmware-ready version:
//   - the big buffers (Plaits, Braids, Elements, Rings, Warps, Clouds, delay, reverb, chorus) come from one
//     static arena of RACK_ARENA_KB, laid out for the modules actually loaded;
//   - voices are limited to what fits (up to 6); modules that can't fit at all are unloaded, FX 2 first;
//   - the delay line is 1 s and the delay and chorus lines are 16-bit.
// Everything else (envelopes, LFOs, matrix, Peaks, Tides, Marbles, Grids, master) is small and static.
//
// Threading: render() runs in the audio interrupt. Every other call changes engine state and must not run
// while render() is running: queue them from the UI and apply them at the start of the audio callback
// (the browser build does the same through its AudioWorklet message queue).
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// slots: 0 SRC, 1 SHP, 2 MOD A, 3 MOD B, 4 FX 1, 5 FX 2, 6 AMP, 7 GLOBAL (param 0 = tempo), 8 LFOs
// module ids: 1 Plaits, 2 Braids, 3 Elements, 10 Rings, 11 Warps, 12 Filter, 20 Peaks, 21 Tides, 22 Marbles,
//             23 Grids, 30 Clouds, 31 Delay, 32 Reverb, 33 Chorus, 0 empty
void init(void);
void set_module(int slot, int id);
void set_param(int slot, int param, float value);
void set_meta(int slot, int param, float lo, float hi, float span);   // the parameter's range, for modulation
void set_route(int i, int src, int slot, int param, float amount, int via);   // 12 matrix rows, then 6 LFO targets
void set_macro(int i, float v);
void set_master(float v);
void set_master_fx(float low_db, float mid_db, float high_db, float comp);
void set_voices(int n);          // 1-6; the FM-1 build caps this at what fits
void set_legato(int on);
void set_vintage(float amount);
void set_tune(int note, float semitones);
void set_bend(float semitones);
void set_ext(int i, float v);    // modulation lanes
void set_ext_slew(int i, float s);
void note_on(float note, float velocity);
void note_off(float note);
void all_off(void);
void reset_clock(void);

// audio: renders n frames (any n) into out_l() / out_r(), floats at 48 kHz
void render(int n);
float* out_l(void);
float* out_r(void);

// screen data
float* monitor(int slot);        // 200 values: live parameters, envelopes, voices, LFOs, levels
float* scope(void);              // 128 samples for the scope

#ifdef RACK_FM1
// 0 arena bytes used, 1 arena size, 2 voices that fit, 3 voices playing,
// 4 unloaded slots (bit per slot: 0 SRC, 1 SHP, 4 FX 1, 5 FX 2)
int fm1_status(int what);
// the second core: run(fn, arg) hands fn(arg) to it (1) or declines (0); wait() returns 0 when done, -1 when it
// dropped the job. With a worker set, 2 or more voices are split between the cores. worker_blocks: blocks shared
void set_worker(int (*run)(void (*)(uint32_t), uint32_t), int (*wait)(void));
uint32_t worker_blocks(void);
#endif

#ifdef __cplusplus
}
#endif
