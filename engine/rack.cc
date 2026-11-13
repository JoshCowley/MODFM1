// FM1 Rack engine: Mutable Instruments modules (MIT; GPL-3 project) slotted
// into one voice chain. Block = 24 frames @ 48 kHz.
//
// One source, two builds:
//   default    the browser build (rack.wasm): every buffer is a fixed array, 6 voices of anything.
//   RACK_FM1   the firmware-ready build: the big buffers come from one memory arena of RACK_ARENA_KB,
//              carved up for the modules actually loaded; voices are limited to what fits; the delay
//              (1 s) and chorus lines are 16-bit. Nothing here touches hardware: a platform layer
//              (firmware/hw) calls the functions declared in firmware/engine/rack.h.

#include <math.h>
#include <string.h>
#include <stdint.h>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/buffer_allocator.h"
#include "stmlib/utils/gate_flags.h"
#include "stmlib/utils/random.h"

#include "plaits/dsp/voice.h"
#include "braids/macro_oscillator.h"
#include "rings/dsp/part.h"
#ifndef RACK_LITE
#include "elements/dsp/part.h"
#endif
#ifndef RACK_LITE
#include "clouds/dsp/granular_processor.h"
#else
#include "clouds/dsp/frame.h"   // the reverb's frame type
#endif
#include "clouds/dsp/fx/reverb.h"
#include "peaks/modulations/lfo.h"
#include "peaks/modulations/multistage_envelope.h"
#include "tides2/poly_slope_generator.h"
#ifndef RACK_LITE
#include "warps/dsp/modulator.h"
#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"
#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
namespace marbles { extern const Scale preset_scales[6]; }
#endif
namespace grids_data {
extern const uint8_t node_0[], node_1[], node_2[], node_3[], node_4[], node_5[], node_6[], node_7[], node_8[], node_9[],
  node_10[], node_11[], node_12[], node_13[], node_14[], node_15[], node_16[], node_17[], node_18[], node_19[],
  node_20[], node_21[], node_22[], node_23[], node_24[];
}

#if defined(__wasm__)
#define EXPORT extern "C" __attribute__((visibility("default")))
#else
#define EXPORT extern "C"
#endif
#ifdef RACK_FM1
#include <new>
#define RINGS_SHARED 1   // one Rings for all the voices (render_voice, run_rings_shared)
#define RINGS_POLY 2     // the notes it rings at once
#ifndef RACK_ARENA_KB
#define RACK_ARENA_KB 340   // the 384K FREE RAM setting less the engine's 44 KB of fixed memory
#endif
#endif

using namespace stmlib;

static const float SR = 48000.0f;
#ifdef RACK_FM1
// the FM-1: blocks of 48 (1 ms). Each module's code is fetched from flash about once a block, and on the FM-1 that,
// not the samples, was most of Rings' and the reverb's time (bench 5-7, 12 Nov 2026: half the samples saved them
// nothing), so twice the samples a block halves it. Every time in the engine is written against BLOCK.
static const int BLOCK = 48;
#else
static const int BLOCK = 24;
#endif
static const int MAXV = 6;

enum Slot { S_SRC, S_SHP, S_MODA, S_MODB, S_FX1, S_FX2, S_AMP, S_GLO, S_LFO, NSLOT };
enum ModId { M_NONE = 0, M_PLAITS = 1, M_BRAIDS = 2, M_ELEMENTS = 3,
             M_RINGS = 10, M_WARPS = 11, M_FILTER = 12,
             M_PEAKS = 20, M_TIDES = 21, M_MARBLES = 22, M_GRIDS = 23, M_FUNC = 24,
             M_CLOUDS = 30, M_DELAY = 31, M_REVERB = 32, M_CHORUS = 33 };
enum Src { SR_NONE, SR_M1, SR_M2, SR_M3, SR_M4, SR_VELO, SR_KEY, SR_RAND, SR_ENV,
           SR_MA0, SR_MA1, SR_MB0, SR_MB1, SR_LFO1, SR_LFO2, SR_LFO3, SR_MENV,
           SR_SEQ1, SR_SEQ2, SR_SEQ3, SR_SEQ4, SR_WHEEL, SR_PRESS, NSRC };
// external sources set from the page: 0-3 mod sequencer lanes, 4 mod wheel, 5 pressure (aftertouch)
static float ext_tgt_[6], ext_cur_[6], ext_slew_[6] = { 1, 1, 1, 1, 0.3f, 0.3f };
// tuning: an offset in semitones per MIDI note; pitch bend in semitones; VINTAGE drift amount; MONO retrigger
static float tune_[128];
static float bend_ = 0.0f, vintage_ = 0.0f;
static bool legato_ = true;
static float card_[6], vdrift_[6];

static const int NP = 32;
static float base_[NSLOT][NP];
static void osc_defaults();   // OSC (below): its ranges and defaults, on AMP's spare parameters
static float lo_[NSLOT][NP], hi_[NSLOT][NP], span_[NSLOT][NP];
static int mod_id_[NSLOT];
static float macros_[4];
static float mod_out_[4];       // MOD A out0/out1, MOD B out0/out1
struct FuncCh { float x; int stage; bool trig_prev, evt; float eoc; };   // FUNC (run_func): x 0..1 along the slope; stage 0 idle 1 rise 2 fall 3 hold
struct FuncState { FuncCh ch[2]; uint32_t ages; };
static FuncState func_[2];
static float pitch_mod_dummy;

struct Route { int src, slot, param, via; float amt; };
static const int NROUTE = 18;   // 12 matrix rows, then two direct targets per LFO
static Route routes_[NROUTE];
// the params some route reaches, per slot (slot -1, the voice's pitch and pan, at 0): mod_sum skips the 18 rows for
// the rest. Kept by set_route and init. Same result as scanning: an unrouted param sums nothing either way.
static uint32_t routed_[NSLOT + 1];
static void routed_rebuild() {
  memset(routed_, 0, sizeof(routed_));
  for (int i = 0; i < NROUTE; ++i) {
    const Route& r = routes_[i];
    if (r.src != SR_NONE && r.slot >= -1 && r.slot < NSLOT && r.param >= 0 && r.param < 32) routed_[r.slot + 1] |= 1u << r.param;
  }
}

// ------------------------------------------------------------------ voice
struct VoiceState {
  bool gate, retrig, active;
  float note, cur_note, vel, rnd;
  float env; int stage;          // 0 idle 1 att 2 dec 3 sus 4 rel
  uint32_t age;
  float drift;
  float pan;
  bool prev_gate_el;
  float menv; int mstage;        // mod env: 0 idle 1 att 2 dec 3 sus 4 rel
  float tm;                      // seconds spent in the AMP delay or hold stage
  float fenv; int fstage;        // the filter's own ADSR
  float benv; int bstage;        // Braids' internal AD
  float pend_vel;                // stage 7: a stolen voice fades for a block or two, then plays the new note at this velocity
  float gl0, gr0;                // the last block's gains: each block ramps from them, so the envelope has no steps
};

static VoiceState vs_[MAXV];
#ifdef RACK_FM1
// carved from arena_ by fm1_layout() for the modules loaded
static plaits::Voice* plaits_; static char (*plaits_buf_)[16384];
static braids::MacroOscillator* braids_;
static rings::Part* rings_; static uint16_t (*rings_rev_)[32768];
#ifndef RACK_LITE
static elements::Part* elements_; static uint16_t (*elements_rev_)[32768];
static warps::Modulator* warps_;
#endif
#else
static plaits::Voice plaits_[MAXV];
static char plaits_buf_[MAXV][16384];
static braids::MacroOscillator braids_[MAXV];
static rings::Part rings_[MAXV];
static uint16_t rings_rev_[MAXV][32768];
#ifndef RACK_LITE
static elements::Part elements_[MAXV];
static uint16_t elements_rev_[MAXV][32768];
static warps::Modulator warps_[MAXV];
#endif
#endif
static Svf svf_[MAXV], svf2_[MAXV];
static int nvoices_ = 2;
static int shrink_to_ = 0;   // fewer voices asked for while some still fade out (set_voices): the count they go to
static int last_voice_ = 0;
static uint32_t age_counter_ = 0;

// ------------------------------------------------------------------ shared
#ifndef RACK_LITE
static clouds::GranularProcessor clouds_;
#endif
static int clouds_quality_ = 0;
static float cl_scan_ = 0.0f, cl_tone_l_ = 0.0f, cl_tone_r_ = 0.0f;
static int cl_grid_ = -1;
namespace clouds { extern const float lut_xfade_in[]; extern const float lut_xfade_out[]; }
static const int CL_LARGE = 118784, CL_SMALL = 65536 - 128, DLEN_MAX = 96000;
// delay and chorus lines: floats in the browser, 16-bit on the FM-1
#ifdef RACK_FM1
typedef int16_t dsamp_t;
static const int DLEN = 48000;   // 1 s
// 16-bit lines keep 6 dB of headroom (full scale = 2.0): mixes of several voices go past 1.0 before the limiter
static inline float DG(int16_t v) { return v * (2.0f / 32767.0f); }
static inline int16_t DS(float x) { x *= 0.5f; x = x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x; return (int16_t)(x * 32767.0f + (x >= 0 ? 0.5f : -0.5f)); }
static uint8_t* clouds_large_; static uint8_t* clouds_small_;
static uint16_t* reverb_buf_;
static dsamp_t *delay_l_, *delay_r_;
#else
typedef float dsamp_t;
static const int DLEN = DLEN_MAX;
static inline float DG(float v) { return v; }
static inline float DS(float x) { return x; }
#ifndef RACK_LITE
static uint8_t clouds_large_[CL_LARGE];
static uint8_t clouds_small_[CL_SMALL];
#endif
static uint16_t reverb_buf_[16384];
static float delay_l_[DLEN_MAX], delay_r_[DLEN_MAX];
#endif
static clouds::Reverb reverb_;
// the longest delay; TAPE wow swings the read point up to ~100 samples further, so the FM-1's 1 s line keeps 128 spare
static const int DMAX_FM1 = 48000 - 128;
#ifdef RACK_FM1
static int delay_max_ = DMAX_FM1, delay_q16_ = 1;
#else
static int delay_max_ = DLEN - 1, delay_q16_ = 0;   // the browser's FM-1 preview (set_hw) switches to DMAX_FM1 and 16-bit values
#endif
static inline float q16(float x) { x *= 0.5f; x = x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x; return (float)(int)(x * 32767.0f + (x >= 0 ? 0.5f : -0.5f)) * (2.0f / 32767.0f); }   // as the FM-1 build's 16-bit lines
static int delay_w_ = 0;
static int dlen_ = DLEN;           // the delay line's length (fm1_fit: what the patch needs, on the FM-1)
static int pre_mask_ = 8191;       // the reverb's pre-delay lines: a power of two, less one (fm1_fit)
#ifdef RACK_FM1
static dsamp_t *chorus_l_, *chorus_r_;
#else
static float chorus_l_[4096], chorus_r_[4096];
#endif
static int chorus_w_ = 0;
static float chorus_phase_ = 0.0f;
static float delay_tone_l_ = 0.0f, delay_tone_r_ = 0.0f;
// shimmer: an octave-up pitch shifter (two crossfaded grains) feeding the reverb
#ifdef RACK_FM1
static int16_t *shim_buf_, *pre_buf_l_, *pre_buf_r_;
#else
static int16_t shim_buf_[4096], pre_buf_l_[8192], pre_buf_r_[8192];
#endif
static int shim_w_ = 0; static float shim_ph_ = 0.0f, shim_fb_ = 0.0f;
static int pre_w_ = 0;
static float tape_ph1_ = 0.0f, tape_ph2_ = 0.0f;
static float rv_lc_l_ = 0, rv_lc_r_ = 0, rv_duck_ = 0, dl_lc_l_ = 0, dl_lc_r_ = 0, dl_duck_ = 0, ch_fb_l_ = 0, ch_fb_r_ = 0;

static peaks::Lfo peaks_lfo_[2];
static peaks::MultistageEnvelope peaks_env_[2];
static tides::PolySlopeGenerator tides_[2];
static GateFlags mod_prev_gate_[2];
static float tap_last_[2], tap_period_[2];
static float tides_raw_ = 0.0f;
// marbles
#ifndef RACK_LITE
static marbles::RandomGenerator marbles_rng_;
static marbles::RandomStream marbles_stream_;
static marbles::TGenerator marbles_t_[2];
static marbles::XYGenerator marbles_xy_[2];
#endif
static float marbles_t_env_[2];
// grids
static int grids_step_ = -1;
static float grids_env_[2][2];   // [slot][trigger, accent]
static bool grids_gate_ = false;
// built-in lfos
static float lfo_phase_[3], lfo_val_[3], lfo_raw_[3], lfo_sh_[3], lfo_sh_prev_[3];
static bool lfo_done_[3];
static float lfo_since_gate_ = 0.0f;
static const uint8_t* const grids_map_[5][5] = {
  { grids_data::node_10, grids_data::node_8, grids_data::node_0, grids_data::node_9, grids_data::node_11 },
  { grids_data::node_15, grids_data::node_7, grids_data::node_13, grids_data::node_12, grids_data::node_6 },
  { grids_data::node_18, grids_data::node_14, grids_data::node_4, grids_data::node_5, grids_data::node_3 },
  { grids_data::node_23, grids_data::node_16, grids_data::node_21, grids_data::node_1, grids_data::node_2 },
  { grids_data::node_24, grids_data::node_19, grids_data::node_17, grids_data::node_20, grids_data::node_22 } };
static float P(int slot, int param, const VoiceState* v);
static int grids_level(int slot, int inst, int step) {
  uint8_t xx = (uint8_t)(P(slot, 0, 0) * 255.0f), yy = (uint8_t)(P(slot, 1, 0) * 255.0f);
  int i = xx >> 6, j = yy >> 6, off = inst * 32 + step;
  uint8_t a = grids_map_[i][j][off], b = grids_map_[i + 1][j][off], c = grids_map_[i][j + 1][off], d = grids_map_[i + 1][j + 1][off];
  uint8_t xb = xx << 2, yb = yy << 2;
  uint8_t ab = (a * (255 - xb) + b * xb) >> 8, cd = (c * (255 - xb) + d * xb) >> 8;
  return (ab * (255 - yb) + cd * yb) >> 8;
}
static bool lfo_any_gate_prev_ = false;
static float TIDES_AD_MAX = 8.0f;
EXPORT void set_tides_ad_max(float v) { TIDES_AD_MAX = v; }
EXPORT float tides_raw() { return tides_raw_; }

// clock / gate bus
#ifdef RACK_FM1
// the FM-1's FPU is single precision only: the clock is whole 16ths plus a fraction, exact however long it runs
static int clk_i_ = 0; static float clk_f_ = 0.0f;
#define CLK() ((float)clk_i_ + clk_f_)
#define CLK_I() clk_i_
static inline void clk_reset() { clk_i_ = 0; clk_f_ = 0.0f; }
static inline void clk_advance(float d) { clk_f_ += d; while (clk_f_ >= 1.0f) { clk_f_ -= 1.0f; ++clk_i_; } if (clk_i_ >= 4096) clk_i_ -= 4096; }
#else
static double clock_phase_ = 0.0;   // in 16ths
#define CLK() ((float)clock_phase_)
#define CLK_I() ((int)clock_phase_)
static inline void clk_reset() { clock_phase_ = 0.0; }
static inline void clk_advance(float d) { clock_phase_ += d; if (clock_phase_ > 4096.0) clock_phase_ -= 4096.0; }
#endif
static bool any_gate_ = false;
static float master_ = 0.7f;

// io
#ifdef RACK_FM1
static const int OUTN = 512;   // the FM-1: ~280 frames a block, the bench 480 (update 44: 4 KB less RAM)
#else
static const int OUTN = 1024;
#endif
static float out_l_[OUTN], out_r_[OUTN];
static float peak_l_ = 0.0f, peak_r_ = 0.0f;
static float scope_[256];
static float blk_l_[BLOCK], blk_r_[BLOCK];
static int blk_pos_ = BLOCK;

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
#ifdef RACK_FM1
// the FM-1 has no fast libm: these stand in for tanhf and sinf in per-sample loops (10 Nov 2026)
// tanh: a [7/6] Pade fit, within 1e-6 of tanhf up to |x| 4.97, where tanh is 0.9999 and it is held at 1
static inline float fast_tanh(float x) {
  if (x > 4.97f) return 1.0f;
  if (x < -4.97f) return -1.0f;
  float x2 = x * x;
  return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f)));
}
// sin(2 pi p) for any p: a parabola, corrected (within 0.001 of sinf), for LFO-like uses (chorus, wow, windows)
static inline float fast_sin01(float p) {
  p -= floorf(p);
  float x = (p < 0.5f ? p : p - 1.0f) * 6.2831853f;            // -pi..pi
  float y = 1.2732395f * x - 0.4052847f * x * fabsf(x);         // a parabola through the sine's zeros and peaks
  return 0.225f * (y * fabsf(y) - y) + y;
}
#define TANHF fast_tanh
#define SIN01(p) fast_sin01(p)
// 48 kHz <-> 24 kHz for the modules the FM-1 runs at half rate (Rings, the reverb): a 39-tap halfband filter (flat to
// 10 kHz, -75 dB from 16 kHz), as its 10 taps either side of the middle (0.5); the others are 0
static const float HB24[10] = {0.315457678f, -0.097823539f, 0.050681315f, -0.028869892f, 0.016396910f,
                               -0.008843422f, 0.004345613f, -0.001840541f, 0.000600712f, -0.000099368f};
struct Half24Down {   // a block at 48 kHz in, half a block at 24 kHz out (19 samples late)
  float h[38 + BLOCK];
  void run(const float* x, float* y) {
    for (int n = 0; n < BLOCK; ++n) h[38 + n] = x[n];
    for (int m = 0; m < BLOCK / 2; ++m) {
      const float* c = h + 19 + 2 * m;
      float v = 0.5f * c[0];
      for (int j = 0; j < 10; ++j) v += HB24[j] * (c[-(2 * j + 1)] + c[2 * j + 1]);
      y[m] = v;
    }
    for (int n = 0; n < 38; ++n) h[n] = h[n + BLOCK];   // (the FM-1 has no memmove)
  }
};
struct Half24Up {     // half a block at 24 kHz in, a block at 48 kHz out: each sample, then the one halfway to the next
  float h[20 + BLOCK / 2];
  void run(const float* y, float* x) {
    for (int m = 0; m < BLOCK / 2; ++m) h[20 + m] = y[m];
    for (int k = 0; k < BLOCK / 2; ++k) {
      const float* u = h + 9 + k;
      float v = 0.0f;
      for (int j = 0; j < 10; ++j) v += HB24[j] * (u[-j] + u[1 + j]);
      x[2 * k] = u[0]; x[2 * k + 1] = 2.0f * v;
    }
    for (int n = 0; n < 20; ++n) h[n] = h[n + BLOCK / 2];
  }
};
#else
#define TANHF tanhf
#define SIN01(p) sinf((p) * 6.2831853f)
#endif
static inline float randf() { return Random::GetFloat(); }

// ------------------------------------------------------------------ params
static float source_value(int s, const VoiceState* v) {
  switch (s) {
    case SR_M1: case SR_M2: case SR_M3: case SR_M4: return macros_[s - SR_M1];
    case SR_VELO: return v ? v->vel : 0.0f;
    case SR_KEY: return v ? clampf((v->note - 60.0f) / 24.0f, -1.0f, 1.0f) : 0.0f;
    case SR_RAND: return v ? v->rnd : 0.0f;
    case SR_ENV: return v ? v->env : 0.0f;
    case SR_MA0: return mod_out_[0];
    case SR_MA1: return mod_out_[1];
    case SR_MB0: return mod_out_[2];
    case SR_MB1: return mod_out_[3];
    case SR_LFO1: case SR_LFO2: case SR_LFO3: return lfo_val_[s - SR_LFO1];
    case SR_MENV: return v ? v->menv : 0.0f;
    case SR_SEQ1: case SR_SEQ2: case SR_SEQ3: case SR_SEQ4: case SR_WHEEL: case SR_PRESS: return ext_cur_[s - SR_SEQ1];
    default: return 0.0f;
  }
}

static float mod_sum(int slot, int param, const VoiceState* v) {
  if (slot >= -1 && slot < NSLOT && param >= 0 && param < 32 && !(routed_[slot + 1] >> param & 1u)) return 0.0f;
  float m = 0.0f;
  for (int i = 0; i < NROUTE; ++i) {
    const Route& r = routes_[i];
    if (r.src == SR_NONE || r.slot != slot || r.param != param) continue;
    float s = source_value(r.src, v) * r.amt;
    if (r.via != SR_NONE) s *= source_value(r.via, v);
    m += s;
  }
  return m;
}

static inline float P(int slot, int param, const VoiceState* v) {
  float x = base_[slot][param];
  // a parameter nothing routes to (most of them) skips the call: P runs hundreds of times a block (10 Nov 2026)
  if (param >= 32 || (routed_[slot + 1] >> param & 1u)) x += mod_sum(slot, param, v) * span_[slot][param];
  return clampf(x, lo_[slot][param], hi_[slot][param]);
}
static int PI(int slot, int param, const VoiceState* v) {
  return (int)(P(slot, param, v) + 0.5f);
}

// ------------------------------------------------------------------ exports
EXPORT float* out_l() { return out_l_; }
EXPORT float* out_r() { return out_r_; }
EXPORT float* scope() { return scope_; }

// a knob's turn glides (set_param_glide, 11 Nov 2026): the parameter moves to its target over a few ms, a step of a
// block at a time, so a level, a cutoff or a delay time turned doesn't click or zipper. One bit per parameter says it
// glides; the main loop sets targets and bits, the render moves and clears them (a bit set again by a racing
// main-loop write only glides once more to where it already is).
static float tgt_[NSLOT][NP];
static uint32_t glide_[NSLOT];
EXPORT void set_param(int slot, int param, float value) {
  if (slot < 0 || slot >= NSLOT || param < 0 || param >= NP) return;
  glide_[slot] &= ~(1u << param);
  tgt_[slot][param] = value; base_[slot][param] = value;
}
EXPORT void set_param_glide(int slot, int param, float value) {
  if (slot < 0 || slot >= NSLOT || param < 0 || param >= NP) return;
  tgt_[slot][param] = value;
  glide_[slot] |= 1u << param;
}
EXPORT float get_param(int slot, int param) {
  if (slot < 0 || slot >= NSLOT || param < 0 || param >= NP) return 0.0f;
  return glide_[slot] >> param & 1u ? tgt_[slot][param] : base_[slot][param];
}
// a matrix row as set: out[0] source, [1] slot, [2] param, [3] via; returns its amount
EXPORT float get_route(int i, int* out) {
  if (i < 0 || i >= NROUTE) { out[0] = out[1] = out[2] = out[3] = 0; return 0.0f; }
  out[0] = routes_[i].src; out[1] = routes_[i].slot; out[2] = routes_[i].param; out[3] = routes_[i].via;
  return routes_[i].amt;
}
EXPORT void set_meta(int slot, int param, float lo, float hi, float span) {
  lo_[slot][param] = lo; hi_[slot][param] = hi; span_[slot][param] = span;
}
static float macro_tgt_[4];
static uint8_t macro_gl_[4];
EXPORT void set_macro(int i, float v) { if (i >= 0 && i < 4) { macro_gl_[i] = 0; macro_tgt_[i] = v; macros_[i] = v; } }
EXPORT void set_macro_glide(int i, float v) { if (i >= 0 && i < 4) { macro_tgt_[i] = v; macro_gl_[i] = 1; } }
// each block: the gliding parameters and macros a part of the way to their targets (about 5 ms to settle)
static void glide_step() {
  const float k = BLOCK >= 48 ? 0.2f : 0.1f;
  for (int s = 0; s < NSLOT; ++s) {
    uint32_t g = glide_[s];
    while (g) {
      int p = __builtin_ctz(g);
      g &= g - 1u;
      float d = tgt_[s][p] - base_[s][p];
      if (fabsf(d) <= 1e-4f * (1.0f + fabsf(tgt_[s][p]))) { base_[s][p] = tgt_[s][p]; glide_[s] &= ~(1u << p); }
      else base_[s][p] += d * k;
    }
  }
  for (int i = 0; i < 4; ++i)
    if (macro_gl_[i]) {
      float d = macro_tgt_[i] - macros_[i];
      if (fabsf(d) <= 1e-4f) { macros_[i] = macro_tgt_[i]; macro_gl_[i] = 0; }
      else macros_[i] += d * k;
    }
}
EXPORT void set_master(float v) { master_ = v; }
EXPORT void set_ext(int i, float v) { if (i >= 0 && i < 6) ext_tgt_[i] = v; }
EXPORT void set_ext_slew(int i, float s) { if (i >= 0 && i < 6) ext_slew_[i] = clampf(s, 0.001f, 1.0f); }
EXPORT void set_tune(int note, float semis) { if (note >= 0 && note < 128) tune_[note] = semis; }
EXPORT void set_bend(float semis) { bend_ = semis; }
EXPORT void set_vintage(float a) { vintage_ = a; }
// An effect slot switched off from the panel (FM-1, 14 Nov 2026: hold FX, then one tap FX 1, two taps FX 2): skipped,
// so it costs nothing; its settings stay. Switched on again, its buffers are cleared first (at the next block), so no
// old echo comes back. A preset load (init) switches both on.
static volatile int fx_off_[2] = {0, 0}, fx_clear_[2] = {0, 0};
EXPORT void set_fx_bypass(int slot, int off) {
  if (slot < S_FX1 || slot > S_FX2) return;
  int k = slot - S_FX1;
  if (!off && fx_off_[k]) fx_clear_[k] = 1;
  fx_off_[k] = off ? 1 : 0;
}
EXPORT int fx_bypass(int slot) { return slot >= S_FX1 && slot <= S_FX2 ? fx_off_[slot - S_FX1] : 0; }
EXPORT void set_legato(int on) { legato_ = on != 0; }
EXPORT void set_route(int i, int src, int slot, int param, float amt, int via) {
  if (i < 0 || i >= NROUTE) return;
  routes_[i].src = src; routes_[i].slot = slot; routes_[i].param = param;
  routes_[i].amt = amt; routes_[i].via = via;
  routed_rebuild();
}

#ifdef RACK_FM1
static int nalloc_ = 0;   // voices the arena holds module state for
#define NALLOC nalloc_
#else
#define NALLOC MAXV
#endif
#ifdef RACK_FM1
static uint8_t rings_q_[8]; static int rings_qn_ = 0;   // shared Rings: notes waiting to strum (one per Rings call)
static uint8_t rings_nq_[2][8]; static int rings_nqn_[2], rings_nl_[2] = {-1, -1};   // the notes this block's voices struck, per core (merged after)
#endif
static void reset_voice_modules(int slot) {
  for (int i = 0; i < NALLOC; ++i) {
    if (slot == S_SRC) {
      BufferAllocator alloc(plaits_buf_[i], 16384);
      if (mod_id_[S_SRC] == M_PLAITS) plaits_[i].Init(&alloc);
      if (mod_id_[S_SRC] == M_BRAIDS) braids_[i].Init();
#ifndef RACK_LITE
      if (mod_id_[S_SRC] == M_ELEMENTS) { elements_[i].Init(elements_rev_[i]); }
#endif
    } else if (slot == S_SHP) {
#ifdef RACK_FM1
      // no reverb buffer when the patch can't reach STR+VERB (fm1_fit): Rings' reverb is then never run (the shim's
      // fx_engine.h leaves a null buffer alone), and render_voice keeps the model off STR+VERB
      if (mod_id_[S_SHP] == M_RINGS && i == 0) { rings_[0].Init(rings_rev_ ? rings_rev_[0] : nullptr); rings_[0].set_polyphony(RINGS_POLY); rings_qn_ = 0; rings_nqn_[0] = rings_nqn_[1] = 0; }
#else
      if (mod_id_[S_SHP] == M_RINGS) { rings_[i].Init(rings_rev_[i]); rings_[i].set_polyphony(1); }
#endif
#ifndef RACK_LITE
      if (mod_id_[S_SHP] == M_WARPS) warps_[i].Init(96000.0f);
#endif
      svf_[i].Init(); svf2_[i].Init();
    }
  }
}

static void init_fx(int id) {
#ifndef RACK_LITE
  if (id == M_CLOUDS) {
    clouds_.Init(clouds_large_, CL_LARGE, clouds_small_, CL_SMALL);
    clouds_.set_num_channels(2); clouds_.set_low_fidelity(false); clouds_quality_ = 0;
  }
#endif
  if (id == M_REVERB) { reverb_.Init(reverb_buf_); if (shim_buf_) memset(shim_buf_, 0, 4096 * 2); memset(pre_buf_l_, 0, (pre_mask_ + 1) * 2); memset(pre_buf_r_, 0, (pre_mask_ + 1) * 2); shim_fb_ = 0; }
  if (id == M_DELAY) { memset(delay_l_, 0, dlen_ * sizeof(dsamp_t)); memset(delay_r_, 0, dlen_ * sizeof(dsamp_t)); }
  if (id == M_CHORUS) { memset(chorus_l_, 0, 4096 * sizeof(dsamp_t)); memset(chorus_r_, 0, 4096 * sizeof(dsamp_t)); }
}

#ifdef RACK_FM1
// ------------------------------------------------------------------ the FM-1 memory arena
// The sources, shapers and effects that need big buffers share one block of RAM. fm1_layout() lays it out
// for what is loaded: the effects first, then as many voices as fit (up to MAXV). If even the effects or one
// voice won't fit, it unloads modules (FX 2, FX 1, then the shaper, then the source) rather than overrun.
#if defined(RACK_ARENA_POOL)   // fm1-base and Felucca: big zero-initialised buffers go in the .pool section
#define RACK_ARENA_ATTR __attribute__((section(".pool")))
#elif !defined(RACK_ARENA_ATTR)
#define RACK_ARENA_ATTR
#endif
static uint8_t arena_[RACK_ARENA_KB * 1024] RACK_ARENA_ATTR __attribute__((aligned(16)));
static int req_[NSLOT];           // what the patch asks for
static int want_voices_ = 2, dropped_ = 0;
static size_t arena_used_ = 0;
// What the patch needs of the big buffers (fm1_fit; until then, the full sizes): the delay line's length, the reverb's
// pre-delay lines (a power of two), the shimmer's line, Rings' reverb (STR+VERB only). A parameter that a matrix row
// can move counts at its full range, so nothing a patch can do outgrows its buffer.
static int fit_dlen_ = DLEN, fit_pre_mask_ = 8191;
static bool fit_shim_ = true, fit_rings_rev_ = true;
static void fm1_layout();
static void fit_reset() { fit_dlen_ = DLEN; fit_pre_mask_ = 8191; fit_shim_ = true; fit_rings_rev_ = true; }
static size_t carve(const int* eff, int nv, bool commit) {
  size_t at = 0;
  auto take = [&](size_t n) -> uint8_t* { at = (at + 15) & ~(size_t)15; uint8_t* p = arena_ + at; at += n; return p; };
  for (int s = S_FX1; s <= S_FX2; ++s) {
    int id = eff[s];
#ifndef RACK_LITE
    if (id == M_CLOUDS) { uint8_t* a = take(CL_LARGE); uint8_t* b = take(CL_SMALL); if (commit) { clouds_large_ = a; clouds_small_ = b; } }
#endif
    if (id == M_REVERB) { uint8_t* a = take(32768); uint8_t* b = fit_shim_ ? take(8192) : nullptr;
                          size_t pl = (size_t)(fit_pre_mask_ + 1) * 2; uint8_t* c = take(pl); uint8_t* d = take(pl);
                          if (commit) { reverb_buf_ = (uint16_t*)a; shim_buf_ = (int16_t*)b; pre_buf_l_ = (int16_t*)c; pre_buf_r_ = (int16_t*)d; pre_mask_ = fit_pre_mask_; } }
    if (id == M_DELAY) { uint8_t* a = take(fit_dlen_ * 2); uint8_t* b = take(fit_dlen_ * 2); if (commit) { delay_l_ = (int16_t*)a; delay_r_ = (int16_t*)b; dlen_ = fit_dlen_; delay_max_ = fit_dlen_ - 128; } }
    if (id == M_CHORUS) { uint8_t* a = take(8192); uint8_t* b = take(8192); if (commit) { chorus_l_ = (int16_t*)a; chorus_r_ = (int16_t*)b; } }
  }
  if (nv > 0) {
    if (eff[S_SRC] == M_PLAITS) { uint8_t* a = take(nv * sizeof(plaits::Voice)); uint8_t* b = take(nv * 16384); if (commit) { plaits_ = (plaits::Voice*)a; plaits_buf_ = (char (*)[16384])b; } }
    if (eff[S_SRC] == M_BRAIDS) { uint8_t* a = take(nv * sizeof(braids::MacroOscillator)); if (commit) braids_ = (braids::MacroOscillator*)a; }
#ifndef RACK_LITE
    if (eff[S_SRC] == M_ELEMENTS) { uint8_t* a = take(nv * sizeof(elements::Part)); uint8_t* b = take(nv * 65536); if (commit) { elements_ = (elements::Part*)a; elements_rev_ = (uint16_t (*)[32768])b; } }
#endif
    if (eff[S_SHP] == M_RINGS) { int nr = RINGS_SHARED ? 1 : nv;   // shared: one Rings for all the voices
                                 uint8_t* a = take(nr * sizeof(rings::Part)); uint8_t* b = fit_rings_rev_ ? take(nr * 65536) : nullptr; if (commit) { rings_ = (rings::Part*)a; rings_rev_ = (uint16_t (*)[32768])b; } }
#ifndef RACK_LITE
    if (eff[S_SHP] == M_WARPS) { uint8_t* a = take(nv * sizeof(warps::Modulator)); if (commit) warps_ = (warps::Modulator*)a; }
#endif
  }
  return at;
}
static void fm1_layout() {
  const size_t cap = sizeof(arena_);
  int eff[NSLOT]; for (int s = 0; s < NSLOT; ++s) eff[s] = req_[s];
  dropped_ = 0;
  if (carve(eff, 0, false) > cap && eff[S_FX2]) { eff[S_FX2] = 0; dropped_ |= 1 << S_FX2; }
  if (carve(eff, 0, false) > cap && eff[S_FX1]) { eff[S_FX1] = 0; dropped_ |= 1 << S_FX1; }
  int nv = MAXV;
  while (nv > 0 && carve(eff, nv, false) > cap) --nv;
  if (nv == 0 && eff[S_SHP]) { eff[S_SHP] = 0; dropped_ |= 1 << S_SHP; nv = MAXV; while (nv > 0 && carve(eff, nv, false) > cap) --nv; }
  if (nv == 0 && eff[S_SRC]) { eff[S_SRC] = 0; dropped_ |= 1 << S_SRC; nv = MAXV; }
  arena_used_ = carve(eff, nv, true);
  memset(arena_, 0, arena_used_);   // the modules expect zeroed memory, as static objects are
  nalloc_ = nv;
  for (int i = 0; i < nv; ++i) {   // construct the module objects in place
    if (eff[S_SRC] == M_PLAITS) new (&plaits_[i]) plaits::Voice();
    if (eff[S_SRC] == M_BRAIDS) new (&braids_[i]) braids::MacroOscillator();
#ifndef RACK_LITE
    if (eff[S_SRC] == M_ELEMENTS) new (&elements_[i]) elements::Part();
#endif
    if (eff[S_SHP] == M_RINGS && (!RINGS_SHARED || i == 0)) new (&rings_[i]) rings::Part();
#ifndef RACK_LITE
    if (eff[S_SHP] == M_WARPS) new (&warps_[i]) warps::Modulator();
#endif
  }
  mod_id_[S_SRC] = eff[S_SRC]; mod_id_[S_SHP] = eff[S_SHP]; mod_id_[S_FX1] = eff[S_FX1]; mod_id_[S_FX2] = eff[S_FX2];
  reset_voice_modules(S_SRC); reset_voice_modules(S_SHP);
  init_fx(eff[S_FX1]); init_fx(eff[S_FX2]);
  int n = want_voices_ < nalloc_ ? want_voices_ : nalloc_; if (n < 1) n = 1;
  for (int i = n; i < MAXV; ++i) { vs_[i].gate = false; vs_[i].active = false; vs_[i].stage = 0; vs_[i].env = 0; }
  nvoices_ = n; shrink_to_ = 0;
}
// After a patch has loaded (its modules, parameters and routes): size the big buffers to what it can use and lay the
// arena out again, so the room left goes to voices. The sound is the same, sample for sample, as with the full sizes.
static bool routed(int slot, int param) { return (routed_[slot + 1] >> param & 1u) != 0; }
static void fm1_fit_calc() {
  static const float beats[] = { 0.25f, 0.5f, 0.75f, 1.0f, 2.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.5f, 3.0f };
  int dl = 0, pre = 0; bool shim = false, rrev = false;
  for (int s = S_FX1; s <= S_FX2; ++s) {
    if (req_[s] == M_DELAY) {
      int need;
      if (routed(s, 0) || routed(s, 9) || routed(s, 10) || routed(S_GLO, 0)) need = DLEN;   // its time can move
      else {
        float bpm = base_[S_GLO][0] > 1.0f ? base_[S_GLO][0] : 112.0f;
        int ti = PI(s, 0, 0); ti = ti < 0 ? 0 : ti > 8 ? 8 : ti;
        int samples = PI(s, 9, 0) == 1 ? (int)(0.01f * powf(200.0f, P(s, 10, 0)) * SR) : (int)(beats[ti] * 60.0f / bpm * SR);
        need = samples + 128 + 1;            // + TAPE's wow, as the 1 s line keeps
        if (need > DLEN) need = DLEN;
        if (need < 4096) need = 4096;
      }
      if (need > dl) dl = need;
    }
    if (req_[s] == M_REVERB) {
      int p = routed(s, 5) ? 8191 : (int)(P(s, 5, 0) * 0.15f * SR); if (p > 7999) p = 7999;
      if (p > pre) pre = p;
      if (routed(s, 4) || PI(s, 4, 0) == 2) shim = true;
    }
  }
  if (req_[S_SHP] == M_RINGS && (routed(S_SHP, 0) || PI(S_SHP, 0, 0) == 5)) rrev = true;
  int mask = 63; while (mask < pre) mask = mask * 2 + 1;
  fit_dlen_ = dl ? dl : DLEN; fit_pre_mask_ = mask; fit_shim_ = shim; fit_rings_rev_ = rrev;
}
EXPORT void fm1_fit() {
  fm1_fit_calc();
  if (delay_w_ >= fit_dlen_) delay_w_ = 0;
  pre_w_ &= fit_pre_mask_;
  fm1_layout();
}
// 1 when the parameters and routes now reach past what fm1_fit sized (a longer delay, more pre-delay, SHIMMER,
// STR+VERB): the platform then fits again, between renders
EXPORT int fm1_fit_needed() {
  int dl = fit_dlen_, mask = fit_pre_mask_; bool shim = fit_shim_, rrev = fit_rings_rev_;
  fm1_fit_calc();
  bool more = fit_dlen_ > dl || fit_pre_mask_ > mask || (fit_shim_ && !shim) || (fit_rings_rev_ && !rrev);
  fit_dlen_ = dl; fit_pre_mask_ = mask; fit_shim_ = shim; fit_rings_rev_ = rrev;
  return more ? 1 : 0;
}
// what the platform layer shows: 0 arena bytes used, 1 arena size, 2 voices that fit, 3 voices playing,
// 4 unloaded slots (bit per slot: 0 SRC, 1 SHP, 4 FX1, 5 FX2)
EXPORT int fm1_status(int what) {
  switch (what) { case 0: return (int)arena_used_; case 1: return (int)sizeof(arena_); case 2: return nalloc_; case 3: return nvoices_; case 4: return dropped_; }
  return 0;
}
#endif

EXPORT void set_module(int slot, int id) {
#ifdef RACK_LITE
  // the Lite firmware ships without Elements, Warps, Marbles and Clouds: they play as an empty slot
  if (id == M_ELEMENTS || id == M_WARPS || id == M_MARBLES || id == M_CLOUDS) id = M_NONE;
#endif
#ifdef RACK_FM1
  if (slot == S_SRC || slot == S_SHP || slot == S_FX1 || slot == S_FX2) { if (req_[slot] == id) return; req_[slot] = id; fm1_layout(); return; }
#endif
  if (mod_id_[slot] == id) return;
  mod_id_[slot] = id;
  if (slot == S_SRC || slot == S_SHP) reset_voice_modules(slot);
  if (slot == S_MODA || slot == S_MODB) {
    int k = slot - S_MODA;
    peaks_lfo_[k].Init(); peaks_env_[k].Init(); tides_[k].Init(); mod_prev_gate_[k] = 0;
#ifndef RACK_LITE
    marbles_t_[k].Init(&marbles_stream_, SR); marbles_xy_[k].Init(&marbles_stream_, SR);
    for (int s = 0; s < 6; ++s) marbles_xy_[k].LoadScale(s, marbles::preset_scales[s]);
#endif
    marbles_t_env_[k] = 0.0f; grids_env_[k][0] = grids_env_[k][1] = 0.0f;
    memset(&func_[k], 0, sizeof(func_[k]));
    mod_out_[k * 2] = mod_out_[k * 2 + 1] = 0.0f;
  }
  if (slot == S_FX1 || slot == S_FX2) init_fx(id);
}

#ifdef RACK_FM1
EXPORT void set_hw(int) {}   // always on in this build
#else
EXPORT void set_hw(int on) { delay_max_ = on ? DMAX_FM1 : DLEN - 1; delay_q16_ = on ? 1 : 0; }
#endif
EXPORT void set_voices(int n) {
  n = n < 1 ? 1 : n > MAXV ? MAXV : n;
#ifdef RACK_FM1
  want_voices_ = n; if (n > nalloc_) n = nalloc_ < 1 ? 1 : nalloc_;
#endif
  if (n < nvoices_) {   // a voice still sounding fades out first (cutting it clicked: the FM-1 sheds voices on overload)
    bool fading = false;
    for (int i = n; i < MAXV; ++i) {
      if (i < nvoices_ && vs_[i].active && vs_[i].env > 0.01f) { vs_[i].gate = false; vs_[i].stage = 7; fading = true; }
      else { vs_[i].gate = false; vs_[i].active = false; vs_[i].stage = 0; vs_[i].env = 0; }
    }
    if (fading) { shrink_to_ = n; return; }
  }
  shrink_to_ = 0;
  nvoices_ = n;
}

EXPORT void note_on(float note, float vel) {
  any_gate_ = true;
  int nv = shrink_to_ ? shrink_to_ : nvoices_;
  if (nv == 1) {
    VoiceState& v = vs_[0];
    bool legato = v.gate && legato_;
    v.note = note; v.vel = vel; v.gate = true; v.active = true;   // one voice: the attack goes on from where it is
    if (!legato) { v.retrig = true; v.stage = 1; v.rnd = randf() * 2.0f - 1.0f; }
    v.age = ++age_counter_; last_voice_ = 0;
    return;
  }
  // free voice, else oldest
  int pick = -1; uint32_t oldest = 0xffffffff;
  for (int i = 0; i < nv; ++i) if (!vs_[i].active) { pick = i; break; }
  if (pick < 0) for (int i = 0; i < nv; ++i) if (!vs_[i].gate && vs_[i].age < oldest) { oldest = vs_[i].age; pick = i; }
  if (pick < 0) { oldest = 0xffffffff; for (int i = 0; i < nv; ++i) if (vs_[i].age < oldest) { oldest = vs_[i].age; pick = i; } }
  VoiceState& v = vs_[pick];
  v.age = ++age_counter_;
  if (v.active && v.env > 0.01f) {   // a stolen voice still sounding: fade it out first (a restart at full level clicked)
    v.note = note; v.gate = true; v.stage = 7; v.pend_vel = vel;
  } else {
    v.note = note; v.cur_note = note; v.vel = vel; v.gate = true; v.active = true; v.retrig = true;
    v.stage = 1; v.rnd = randf() * 2.0f - 1.0f;
  }
  v.pan = (pick % 2 ? 1.0f : -1.0f) * (0.3f + 0.7f * (pick / 2) / 2.0f);
  last_voice_ = pick;
}
EXPORT void reset_clock() { clk_reset(); }

EXPORT void note_off(float note) {
  for (int i = 0; i < MAXV; ++i) if (vs_[i].gate && fabsf(vs_[i].note - note) < 0.01f) { vs_[i].gate = false; if (vs_[i].stage != 7) vs_[i].stage = 4; }
  any_gate_ = false;
  for (int i = 0; i < nvoices_; ++i) if (vs_[i].gate) any_gate_ = true;
}
EXPORT void all_off() { for (int i = 0; i < MAXV; ++i) { vs_[i].gate = false; if (vs_[i].stage) vs_[i].stage = 4; } any_gate_ = false; }

static void master_reset();
EXPORT void init() {
#ifdef RACK_FM1
  fit_reset();
#endif
  fx_off_[0] = fx_off_[1] = 0; fx_clear_[0] = fx_clear_[1] = 0;
  memset(base_, 0, sizeof(base_));
  memset(tgt_, 0, sizeof(tgt_)); memset(glide_, 0, sizeof(glide_)); memset(macro_gl_, 0, sizeof(macro_gl_));
  blk_pos_ = BLOCK; memset(blk_l_, 0, sizeof(blk_l_)); memset(blk_r_, 0, sizeof(blk_r_));
  master_reset();   // none of the old patch's block left to play
  for (int s = 0; s < NSLOT; ++s) for (int p = 0; p < NP; ++p) { lo_[s][p] = -1e9f; hi_[s][p] = 1e9f; span_[s][p] = 1.0f; }
  osc_defaults();
  for (int i = 0; i < MAXV; ++i) { memset(&vs_[i], 0, sizeof(VoiceState)); svf_[i].Init(); svf2_[i].Init(); card_[i] = randf() * 2.0f - 1.0f; vdrift_[i] = 0.0f; }
  memset(tune_, 0, sizeof(tune_)); bend_ = 0.0f; vintage_ = 0.0f; legato_ = true;
  for (int i = 0; i < 6; ++i) ext_tgt_[i] = ext_cur_[i] = 0.0f;
  for (int k = 0; k < 2; ++k) { peaks_lfo_[k].Init(); peaks_env_[k].Init(); tides_[k].Init(); tap_period_[k] = 0.5f; }
  for (int i = 0; i < NROUTE; ++i) routes_[i].src = SR_NONE;
  routed_rebuild();
#ifndef RACK_LITE
  marbles_rng_.Init(0x2357bd1);
  marbles_stream_.Init(&marbles_rng_);
#endif
#ifdef RACK_FM1
  fm1_layout();
#endif
}

// ------------------------------------------------------------------ envelopes
static inline float time_coef(float t01, float min_s, float max_s) {
  float secs = min_s * powf(max_s / min_s, t01);
  return 1.0f - expf(-1.0f / (secs * SR / BLOCK) * 4.0f);
}

// AMP params: 0-3 ADSR, 14 DELAY, 15 HOLD, 16 A CURVE, 17 D CURVE, 18 VEL>ATK, 19 KEY>TIME, 20 M SUST, 21 PITCH ENV.
// Every new control is neutral at 0, so older patches render exactly as before.
static inline float stage_secs(float x) { return x * x * 4.0f; }
static float attack_step(float env, float a, float curve) {
  // the attack aims past 1 and stops there: aiming far past is close to linear, aiming just past is punchy
  if (curve == 0.0f) return env + (1.15f - env) * time_coef(a, 0.001f, 6.0f);
  float T = curve > 0.0f ? 1.15f - curve * 0.13f : 1.15f - curve * 3.0f;
  float norm = logf(1.15f / 0.15f) / logf(T / (T - 1.0f));
  return env + (T - env) * fminf(1.0f, time_coef(a, 0.001f, 6.0f) * norm);
}
// one decay/release step toward `tgt` from `top`; returns true once there
static bool fall_step(float& env, float tgt, float c, float curve, float top, float done_at) {
  if (curve < -0.01f) {                       // aim below the target: straighter, nearer linear
    float k = -curve; c *= logf((1.0f + k) / k) / logf(1.0f / done_at);
    float aim = tgt - k * (top - tgt) - k * 0.05f;
    env += (aim - env) * fminf(1.0f, c);
    if (env <= tgt) { env = tgt; return true; }
    return false;
  }
  if (curve > 0.01f) c = fminf(1.0f, c * (1.0f + curve * 4.0f * clampf((env - tgt) / (top - tgt + 1e-4f), 0.0f, 1.0f)));
  env += (tgt - env) * c;
  return tgt <= 0.0f ? env < done_at : fabsf(env - tgt) < done_at;
}
static void run_env(VoiceState& v) {
  float a = P(S_AMP, 0, &v), d = P(S_AMP, 1, &v), s = P(S_AMP, 2, &v), r = P(S_AMP, 3, &v);
  float va = P(S_AMP, 18, &v), kt = P(S_AMP, 19, &v);
  if (va != 0.0f || kt != 0.0f) {
    float k = -kt * clampf((v.note - 60.0f) / 48.0f, -1.0f, 1.0f) * 0.4f;   // higher notes shorter when KEY>TIME is up
    a = clampf(a - va * v.vel * 0.5f + k, 0.0f, 1.0f); d = clampf(d + k, 0.0f, 1.0f); r = clampf(r + k, 0.0f, 1.0f);
  }
  if (v.stage == 7) {   // a stolen voice fading out (about 1.5 ms), then its new note starts from silence
    v.env *= 0.3f;
    if (v.env > 0.003f) return;
    v.env = 0.0f;
    if (!v.gate) { v.stage = 0; v.active = false; return; }   // the new note was already let go: drop it
    v.stage = 1; v.retrig = true; v.vel = v.pend_vel; if (nvoices_ > 1) v.cur_note = v.note; v.rnd = randf() * 2.0f - 1.0f;
  }
  float dly = P(S_AMP, 14, &v), hold = P(S_AMP, 15, &v), ac = P(S_AMP, 16, &v), dc = P(S_AMP, 17, &v);
  if (v.retrig && v.stage == 1 && dly > 0.0005f) { v.stage = 5; v.tm = 0.0f; }
  switch (v.stage) {
    case 5: v.env *= 0.9f; v.tm += BLOCK / SR; if (v.tm >= stage_secs(dly)) v.stage = 1; break;
    case 1: v.env = attack_step(v.env, a, ac); if (v.env >= 1.0f) { v.env = 1.0f; v.stage = hold > 0.0005f ? 6 : 2; v.tm = 0.0f; } break;
    case 6: v.env = 1.0f; v.tm += BLOCK / SR; if (v.tm >= stage_secs(hold)) v.stage = 2; break;
    case 2:
      if (dc == 0.0f) { v.env += (s - v.env) * time_coef(d, 0.005f, 8.0f); if (fabsf(v.env - s) < 0.001f) v.stage = 3; }
      else if (fall_step(v.env, s, time_coef(d, 0.005f, 8.0f), dc, 1.0f, 0.001f)) v.stage = 3;
      break;
    case 3: v.env = s; break;
    case 4:
      if (dc == 0.0f) { v.env += (0.0f - v.env) * time_coef(r, 0.005f, 10.0f); if (v.env < 0.0005f) { v.env = 0.0f; v.stage = 0; v.active = false; } }
      else if (fall_step(v.env, 0.0f, time_coef(r, 0.005f, 10.0f), dc, 1.0f, 0.0005f)) { v.env = 0.0f; v.stage = 0; v.active = false; }
      break;
    default: v.env = 0.0f;
  }
  // mod env (AD, optional loop; with M SUST up it holds while the key is down): AMP params 8 attack, 9 decay, 10 loop, 20 sustain
  float ma = P(S_AMP, 8, &v), md = P(S_AMP, 9, &v); bool loop = PI(S_AMP, 10, &v) == 1;
  float ms = loop ? 0.0f : P(S_AMP, 20, &v);
  if (v.retrig) v.mstage = 1;
  if (v.mstage == 1) { v.menv += (1.15f - v.menv) * time_coef(ma, 0.001f, 6.0f); if (v.menv >= 1.0f) { v.menv = 1.0f; v.mstage = 2; } }
  else if (v.mstage == 2 && ms <= 0.001f) { v.menv += (0.0f - v.menv) * time_coef(md, 0.005f, 8.0f); if (v.menv < 0.002f) { v.menv = 0.0f; v.mstage = loop && v.gate ? 1 : 0; } }
  else if (v.mstage == 2) { v.menv += (ms - v.menv) * time_coef(md, 0.005f, 8.0f); if (!v.gate) v.mstage = 4; else if (fabsf(v.menv - ms) < 0.002f) v.mstage = 3; }
  else if (v.mstage == 3) { v.menv = ms; if (!v.gate) v.mstage = 4; }
  else if (v.mstage == 4) { v.menv += (0.0f - v.menv) * time_coef(md, 0.005f, 8.0f); if (v.menv < 0.002f) { v.menv = 0.0f; v.mstage = 0; } }
}
// the filter's own ADSR (FILTER params 8-11), used when ENV SRC is OWN
static void run_fenv(VoiceState& v) {
  float a = P(S_SHP, 8, &v), d = P(S_SHP, 9, &v), s = P(S_SHP, 10, &v), r = P(S_SHP, 11, &v);
  if (v.retrig) v.fstage = 1;
  if (!v.gate && v.fstage >= 1 && v.fstage <= 3) v.fstage = 4;
  switch (v.fstage) {
    case 1: v.fenv += (1.15f - v.fenv) * time_coef(a, 0.001f, 6.0f); if (v.fenv >= 1.0f) { v.fenv = 1.0f; v.fstage = 2; } break;
    case 2: v.fenv += (s - v.fenv) * time_coef(d, 0.005f, 8.0f); if (fabsf(v.fenv - s) < 0.001f) v.fstage = 3; break;
    case 3: v.fenv = s; break;
    case 4: v.fenv += (0.0f - v.fenv) * time_coef(r, 0.005f, 10.0f); if (v.fenv < 0.0005f) { v.fenv = 0.0f; v.fstage = 0; } break;
    default: v.fenv = 0.0f;
  }
}
// Braids' internal AD (SRC params 10 attack, 11 decay), struck on each new note
static void run_benv(VoiceState& v) {
  if (v.retrig) v.bstage = 1;
  float a = P(S_SRC, 10, &v), d = P(S_SRC, 11, &v);
  if (v.bstage == 1) { v.benv += (1.15f - v.benv) * time_coef(a, 0.001f, 2.0f); if (v.benv >= 1.0f) { v.benv = 1.0f; v.bstage = 2; } }
  else if (v.bstage == 2) { v.benv += (0.0f - v.benv) * time_coef(d, 0.005f, 8.0f); if (v.benv < 0.001f) { v.benv = 0.0f; v.bstage = 0; } }
}

// ------------------------------------------------------------------ mod slots
static float division_gate(int which) {   // 0 NOTE 1 GRID 2 1/16 3 1/8 4 1/4 5 BAR
  static const float div[] = { 0, 0, 1, 2, 4, 16 };
  if (which == 0) return any_gate_ ? 1.0f : 0.0f;
  if (which == 1) return grids_gate_ && fmodf(CLK(), 1.0f) < 0.5f ? 1.0f : 0.0f;
  float p = fmodf(CLK(), div[which]) / div[which];
  return p < 0.5f ? 1.0f : 0.0f;
}


// ------------------------------------------------------------------ FUNC (13 Nov 2026): a function generator after
// Make Noise's Maths, as a MOD module. Two function channels (1 and 4): a rise and a fall (1 ms to 20 s), a curve
// (log, linear, exp), triggered by a key, the keys held, the clock, the other channel's end of cycle or any matrix
// source crossing the middle; CYCLE makes one an LFO, HOLD (AR) keeps it up while its trigger is held. Two input
// channels (2 and 3) scale any matrix source (or, with none, give an offset). A mixer of the four: SUM, INV and OR.
// OUT 1 and OUT 2 each pick one of CH 1, CH 4, SUM, INV, OR, EOR (channel 1 falling) or EOC (channel 4 done).
// Parameters: 0-3 CH 1 RISE FALL CURVE CYCLE | 4-7 CH 4 the same | 8 TRIG 1, 9 TRIG 4, 10 TRIG SRC, 11 HOLD |
// 12 CH 2 SRC, 13 CH 2 AMT, 14 CH 3 SRC, 15 CH 3 AMT | 16-19 LVL 1-4 | 20 OUT 1, 21 OUT 2. Once a block, like Tides.
static float func_shape(float x, float curve) {   // the curve: -1 log (fast then slow), 0 linear, +1 exp (slow then fast)
  if (curve > 0.01f) return powf(x, 1.0f + curve * 3.0f);
  if (curve < -0.01f) return 1.0f - powf(1.0f - x, 1.0f - curve * 3.0f);
  return x;
}
static bool func_trigger(int k, int which, int c, bool* level) {   // a rising edge of channel c's trigger this block
  FuncState& f = func_[k];
  int slot = S_MODA + k;
  bool now = false;
  *level = false;
  switch (which) {
    case 1: now = age_counter_ != f.ages; *level = any_gate_; return now;           // NOTE: each key
    case 2: now = any_gate_; break;                                                   // GATE: keys held
    case 3: case 4: case 5: case 6: now = division_gate(which - 1) > 0.5f; break;    // 1/16 1/8 1/4 BAR
    case 7: return f.ch[1 - c].evt;                                                  // the other's end of cycle
    case 8: now = source_value(PI(slot, 10, 0), &vs_[last_voice_]) > 0.5f; break;    // a matrix source over the middle
    default: return false;
  }
  *level = now;
  bool rise = now && !f.ch[c].trig_prev;
  f.ch[c].trig_prev = now;
  return rise;
}
static void run_func(int k) {
  FuncState& f = func_[k];
  int slot = S_MODA + k;
  const VoiceState* v = &vs_[last_voice_];
  float dt = BLOCK / SR, chv[4];
  bool hold = PI(slot, 11, 0) == 1;
  for (int c = 0; c < 2; ++c) {
    FuncCh& h = f.ch[c];
    int b = c * 4;
    float rise = 0.001f * powf(20000.0f, P(slot, b + 0, 0)), fall = 0.001f * powf(20000.0f, P(slot, b + 1, 0));
    bool cyc = PI(slot, b + 3, 0) == 1, lvl;
    bool trg = func_trigger(k, PI(slot, 8 + c, 0), c, &lvl);
    h.evt = false;
    if (h.eoc > 0.0f) h.eoc -= dt / 0.01f;   // the end-of-cycle gate: 10 ms
    if (trg || (cyc && h.stage == 0)) h.stage = 1;   // a trigger restarts the rise from where it is (as Maths)
    if (h.stage == 1) { h.x += dt / rise; if (h.x >= 1.0f) { h.x = 1.0f; h.stage = hold && lvl ? 3 : 2; } }
    else if (h.stage == 3) { if (!lvl) h.stage = 2; }
    else if (h.stage == 2) { h.x -= dt / fall; if (h.x <= 0.0f) { h.x = 0.0f; h.stage = 0; h.eoc = 1.0f; h.evt = true; } }
    chv[c ? 3 : 0] = func_shape(h.x, P(slot, b + 2, 0));
  }
  f.ages = age_counter_;
  for (int c = 0; c < 2; ++c) {   // channels 2 and 3: a source scaled, or an offset with none
    int src = PI(slot, 12 + c * 2, 0);
    float amt = P(slot, 13 + c * 2, 0);
    chv[1 + c] = src ? source_value(src, v) * amt : amt;
  }
  float sum = 0.0f, orv = 0.0f;
  for (int i = 0; i < 4; ++i) {
    float x = chv[i] * P(slot, 16 + i, 0);
    sum += x;
    if (x > orv) orv = x;
  }
  sum = clampf(sum, -1.5f, 1.5f);
  float outs[7] = { chv[0], chv[3], sum, -sum, orv, f.ch[0].stage == 2 ? 1.0f : 0.0f, f.ch[1].eoc > 0.0f ? 1.0f : 0.0f };
  float* o = &mod_out_[k * 2];
  for (int j = 0; j < 2; ++j) { int w = PI(slot, 20 + j, 0); o[j] = outs[w < 0 ? 0 : w > 6 ? 6 : w]; }
}
static void run_mod(int k) {
  int slot = S_MODA + k;
  int id = mod_id_[slot];
  float* o = &mod_out_[k * 2];
  if (id == M_FUNC) {
    run_func(k);
  } else if (id == M_PEAKS) {
    int func = PI(slot, 0, 0);
    float k1 = P(slot, 1, 0), k2 = P(slot, 2, 0);
    bool g = division_gate(PI(slot, 3, 0)) > 0.5f;
    GateFlags flags[BLOCK]; int16_t out[BLOCK];
    for (int i = 0; i < BLOCK; ++i) { flags[i] = ExtractGateFlags(mod_prev_gate_[k], g); mod_prev_gate_[k] = flags[i]; }
    if (func == 0) {
      peaks_env_[k].set_adsr((uint16_t)(k1 * 65535), (uint16_t)(k2 * 65535), (uint16_t)(P(slot, 4, 0) * 65535), (uint16_t)(k2 * 65535));
      peaks_env_[k].Process(flags, out, BLOCK);
      o[0] = out[BLOCK - 1] / 32767.0f; o[1] = o[0];
      if (PI(slot, 5, 0) == 1) { o[0] = 1.0f - o[0]; o[1] = o[0]; }
    } else {
      peaks_lfo_[k].set_sync(func == 2);
      peaks_lfo_[k].set_rate((uint16_t)(k1 * 65535));
      peaks_lfo_[k].set_shape_parameter_preset((uint16_t)(k2 * 65535));
      peaks_lfo_[k].Process(flags, out, BLOCK);
      o[0] = out[BLOCK - 1] / 32767.0f; o[1] = o[0] * 0.5f + 0.5f;
      if (PI(slot, 5, 0) == 1) { o[0] = -o[0]; o[1] = 1.0f - o[1]; }
    }
  } else if (id == M_TIDES) {
    int mode = PI(slot, 0, 0);                // AD LOOP AR
    float freq = P(slot, 1, 0), shape = P(slot, 2, 0), slope = P(slot, 3, 0);
    float smooth = P(slot, 4, 0); int range = PI(slot, 5, 0);
    bool g = division_gate(PI(slot, 6, 0)) > 0.5f;
    float hz = (range == 0 ? 0.02f : range == 1 ? 0.25f : 2.0f) * powf(2.0f, freq * 8.0f);
    GateFlags flags[BLOCK];
    for (int i = 0; i < BLOCK; ++i) { flags[i] = ExtractGateFlags(mod_prev_gate_[k], g); mod_prev_gate_[k] = flags[i]; }
    tides::PolySlopeGenerator::OutputSample out[BLOCK];
    tides::RampMode rm = mode == 0 ? tides::RAMP_MODE_AD : mode == 1 ? tides::RAMP_MODE_LOOPING : tides::RAMP_MODE_AR;
#ifdef RACK_FM1
    // the FM-1: only the block's last value is used (a modulator), so Tides runs once a block, at the block rate
    // (2 kHz), not 24 times: 16 % of a core down to about 1 % (measured 8 Nov 2026). The gate's edge lands on the
    // block, as the gate itself is read once a block
    GateFlags f1[1] = { flags[0] };
    for (int i = 1; i < BLOCK; ++i) f1[0] |= flags[i] & (GATE_FLAG_RISING | GATE_FLAG_FALLING);
    f1[0] = (GateFlags)((f1[0] & ~GATE_FLAG_HIGH) | (flags[BLOCK - 1] & GATE_FLAG_HIGH));
    tides_[k].Render(rm, tides::OUTPUT_MODE_GATES, tides::RANGE_CONTROL,
        hz / SR * BLOCK, clampf(slope * 0.5f + 0.5f, 0.01f, 0.99f), shape, smooth * 0.5f + 0.5f, 1.0f,
        f1, NULL, out + BLOCK - 1, 1);
#else
    tides_[k].Render(rm, tides::OUTPUT_MODE_GATES, tides::RANGE_CONTROL,
        hz / SR, clampf(slope * 0.5f + 0.5f, 0.01f, 0.99f), shape, smooth * 0.5f + 0.5f, 1.0f,
        flags, NULL, out, BLOCK);
#endif
    tides_raw_ = out[BLOCK - 1].channel[0];
    float raw = out[BLOCK - 1].channel[0];
    float u = clampf(mode == 1 ? raw / 4.5f * 0.5f + 0.5f : raw / TIDES_AD_MAX, 0.0f, 1.0f);
    if (PI(slot, 7, 0) == 1) u = 1.0f - u;   // INVERT
    o[1] = u;                    // TIDE+ unipolar
    o[0] = u * 2.0f - 1.0f;      // TIDES bipolar
#ifndef RACK_LITE
  } else if (id == M_MARBLES) {
    // RATE SPREAD BIAS STEPS | DEJA VU LENGTH JITTER CLOCK
    static const int lengths[] = { 1, 2, 3, 4, 5, 6, 8, 16 };
    static const float divs[] = { 0, 1, 2, 4 };
    int clock = PI(slot, 7, 0);
    float dv = P(slot, 4, 0);
    marbles::TGenerator& t = marbles_t_[k];
    t.set_model((marbles::TGeneratorModel)PI(slot, 9, 0));
    t.set_range(marbles::T_GENERATOR_RANGE_1X);
    t.set_rate((P(slot, 0, 0) - 0.5f) * 72.0f);
    t.set_bias(P(slot, 2, 0)); t.set_jitter(P(slot, 6, 0));
    t.set_deja_vu(dv); t.set_length(lengths[PI(slot, 5, 0)]);
    t.set_pulse_width_mean(0.5f + P(slot, 11, 0) * 0.45f); t.set_pulse_width_std(0.0f);
    float rb[BLOCK * 4]; bool gates[BLOCK * 2]; float volts[BLOCK * 4];
    marbles::Ramps ramps; ramps.master = rb; ramps.external = rb + BLOCK; ramps.slave[0] = rb + BLOCK * 2; ramps.slave[1] = rb + BLOCK * 3;
    GateFlags ext[BLOCK];
    bool ext_gate = clock ? (fmodf(CLK(), divs[clock]) / divs[clock]) < 0.5f : false;
    for (int i = 0; i < BLOCK; ++i) { ext[i] = ExtractGateFlags(mod_prev_gate_[k], ext_gate); mod_prev_gate_[k] = ext[i]; }
    t.Process(clock != 0, ext, ramps, gates, BLOCK);
    marbles::GroupSettings x, y;
    x.control_mode = marbles::CONTROL_MODE_IDENTICAL; x.voltage_range = (marbles::VoltageRange)PI(slot, 10, 0);
    x.register_mode = false; x.register_value = 0.0f;
    x.spread = P(slot, 1, 0); x.bias = P(slot, 2, 0); x.steps = P(slot, 3, 0); x.deja_vu = dv;
    x.scale_index = PI(slot, 8, 0); x.length = lengths[PI(slot, 5, 0)]; x.ratio.p = 1; x.ratio.q = 1;
    y = x; y.deja_vu = 0.0f; y.length = 1;
    marbles_xy_[k].Process(marbles::CLOCK_SOURCE_INTERNAL_T1_T2_T3, x, y, ext, ramps, volts, BLOCK);
    o[0] = volts[(BLOCK - 1) * 4 + 0];            // X1 in volts, 0..2 V: 1.0 = one octave on PITCH
    bool tg = gates[(BLOCK - 1) * 2];
    marbles_t_env_[k] = tg ? 1.0f : marbles_t_env_[k] * 0.9f;
    o[1] = marbles_t_env_[k];
#endif
  } else if (id == M_GRIDS) {
    // X Y FILL OUT | CHAOS

    int step = CLK_I() & 31;
    float* env = grids_env_[k];
    {
      static int last[2] = { -1, -1 };
      if (step != last[k]) {
        last[k] = step;
        uint8_t xx = (uint8_t)(P(slot, 0, 0) * 255.0f), yy = (uint8_t)(P(slot, 1, 0) * 255.0f);
        uint8_t density = (uint8_t)(P(slot, 2, 0) * 255.0f), chaos = (uint8_t)(P(slot, 4, 0) * 255.0f);
        int out = PI(slot, 3, 0);
        bool hit = false, accent = false;
        for (int inst = 0; inst < 3; ++inst) {
          if (out < 3 && inst != out) continue;
          int level = grids_level(slot, inst, step);
          level += (int)((randf() * 255.0f) * chaos / 255.0f / 4.0f);
          if (level > 255) level = 255;
          if (level > (255 - density)) { hit = true; if (level > 192) accent = true; }
        }
        if (hit) { env[0] = 1.0f; if (accent) env[1] = 1.0f; }
        grids_gate_ = hit;
      }
    }
    o[0] = env[0]; o[1] = env[1];
    float gd = P(slot, 5, 0), gk = gd == 0.0f ? 0.993f : 1.0f - 0.007f * powf(2.0f, -gd * 3.0f);   // DECAY: ~70 ms at 0
    env[0] *= gk; env[1] *= gk;
  } else {
    o[0] = o[1] = 0.0f;
  }
}

static void run_lfos() {
  static const float shapes_div[] = { 0, 1, 2, 4, 8, 16, 32 };   // OFF 1/16 1/8 1/4 1/2 1BAR 2BARS
  bool any = any_gate_;
  bool rising = any && !lfo_any_gate_prev_;
  lfo_any_gate_prev_ = any;
  if (rising) lfo_since_gate_ = 0.0f; else lfo_since_gate_ += BLOCK / SR;
  for (int l = 0; l < 3; ++l) {
    // LFO params 12 + l*4: FADE, PHASE, POLARITY (BI/UNI), ONE SHOT
    float fade = P(S_LFO, 12 + l * 4, 0), phase = P(S_LFO, 13 + l * 4, 0);
    bool uni = PI(S_LFO, 14 + l * 4, 0) == 1, one = PI(S_LFO, 15 + l * 4, 0) == 1;
    int shape = PI(S_LFO, l * 4 + 0, 0);
    float rate = P(S_LFO, l * 4 + 1, 0);
    int sync = PI(S_LFO, l * 4 + 2, 0);
    bool retrig = PI(S_LFO, l * 4 + 3, 0) == 1;
    float ph;
    if (sync > 0) {
      ph = fmodf(CLK(), shapes_div[sync]) / shapes_div[sync];
    } else {
      float hz = 0.03f * powf(2.0f, rate * 10.0f);     // 0.03 .. 30 Hz
      if (one && rising) { lfo_phase_[l] = 0.0f; lfo_done_[l] = false; }
      if (!(one && lfo_done_[l])) lfo_phase_[l] += hz * BLOCK / SR;
      if (retrig && rising) lfo_phase_[l] = 0.0f;
      if (one && lfo_phase_[l] >= 1.0f) { lfo_done_[l] = true; lfo_phase_[l] = 0.9999f; }
      lfo_phase_[l] -= floorf(lfo_phase_[l]);
      ph = lfo_phase_[l];
    }
    if (phase != 0.0f) { ph += phase; ph -= floorf(ph); }
    bool wrapped = ph < lfo_sh_prev_[l];
    lfo_sh_prev_[l] = ph;
    if (wrapped) lfo_sh_[l] = randf() * 2.0f - 1.0f;
    float v;
    switch (shape) {
      case 0: v = sinf(ph * 6.2831853f); break;
      case 1: v = ph < 0.5f ? ph * 4.0f - 1.0f : 3.0f - ph * 4.0f; break;
      case 2: v = 1.0f - ph * 2.0f; break;
      case 3: v = ph < 0.5f ? 1.0f : -1.0f; break;
      case 4: v = lfo_sh_[l]; break;
      default: v = lfo_raw_[l] + (lfo_sh_[l] - lfo_raw_[l]) * 0.05f; break;
    }
    lfo_raw_[l] = v;
    if (uni) v = v * 0.5f + 0.5f;
    if (fade > 0.001f) v *= fminf(1.0f, lfo_since_gate_ / stage_secs(fade) * 0.5f);
    lfo_val_[l] = v;
  }
}

// ------------------------------------------------------------------ OSC: a plain oscillator per voice, mixed with the source
// Its parameters ride on AMP's spare ones (24-30), so presets, the matrix and the monitor reach them as AMP's:
//   24 WAVE (SINE TRI SAW SQUARE PULSE NOISE)  25 OCTAVE (-3..2)  26 SEMI (-12..12)  27 FINE (-1..1 semitone)
//   28 PW (0.05-0.95)  29 OSC LVL (0: off, the default)  30 MAIN LVL (the source's level, 1 the default)
// With OSC LVL at 0 and MAIN LVL at 1 a preset sounds exactly as before.
enum { OSC_WAVE = 24, OSC_OCT, OSC_SEMI, OSC_FINE, OSC_PW, OSC_LVL, OSC_MAIN, OSC_AUX };   // OSC_AUX: Plaits' AUX into the mix (0 off)
static float osc_ph_[MAXV];
static uint32_t osc_rng_[MAXV];
static void osc_defaults() {
  static const float LO[8] = {0, -3, -12, -1, 0.05f, 0, 0, 0}, HI[8] = {5, 2, 12, 1, 0.95f, 1, 1, 1};
  for (int k = 0; k < 8; ++k) { lo_[S_AMP][OSC_WAVE + k] = LO[k]; hi_[S_AMP][OSC_WAVE + k] = HI[k]; span_[S_AMP][OSC_WAVE + k] = 1.0f; }
  base_[S_AMP][OSC_PW] = 0.5f; base_[S_AMP][OSC_MAIN] = 1.0f;
}
static inline float osc_blep(float t, float dt) {   // PolyBLEP: the step's corner rounded off over one sample
  if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
  return 0.0f;
}
static void osc_mix(int i, const VoiceState& v, float note, float* buf, float* aux, bool stereo, const float* paux = nullptr) {
  float lvl = P(S_AMP, OSC_LVL, &v), main = P(S_AMP, OSC_MAIN, &v);
  if (main < 0.9999f) for (int n = 0; n < BLOCK; ++n) { buf[n] *= main; if (stereo) aux[n] *= main; }
  float al = paux ? P(S_AMP, OSC_AUX, &v) : 0.0f;   // Plaits' AUX output, mixed in on its own level
  if (al > 0.0001f) for (int n = 0; n < BLOCK; ++n) { buf[n] += paux[n] * al; if (stereo) aux[n] += paux[n] * al; }
  if (lvl <= 0.0001f) return;
  int wave = PI(S_AMP, OSC_WAVE, &v);
  float on = note + floorf(P(S_AMP, OSC_OCT, &v) + 0.5f) * 12.0f + floorf(P(S_AMP, OSC_SEMI, &v) + 0.5f) + P(S_AMP, OSC_FINE, &v);
  float dt = clampf(440.0f * SemitonesToRatio(on - 69.0f) / SR, 0.00001f, 0.45f);
  float pw = wave == 3 ? 0.5f : P(S_AMP, OSC_PW, &v), ph = osc_ph_[i], g = lvl * 0.5f;
  uint32_t r = osc_rng_[i] ? osc_rng_[i] : 0x9E3779B9u + (uint32_t)i;
  for (int n = 0; n < BLOCK; ++n) {
    float s;
    switch (wave) {
      case 0: { float y = ph < 0.5f ? 16.0f * ph * (0.5f - ph) : -16.0f * (ph - 0.5f) * (1.0f - ph);
                s = 0.225f * (y * fabsf(y) - y) + y; break; }                        // sine (Bhaskara, refined)
      case 1: s = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;              // triangle
      case 2: s = 2.0f * ph - 1.0f - osc_blep(ph, dt); break;                          // saw
      case 5: r ^= r << 13; r ^= r >> 17; r ^= r << 5; s = (float)(int32_t)r * (1.0f / 2147483648.0f); break;   // noise
      default: { float q = ph + 1.0f - pw; q -= floorf(q);                            // square, pulse
                 s = (ph < pw ? 1.0f : -1.0f) + osc_blep(ph, dt) - osc_blep(q, dt); }
    }
    ph += dt; if (ph >= 1.0f) ph -= 1.0f;
    buf[n] += s * g;
    if (stereo) aux[n] += s * g;
  }
  osc_ph_[i] = ph; osc_rng_[i] = r;
}

// ------------------------------------------------------------------ voice render
// each core's own scratch (RACK_FM1's second core renders some voices: render_voice(.., core 1))
static float vbuf_c_[2][BLOCK], vaux_c_[2][BLOCK];
static float paux_c_[2][BLOCK];   // Plaits' AUX output, for OSC's mixer (AUX LVL)

#ifdef RACK_FM1
// One shared Rings (the FM-1, 8 Nov 2026), as on the hardware module: every voice feeds its source, enveloped by AMP,
// into one Rings, which plays RINGS_POLY notes itself (each note strums its next resonator). Rings costs about one
// voice's worth whatever the number of voices, and its memory is taken once. The heavy models (MODAL, SYMPATH,
// SYM QNT: 55-58 % of a core for one voice, measured) play as STRING here; the menus only offer STRING, FM VOICE and
// STR+VERB. Rings' parameters follow the last note struck (their per-voice modulation becomes that voice's).
static float rings_in_[2][BLOCK];          // each core's voices' excitation
// Rings and the reverb run a block (0.5 ms) behind on the FM-1, on the first core while the second renders the voices
// (12 Nov 2026: with them after the voices, the second core saved Rings presets almost nothing, bench 6): Rings rings
// last block's excitation, the reverb last block's input
static float rings_ex_[BLOCK];             // last block's excitation (both cores)
static float rv_in_l_[BLOCK], rv_in_r_[BLOCK], rv_wet_l_[BLOCK], rv_wet_r_[BLOCK];
static int rv_early_ = -1;                 // the slot whose reverb runs a block behind (-1: none)
static float rings_dry_g_ = 1.0f;          // what the voices keep of their dry signal (1 - MIX)
static int rings_last_ = 0;                // the voice struck last: Rings' parameters are read with it
static float rings_note_ = 60.0f;
static int rings_fm1_model(int m) { return m == 0 || m == 1 || m == 4 ? 2 : m; }   // MODAL SYMPATH SYM QNT: STRING
#endif

static void render_voice(int i, float* mix_l, float* mix_r, int core = 0) {
  float* const vbuf_ = vbuf_c_[core];
  float* const vaux_ = vaux_c_[core];
  VoiceState& v = vs_[i];
  if (!v.active && v.stage == 0) { v.gl0 = v.gr0 = 0.0f; return; }
  run_env(v);

  // glide (mono only)
  float glide = P(S_AMP, 5, &v);
  if (nvoices_ == 1 && glide > 0.001f) v.cur_note += (v.note - v.cur_note) * (1.0f - powf(glide, 0.08f) * 0.995f);
  else if (v.stage != 7) v.cur_note = v.note;   // a fading stolen voice keeps its old pitch
  int ti = (int)(v.cur_note + 0.5f); ti = ti < 0 ? 0 : ti > 127 ? 127 : ti;
  float vint = 0.0f;
  if (vintage_ > 0.0f) {   // each voice its own small tuning error, a slow drift, and each note a little of its own
    vdrift_[i] += (randf() * 2.0f - 1.0f) * 0.01f; vdrift_[i] *= 0.999f;
    vint = vintage_ * (card_[i] * 0.06f + clampf(vdrift_[i], -1.0f, 1.0f) * 0.05f + v.rnd * 0.02f);
  }
  float note = v.cur_note + tune_[ti] + bend_ + vint + mod_sum(-1, 0, &v) * 12.0f;
  float penv = P(S_AMP, 21, &v); if (penv != 0.0f) note += v.menv * penv * 24.0f;   // PITCH ENV: the mod env bends the pitch

  // ---- SRC
  int src = mod_id_[S_SRC];
  const float* paux = nullptr;
  if (src == M_PLAITS) {
    plaits::Patch p; plaits::Modulations m;
    memset(&p, 0, sizeof(p)); memset(&m, 0, sizeof(m));
    p.engine = PI(S_SRC, 0, &v);
    p.note = note; p.harmonics = P(S_SRC, 1, &v); p.timbre = P(S_SRC, 2, &v); p.morph = P(S_SRC, 3, &v);
    // page 3-4: OCTAVE, FINE, and how far the internal decay envelope moves TIMBRE, MORPH and pitch (as Plaits' attenuverters do with TRIG patched)
    float oct = P(S_SRC, 8, &v), fine = P(S_SRC, 9, &v); if (oct != 0.0f || fine != 0.0f) p.note += floorf(oct + 0.5f) * 12.0f + fine;
    p.timbre_modulation_amount = P(S_SRC, 10, &v); p.morph_modulation_amount = P(S_SRC, 11, &v);
    p.frequency_modulation_amount = P(S_SRC, 12, &v);
    float aux = P(S_SRC, 13, &v);
    bool lpg = PI(S_SRC, 4, &v) == 1;
    p.decay = P(S_SRC, 5, &v); p.lpg_colour = P(S_SRC, 6, &v);
    m.trigger_patched = true;
    m.level_patched = !lpg; m.level = 0.4f + 0.6f * v.vel;
    plaits::Voice::Frame fr[BLOCK];
    // two calls of 12 (Plaits' kBlockSize): its envelopes, LPG and trigger delay step once a call and are timed for
    // 12 frames (one call of 24, tried on the FM-1 in update 18, halved them and wasn't faster: benchmark 5)
    for (int h = 0; h < BLOCK / 12; ++h) {
      m.trigger = (v.retrig && h == 0) || !v.gate ? 0.0f : 1.0f;   // gate level; a retrigger drops it for 12 frames
      plaits_[i].Render(p, m, fr + h * 12, 12);
    }
    float lvl = P(S_SRC, 7, &v);
    if (aux > 0.0f) for (int n = 0; n < BLOCK; ++n) vbuf_[n] = (fr[n].out * (1.0f - aux) + fr[n].aux * aux) / 32768.0f * lvl;
    else for (int n = 0; n < BLOCK; ++n) vbuf_[n] = fr[n].out / 32768.0f * lvl;
    if (P(S_AMP, OSC_AUX, &v) > 0.0001f) { for (int n = 0; n < BLOCK; ++n) paux_c_[core][n] = fr[n].aux / 32768.0f * lvl; paux = paux_c_[core]; }
  } else if (src == M_BRAIDS) {
    braids::MacroOscillator& b = braids_[i];
    b.set_shape((braids::MacroOscillatorShape)PI(S_SRC, 0, &v));
    v.drift += (randf() * 2.0f - 1.0f) * 0.02f; v.drift *= 0.995f;
    float n2 = note + v.drift * P(S_SRC, 3, &v) * 0.6f;
    // page 3-4: OCTAVE, FINE, and the internal AD envelope (as Braids' |\ settings) on TIMBRE, COLOR, pitch and level
    run_benv(v);
    float oct = P(S_SRC, 8, &v), fine = P(S_SRC, 9, &v), et = P(S_SRC, 12, &v), ec = P(S_SRC, 13, &v), ep = P(S_SRC, 14, &v);
    if (oct != 0.0f || fine != 0.0f || ep != 0.0f) n2 += floorf(oct + 0.5f) * 12.0f + fine + v.benv * ep * 24.0f;
    b.set_pitch((int16_t)clampf((n2 - 12.0f) * 128.0f, 0, 16383));   // Braids runs at 96 kHz natively
    float tb = P(S_SRC, 1, &v), cl = P(S_SRC, 2, &v);
    if (et != 0.0f) tb = clampf(tb + v.benv * et, 0.0f, 1.0f);
    if (ec != 0.0f) cl = clampf(cl + v.benv * ec, 0.0f, 1.0f);
    b.set_parameters((int16_t)(tb * 32767), (int16_t)(cl * 32767));
    bool vca = PI(S_SRC, 15, &v) == 1;
    if (v.retrig) b.Strike();
    int16_t buf[BLOCK]; uint8_t sync[BLOCK]; memset(sync, 0, sizeof(sync));
    for (int h = 0; h < BLOCK; h += 24) b.Render(sync + h, buf + h, BLOCK - h < 24 ? BLOCK - h : 24);   // Braids' largest call: 24
    static const int bits_tab[] = { 2, 3, 4, 6, 8, 12, 16 };
    static const int rate_div[] = { 12, 6, 3, 2, 1, 1 };
    int bits = bits_tab[PI(S_SRC, 4, &v)], div = rate_div[PI(S_SRC, 5, &v)];
    float sign = P(S_SRC, 6, &v);
    int16_t held = buf[0];
    for (int n = 0; n < BLOCK; ++n) {
      if (n % div == 0) held = buf[n];
      int32_t s = held;
      if (bits < 16) { int sh = 16 - bits; s = (s >> sh) << sh; }
      float f = s / 32768.0f;
      if (sign > 0.01f) f = f * (1.0f - sign) + sign * f * fabsf(f) * 1.6f;
      vbuf_[n] = f * P(S_SRC, 7, &v);
    }
    if (vca) for (int n = 0; n < BLOCK; ++n) vbuf_[n] *= v.benv;
#ifndef RACK_LITE
  } else if (src == M_ELEMENTS) {
    elements::Patch* ep = elements_[i].mutable_patch();
    ep->exciter_bow_level = P(S_SRC, 0, &v); ep->exciter_blow_level = P(S_SRC, 1, &v);
    ep->exciter_strike_level = P(S_SRC, 2, &v); ep->exciter_envelope_shape = P(S_SRC, 3, &v);
    ep->resonator_geometry = P(S_SRC, 4, &v); ep->resonator_brightness = P(S_SRC, 5, &v);
    ep->resonator_damping = P(S_SRC, 6, &v); ep->resonator_position = P(S_SRC, 7, &v);
    ep->exciter_blow_meta = P(S_SRC, 8, &v); ep->exciter_strike_meta = P(S_SRC, 9, &v);
    ep->exciter_bow_timbre = P(S_SRC, 10, &v); ep->space = P(S_SRC, 11, &v) * 2.0f;
    ep->exciter_blow_timbre = 0.5f + P(S_SRC, 14, &v) * 0.5f;
    ep->exciter_strike_timbre = clampf(ep->resonator_brightness + P(S_SRC, 15, &v), 0.0f, 1.0f);
    ep->exciter_signature = 0.0f; ep->resonator_modulation_frequency = 0.0f; ep->resonator_modulation_offset = 0.0f;
    ep->reverb_diffusion = 0.625f; ep->reverb_lp = 0.7f; ep->modulation_frequency = 0.0f;
    elements::PerformanceState ps;
    float eoct = P(S_SRC, 12, &v), efine = P(S_SRC, 13, &v);
    ps.gate = v.gate; ps.note = (eoct != 0.0f || efine != 0.0f ? note + floorf(eoct + 0.5f) * 12.0f + efine : note) - 7.02f;   // Elements runs at 32 kHz natively
    ps.modulation = 0.0f; ps.strength = 0.4f + 0.6f * v.vel;
    float zero[12] = { 0 }, a[12], b2[12];
    for (int h = 0; h < BLOCK / 12; ++h) {
      elements_[i].Process(ps, zero, zero, a, b2, 12);
      for (int n = 0; n < 12; ++n) { vbuf_[h * 12 + n] = a[n] * 2.4f; vaux_[h * 12 + n] = b2[n] * 2.4f; }
    }
#endif
  } else {
    memset(vbuf_, 0, BLOCK * sizeof(float));
  }
  bool stereo = src == M_ELEMENTS;
  osc_mix(i, v, note, vbuf_, vaux_, stereo, paux);   // OSC, the source's MAIN LVL and Plaits' AUX LVL
  if (!stereo) memcpy(vaux_, vbuf_, BLOCK * sizeof(float));

  // ---- SHP
  int shp = mod_id_[S_SHP];
#ifdef RACK_FM1
  if (shp == M_RINGS) {   // shared Rings: this voice's excitation goes to it (after AMP's envelope), its dry part on
    float vel_amt = P(S_AMP, 4, &v), g = v.env * (1.0f - vel_amt + vel_amt * v.vel) * P(S_SHP, 5, &v) * 2.0f;
    if (v.retrig) { if (rings_nqn_[core] < 8) rings_nq_[core][rings_nqn_[core]++] = (uint8_t)clampf(note + 0.5f, 0.0f, 127.0f); rings_nl_[core] = i; }
    for (int n = 0; n < BLOCK; ++n) { rings_in_[core][n] += vbuf_[n] * g; vbuf_[n] *= rings_dry_g_; vaux_[n] *= rings_dry_g_; }
  } else
#endif
  if (shp == M_RINGS) {
    rings::Part& r = rings_[i];
    int rmodel = PI(S_SHP, 0, &v);
#ifdef RACK_FM1
    if (rmodel == 5 && !rings_rev_) rmodel = 2;   // STR+VERB without its buffer (fm1_fit found it unreachable): STRING
#endif
    r.set_model((rings::ResonatorModel)rmodel);
    rings::Patch rp; rp.structure = P(S_SHP, 1, &v); rp.brightness = P(S_SHP, 2, &v);
    rp.damping = P(S_SHP, 3, &v); rp.position = P(S_SHP, 4, &v);
    rings::PerformanceState ps;
    // page 3: TUNE, FINE, INT EXC (Rings' own pluck, so it rings with no source) and CHORD (the SYM QNT chord table)
    float rt = P(S_SHP, 8, &v), rf = P(S_SHP, 9, &v);
    ps.strum = v.retrig; ps.internal_exciter = PI(S_SHP, 10, &v) == 1; ps.internal_strum = false; ps.internal_note = false;
    ps.tonic = 0.0f; ps.note = rt != 0.0f || rf != 0.0f ? note + floorf(rt + 0.5f) + rf : note; ps.fm = 0.0f; ps.chord = PI(S_SHP, 11, &v);
    float excite = P(S_SHP, 5, &v) * 2.0f, mix = P(S_SHP, 6, &v), spread = P(S_SHP, 7, &v);
    float in[BLOCK], o1[BLOCK], o2[BLOCK];
    for (int n = 0; n < BLOCK; ++n) in[n] = vbuf_[n] * excite;
    r.Process(ps, rp, in, o1, o2, BLOCK);
    for (int n = 0; n < BLOCK; ++n) {
      float c = (o1[n] + o2[n]) * 0.5f;
      float l = c + (o1[n] - c) * spread, rr = c + (o2[n] - c) * spread;
      vbuf_[n] = vbuf_[n] * (1.0f - mix) + l * mix;
      vaux_[n] = vaux_[n] * (1.0f - mix) + rr * mix;
    }
#ifndef RACK_LITE
  } else if (shp == M_WARPS) {
    // ALGO TIMBRE CARRIER MOD IN | CAR LVL MOD LVL MIX
    warps::Modulator& w = warps_[i];
    warps::Parameters* wp = w.mutable_parameters();
    int mod_in = PI(S_SHP, 3, &v);                 // 0 OSC, 1 USB IN, 2 NOISE
    wp->modulation_algorithm = P(S_SHP, 0, &v);
    wp->modulation_parameter = P(S_SHP, 1, &v);
    wp->carrier_shape = mod_in == 0 ? 1 + PI(S_SHP, 2, &v) : 0;
    float ct = P(S_SHP, 7, &v);
    wp->note = (ct != 0.0f ? note + floorf(ct + 0.5f) : note) - 12.0f;   // Warps runs at 96 kHz natively; CAR TUNE
    wp->channel_drive[0] = P(S_SHP, 4, &v); wp->channel_drive[1] = P(S_SHP, 5, &v);
    wp->frequency_shift_pot = 0.5f; wp->frequency_shift_cv = 0.0f; wp->phase_shift = 0.0f;
    float mix = mod_in == 1 ? 0.0f : P(S_SHP, 6, &v);   // USB IN: no audio input in the browser, pass the source through
    warps::ShortFrame in[BLOCK], out[BLOCK];
    for (int n = 0; n < BLOCK; ++n) {
      float srcs = clampf(vbuf_[n], -1.0f, 1.0f);
      float other = mod_in == 2 ? (randf() * 2.0f - 1.0f) * 0.5f : 0.0f;   // USB IN is silent in the browser
      if (mod_in == 0) { in[n].l = 0; in[n].r = (int16_t)(srcs * 32767.0f); }
      else { in[n].l = (int16_t)(srcs * 32767.0f); in[n].r = (int16_t)(other * 32767.0f); }
    }
    w.Process(in, out, BLOCK);
    for (int n = 0; n < BLOCK; ++n) {
      float a = out[n].l / 32768.0f, b = out[n].r / 32768.0f;
      vbuf_[n] = vbuf_[n] * (1.0f - mix) + a * mix;
      vaux_[n] = vaux_[n] * (1.0f - mix) + b * mix;
    }
#endif
  } else if (shp == M_FILTER) {
    // TYPE CUTOFF RESO DRIVE | ENV AMT KEY TRK VEL AMT ENV SRC | F ATK F DEC F SUS F REL | SAT AUDIO FM GAIN
    int type = PI(S_SHP, 0, &v);
    float cutoff = P(S_SHP, 1, &v), reso = P(S_SHP, 2, &v), drive = P(S_SHP, 3, &v);
    float env_amt = P(S_SHP, 4, &v), ktrk = P(S_SHP, 5, &v), velamt = P(S_SHP, 6, &v);
    int esrc = PI(S_SHP, 7, &v);
    float e = v.env;
    if (esrc == 1) { run_fenv(v); e = v.fenv; } else if (esrc == 2) e = v.menv;
    float semis = cutoff * 120.0f - 30.0f + env_amt * e * 60.0f + ktrk * (note - 60.0f);
    if (velamt != 0.0f) semis += velamt * v.vel * 48.0f;
    float f = clampf(440.0f * SemitonesToRatio(semis - 69.0f + 60.0f) / SR, 0.0001f, 0.45f);
    float q = 0.5f + reso * reso * 25.0f;
    int sat = PI(S_SHP, 12, &v); float fm = P(S_SHP, 13, &v), gdb = P(S_SHP, 14, &v);
    float post = gdb != 0.0f ? powf(10.0f, gdb * 12.0f / 20.0f) : 1.0f;
    bool four = type == 3;
    svf_[i].set_f_q<FREQUENCY_FAST>(f, four ? 0.5f + reso * reso * 4.0f : q);
    if (four) svf2_[i].set_f_q<FREQUENCY_FAST>(f, 0.6f + reso * reso * 20.0f);
    float g1 = 1.0f + drive * 6.0f, norm1 = drive > 0.0f ? sqrtf(g1) : 1.0f;
    // DRIVE and GAIN ramp across the block from the last block's (a knob turned stepped the level, 11 Nov 2026)
    static float flt_g_[MAXV], flt_norm_[MAXV], flt_post_[MAXV];
    if (flt_g_[i] <= 0.0f || v.retrig) { flt_g_[i] = g1; flt_norm_[i] = norm1; flt_post_[i] = post; }
    float g = flt_g_[i], norm = flt_norm_[i], post1 = post;
    const float dgr = (g1 - g) * (1.0f / BLOCK), dnr = (norm1 - norm) * (1.0f / BLOCK), dpo = (post1 - flt_post_[i]) * (1.0f / BLOCK);
    post = flt_post_[i];
    flt_g_[i] = g1; flt_norm_[i] = norm1; flt_post_[i] = post1;
    for (int n = 0; n < BLOCK; ++n) {
      g += dgr; norm += dnr; post += dpo;
      float in = (vbuf_[n] + vaux_[n]) * 0.5f, xg = in * g, x;
      switch (sat) {
        case 1: x = clampf(xg, -1.0f, 1.0f) / norm; break;                        // HARD
        case 2: x = sinf(clampf(xg, -4.0f, 4.0f) * 1.5707963f) / norm; break;      // FOLD
        case 3: x = (xg >= 0.0f ? TANHF(xg) : TANHF(xg * 0.6f) / 0.6f) / norm; break;   // ASYM
        default: x = TANHF(xg) / norm;                                             // SOFT (as before)
      }
      if (fm > 0.0f) {      // AUDIO FM: the source moves the cutoff at audio rate
        float ff = clampf(f * SemitonesToRatio(in * fm * 48.0f), 0.0001f, 0.45f);
        svf_[i].set_f_q<FREQUENCY_FAST>(ff, four ? 0.5f + reso * reso * 4.0f : q);
        if (four) svf2_[i].set_f_q<FREQUENCY_FAST>(ff, 0.6f + reso * reso * 20.0f);
      }
      float y;
      if (type == 0) y = svf_[i].Process<FILTER_MODE_LOW_PASS>(x);
      else if (type == 1) y = svf_[i].Process<FILTER_MODE_BAND_PASS>(x);
      else if (type == 2) y = svf_[i].Process<FILTER_MODE_HIGH_PASS>(x);
      else if (four) y = svf2_[i].Process<FILTER_MODE_LOW_PASS>(svf_[i].Process<FILTER_MODE_LOW_PASS>(x));
      else { float lp, hp; svf_[i].Process<FILTER_MODE_LOW_PASS, FILTER_MODE_HIGH_PASS>(x, &lp, &hp); y = type == 4 ? lp + hp : lp - hp; }   // NOTCH, PEAK
      vbuf_[n] = vaux_[n] = y * post;
    }
  }
  v.retrig = false;

  // ---- AMP
  float level = P(S_AMP, 7, &v);
  float vel_amt = P(S_AMP, 4, &v);
  float gain = v.env * 4.0f * level * level * (1.0f - vel_amt + vel_amt * v.vel);   // LEVEL 0.5 = unity, 1.0 = +12 dB
  float spread = P(S_AMP, 6, &v);
  float pan = clampf(v.pan * spread + mod_sum(-1, 1, &v), -1.0f, 1.0f);
  float gl = gain * sqrtf(0.5f * (1.0f - pan)), gr = gain * sqrtf(0.5f * (1.0f + pan));
  // the gain ramps across the block from the last block's: a step every 24 samples clicked on fast attacks,
  // releases and plucks (10 Nov 2026)
  float l0 = v.gl0 * 1.41f, r0 = v.gr0 * 1.41f, dl = (gl - v.gl0) * (1.41f / BLOCK), dr = (gr - v.gr0) * (1.41f / BLOCK);
  for (int n = 0; n < BLOCK; ++n) { l0 += dl; r0 += dr; mix_l[n] += vbuf_[n] * l0; mix_r[n] += vaux_[n] * r0; }
  v.gl0 = gl; v.gr0 = gr;
}

// ------------------------------------------------------------------ master: 3-band EQ, glue compressor, look-ahead limiter
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void set(int type, float f, float gain_db, float q) {   // RBJ cookbook: 0 low shelf, 1 peak, 2 high shelf
    float A = powf(10.0f, gain_db / 40.0f), w = 2.0f * 3.14159265f * f / SR, cw = cosf(w), sw = sinf(w);
    float a0, alpha = sw / (2.0f * q);
    if (type == 1) { b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; }
    else {
      float sa = 2.0f * sqrtf(A) * alpha, s = type == 0 ? -1.0f : 1.0f;
      b0 = A * ((A + 1) - s * (A - 1) * cw + sa); b1 = 2 * s * A * ((A - 1) - s * (A + 1) * cw); b2 = A * ((A + 1) - s * (A - 1) * cw - sa);
      a0 = (A + 1) + s * (A - 1) * cw + sa; a1 = -2 * s * ((A - 1) + s * (A + 1) * cw); a2 = (A + 1) + s * (A - 1) * cw - sa;
    }
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
  }
  inline float run(float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
static Biquad eq_[2][3];
static float eq_db_[3] = { 0, 0, 0 }, eq_cur_[3] = { 0, 0, 0 }, comp_amt_ = 0.0f;
static float comp_g_ = 0.0f;   // the FM-1's compressor gain at the end of the last block (0: none yet)
static float comp_env_ = 0.0f, comp_gr_db_ = 0.0f, lim_gain_ = 1.0f, lim_gr_db_ = 0.0f;
static float lim_buf_l_[64], lim_buf_r_[64]; static int lim_w_ = 0;
// the limiter's look-ahead peak: the largest |sample| of the last 48, kept as a falling queue (each sample goes in
// once and out once) instead of reading all 48 for every sample. The same peak exactly; NaN counts as 0, as fmaxf
// skipped it.
static float lim_qv_[64]; static uint32_t lim_qi_[64], lim_qh_ = 0, lim_qt_ = 0, lim_n_ = 0;
// a new patch (init): nothing of the old one's sound left in the master's filters, compressor and limiter
static void master_reset() {
  for (int c = 0; c < 2; ++c) for (int b = 0; b < 3; ++b) eq_[c][b].z1 = eq_[c][b].z2 = 0.0f;
  memset(lim_buf_l_, 0, sizeof(lim_buf_l_)); memset(lim_buf_r_, 0, sizeof(lim_buf_r_));
  lim_qh_ = lim_qt_ = 0; lim_gain_ = 1.0f; comp_env_ = 0.0f; comp_g_ = 0.0f;
}
EXPORT void set_master_fx(float low, float mid, float high, float comp) { eq_db_[0] = low; eq_db_[1] = mid; eq_db_[2] = high; comp_amt_ = comp; }
static void run_master(float* l, float* r) {
  // EQ: the gains glide, the filters are re-designed only when they move
  bool flat = true;
  for (int b = 0; b < 3; ++b) {
    if (fabsf(eq_cur_[b] - eq_db_[b]) > 0.01f) { eq_cur_[b] += (eq_db_[b] - eq_cur_[b]) * 0.2f;
      static const float f[3] = { 150.0f, 1000.0f, 5000.0f }, q[3] = { 0.707f, 0.9f, 0.707f };
      for (int c = 0; c < 2; ++c) eq_[c][b].set(b == 0 ? 0 : b == 1 ? 1 : 2, f[b], eq_cur_[b], q[b]); }
    if (fabsf(eq_cur_[b]) > 0.01f) flat = false;
  }
  if (!flat) for (int n = 0; n < BLOCK; ++n) for (int b = 0; b < 3; ++b) { l[n] = eq_[0][b].run(l[n]); r[n] = eq_[1][b].run(r[n]); }
  // glue compressor: one knob lowers the threshold from -6 to -30 dB and raises the ratio from 2:1 to 4:1, with make-up
  if (comp_amt_ > 0.001f) {
    float thr = -6.0f - 24.0f * comp_amt_, ratio = 2.0f + 2.0f * comp_amt_, makeup = -thr * (1.0f - 1.0f / ratio) * 0.5f;
#ifdef RACK_FM1
    // the FM-1: the gain worked out once a block (log10f and powf per sample were most of the compressor's time) and
    // ramped across it; the envelope still follows every sample, and the limiter after catches what the ramp lags
    for (int n = 0; n < BLOCK; ++n) {
      float x = fmaxf(fabsf(l[n]), fabsf(r[n]));
      comp_env_ += (x - comp_env_) * (x > comp_env_ ? 0.02f : 0.0004f);
    }
    float lvl = 20.0f * log10f(comp_env_ + 1e-6f), over = lvl - thr;
    float gr = over > 0 ? over * (1.0f - 1.0f / ratio) : 0.0f;
    comp_gr_db_ = gr;
    float g1 = powf(10.0f, (makeup - gr) / 20.0f), g = comp_g_, dg = (g1 - comp_g_) / BLOCK;
    if (g <= 0.0f) { g = g1; dg = 0.0f; }
    for (int n = 0; n < BLOCK; ++n) { g += dg; l[n] *= g; r[n] *= g; }
    comp_g_ = g1;
#else
    for (int n = 0; n < BLOCK; ++n) {
      float x = fmaxf(fabsf(l[n]), fabsf(r[n]));
      comp_env_ += (x - comp_env_) * (x > comp_env_ ? 0.02f : 0.0004f);
      float lvl = 20.0f * log10f(comp_env_ + 1e-6f), over = lvl - thr;
      float gr = over > 0 ? over * (1.0f - 1.0f / ratio) : 0.0f;
      comp_gr_db_ = gr;
      float g = powf(10.0f, (makeup - gr) / 20.0f);
      l[n] *= g; r[n] *= g;
    }
#endif
  } else comp_gr_db_ = 0.0f;
  // limiter: looks 48 samples (1 ms) ahead, so a loud attack is already turned down when it arrives
  for (int n = 0; n < BLOCK; ++n) {
    lim_buf_l_[lim_w_] = l[n]; lim_buf_r_[lim_w_] = r[n];
    float a = fmaxf(fabsf(l[n]), fabsf(r[n])); if (!(a == a)) a = 0.0f;
    while (lim_qt_ != lim_qh_ && lim_qv_[(lim_qt_ - 1u) & 63u] <= a) --lim_qt_;
    lim_qv_[lim_qt_ & 63u] = a; lim_qi_[lim_qt_ & 63u] = lim_n_; ++lim_qt_;
    while (lim_n_ - lim_qi_[lim_qh_ & 63u] >= 48u) ++lim_qh_;
    float peak = lim_qv_[lim_qh_ & 63u]; ++lim_n_;
    float target = peak > 0.89f ? 0.89f / peak : 1.0f;
    lim_gain_ += (target - lim_gain_) * (target < lim_gain_ ? 0.3f : 0.0005f);
    int rd = (lim_w_ - 47) & 63;
    l[n] = lim_buf_l_[rd] * lim_gain_; r[n] = lim_buf_r_[rd] * lim_gain_;
    lim_w_ = (lim_w_ + 1) & 63;
  }
  lim_gr_db_ = -20.0f * log10f(lim_gain_ + 1e-6f);
}

// ------------------------------------------------------------------ FX
// the reverb's wet signal from in_l/in_r (its pre-delay, LO CUT, shimmer and tank), into wl/wr
static void reverb_wet(int slot, const float* l, const float* r, float* wl_out, float* wr_out) {
    int type = PI(slot, 4, 0);                     // 0 PLATE, 1 HALL, 2 SHIMMER
    float size = P(slot, 0, 0), mix = P(slot, 3, 0);
    float time = type == 0 ? 0.35f + size * 0.63f : 0.6f + size * 0.39f;
    float rlp = (type == 1 ? 0.8f : 0.95f) - P(slot, 1, 0) * 0.6f;
#ifdef RACK_FM1
    // the FM-1 runs the reverb at 24 kHz (12 Nov 2026: 21 % of a core at 48): its loops take twice as long, so each
    // pass loses what two did (time squared), and its damping is per sample, so twice as much a sample
    time *= time; rlp = 1.0f - (1.0f - rlp) * (1.0f - rlp);
#endif
    reverb_.set_amount(1.0f);
    reverb_.set_time(time);
    reverb_.set_diffusion(0.3f + 0.6f * P(slot, 2, 0));
    reverb_.set_lp(rlp);
    reverb_.set_input_gain(type == 0 ? 0.25f : 0.2f);
    int pre = (int)(P(slot, 5, 0) * 0.15f * SR); if (pre > 7999) pre = 7999;
    float lc = P(slot, 7, 0), width = P(slot, 8, 0), duck = P(slot, 9, 0);
    float lcc = lc > 0.0f ? 1.0f - expf(-6.2831853f * (20.0f + lc * lc * 980.0f) / SR) : 0.0f;
    float shim = type == 2 && shim_buf_ ? P(slot, 6, 0) : 0.0f;
    clouds::FloatFrame fr[BLOCK];
    for (int n = 0; n < BLOCK; ++n) {
      pre_buf_l_[pre_w_] = (int16_t)(clampf(l[n], -1.0f, 1.0f) * 32767.0f); pre_buf_r_[pre_w_] = (int16_t)(clampf(r[n], -1.0f, 1.0f) * 32767.0f);
      int rd = (pre_w_ - pre) & pre_mask_; pre_w_ = (pre_w_ + 1) & pre_mask_;
      float in_l = pre_buf_l_[rd] / 32768.0f, in_r = pre_buf_r_[rd] / 32768.0f;
      if (lc > 0.0f) { rv_lc_l_ += (in_l - rv_lc_l_) * lcc; rv_lc_r_ += (in_r - rv_lc_r_) * lcc; in_l -= rv_lc_l_; in_r -= rv_lc_r_; }   // LO CUT: thin the tail
      if (shim > 0.0f) {         // the tail, an octave up, back into the tank
        shim_buf_[shim_w_] = (int16_t)(clampf(shim_fb_, -1.0f, 1.0f) * 32767.0f);
        shim_ph_ += 1.0f / 2048.0f; if (shim_ph_ >= 1.0f) shim_ph_ -= 1.0f;
        float d1 = (1.0f - shim_ph_) * 2048.0f, d2 = (1.0f - fmodf(shim_ph_ + 0.5f, 1.0f)) * 2048.0f;
        auto rdbuf = [](float d) { float pos = shim_w_ - d; while (pos < 0) pos += 4096.0f; int i0 = (int)pos; float f = pos - i0; return (shim_buf_[i0 & 4095] * (1 - f) + shim_buf_[(i0 + 1) & 4095] * f) / 32768.0f; };
        float w1 = SIN01(shim_ph_ * 0.5f), w2 = SIN01(fmodf(shim_ph_ + 0.5f, 1.0f) * 0.5f);
        float up = rdbuf(d1) * w1 + rdbuf(d2) * w2;
        shim_w_ = (shim_w_ + 1) & 4095;
        in_l += up * shim * 0.35f; in_r += up * shim * 0.35f;   // 1.9.2: 0.7 let long shimmer tails grow after release
      }
      fr[n].l = in_l; fr[n].r = in_r;
    }
#ifdef RACK_FM1
    {
      static Half24Down dl, dr; static Half24Up ul, ur;
      float xl[BLOCK], xr[BLOCK], yl[BLOCK / 2], yr[BLOCK / 2];
      clouds::FloatFrame h[BLOCK / 2];
      for (int n = 0; n < BLOCK; ++n) { xl[n] = fr[n].l; xr[n] = fr[n].r; }
      dl.run(xl, yl); dr.run(xr, yr);
      for (int m = 0; m < BLOCK / 2; ++m) { h[m].l = yl[m]; h[m].r = yr[m]; }
      reverb_.Process(h, BLOCK / 2);
      for (int m = 0; m < BLOCK / 2; ++m) { yl[m] = h[m].l; yr[m] = h[m].r; }
      ul.run(yl, xl); ur.run(yr, xr);
      for (int n = 0; n < BLOCK; ++n) { fr[n].l = xl[n]; fr[n].r = xr[n]; }
    }
#else
    reverb_.Process(fr, BLOCK);
#endif
    for (int n = 0; n < BLOCK; ++n) { shim_fb_ = (fr[n].l + fr[n].r) * 0.5f; wl_out[n] = fr[n].l; wr_out[n] = fr[n].r; }
}
static void run_fx(int slot, float* l, float* r) {
  int id = mod_id_[slot];
  if (fx_off_[slot - S_FX1]) return;   // switched off from the panel (set_fx_bypass)
#ifndef RACK_LITE
  if (id == M_CLOUDS) {
    clouds::Parameters* p = clouds_.mutable_parameters();
    p->position = P(slot, 0, 0); p->size = P(slot, 1, 0); p->pitch = P(slot, 2, 0) * 24.0f;
    p->density = P(slot, 3, 0); p->texture = P(slot, 4, 0); p->dry_wet = P(slot, 5, 0);
    p->stereo_spread = P(slot, 6, 0); p->feedback = P(slot, 7, 0); p->reverb = P(slot, 8, 0);
    clouds_.set_playback_mode((clouds::PlaybackMode)PI(slot, 9, 0));
    p->freeze = PI(slot, 10, 0) == 1;
    // page 3-4: QUALITY (16/8-bit, stereo/mono, as the hardware's button), IN GAIN, NOTE TRG (a grain on each new note), PITCH TRK
    int qual = PI(slot, 11, 0); if (qual != clouds_quality_) { clouds_quality_ = qual; clouds_.set_quality(qual); }
    static bool cl_prev = false; bool trg = PI(slot, 13, 0) == 1 && any_gate_ && !cl_prev; cl_prev = any_gate_;
    // page 4-6 (1.5): GRN SYNC fires grains on the tempo; SPRAY, PITCH RND and SIZE RND give each grain its own
    // position, pitch and length; PITCH QNT keeps the scatter musical; SCAN moves the read point; TONE colours the wet only
    static const float grid_div[] = { 0, 1, 2, 4, 8 };
    int gs = PI(slot, 15, 0);
    if (gs > 0) { int cell = (int)(CLK() / grid_div[gs]); if (cell != cl_grid_) { cl_grid_ = cell; trg = true; } } else cl_grid_ = -1;
    p->trigger = trg; p->gate = trg;
    if (PI(slot, 14, 0) == 1) p->pitch = clampf(p->pitch + vs_[last_voice_].note - 60.0f, -48.0f, 48.0f);
    float spray = P(slot, 16, 0), prnd = P(slot, 17, 0), srnd = P(slot, 19, 0), scan = P(slot, 20, 0), tone = P(slot, 21, 0);
    int pq = PI(slot, 18, 0);
    if (scan != 0.0f) { cl_scan_ += scan * fabsf(scan) * 0.5f * BLOCK / SR; cl_scan_ -= floorf(cl_scan_); } else cl_scan_ = 0.0f;
    if (scan != 0.0f || spray > 0.0f) {
      float pos = p->position + cl_scan_ + (spray > 0.0f ? (randf() - 0.5f) * spray : 0.0f);
      p->position = pos - floorf(pos);
    }
    if (prnd > 0.0f) {
      float st = (randf() * 2.0f - 1.0f) * prnd * 24.0f;
      if (pq == 1) st = floorf(st + 0.5f);
      else if (pq == 2) { static const float iv[] = { -24, -19, -12, -7, 0, 7, 12, 19, 24 }; float best = 0; for (float x : iv) if (fabsf(x - st) < fabsf(best - st)) best = x; st = best; }
      else if (pq == 3) st = floorf(st / 12.0f + 0.5f) * 12.0f;
      p->pitch = clampf(p->pitch + st, -48.0f, 48.0f);
    }
    if (srnd > 0.0f) p->size = clampf(p->size * (1.0f - randf() * srnd), 0.0f, 1.0f);
    float user_mix = p->dry_wet;
    if (tone != 0.0f) p->dry_wet = 1.0f;     // TONE: take the wet signal alone, filter it, and mix it back ourselves
    float ig = P(slot, 12, 0), igain = ig != 0.0f ? powf(10.0f, ig * 12.0f / 20.0f) : 1.0f;
    clouds_.Prepare();
    clouds::ShortFrame in[BLOCK], out[BLOCK];
    for (int n = 0; n < BLOCK; ++n) {
      in[n].l = (int16_t)(clampf(l[n] * igain, -1.0f, 1.0f) * 32767.0f);
      in[n].r = (int16_t)(clampf(r[n] * igain, -1.0f, 1.0f) * 32767.0f);
    }
    clouds_.Process(in, out, BLOCK);
    if (tone == 0.0f) for (int n = 0; n < BLOCK; ++n) { l[n] = out[n].l / 32768.0f; r[n] = out[n].r / 32768.0f; }
    else {
      float hz = tone < 0.0f ? 18000.0f * powf(300.0f / 18000.0f, -tone) : 20.0f * powf(2000.0f / 20.0f, tone);
      float c = 1.0f - expf(-6.2831853f * hz / SR);
      float fi = Interpolate(clouds::lut_xfade_in, user_mix, 16.0f), fo = Interpolate(clouds::lut_xfade_out, user_mix, 16.0f);
      float norm = 2.0f / Interpolate(clouds::lut_xfade_in, 1.0f, 16.0f);   // Clouds halves its output (SoftConvert); undo that for the wet
      for (int n = 0; n < BLOCK; ++n) {
        float wl = out[n].l / 32768.0f * norm, wr = out[n].r / 32768.0f * norm;
        cl_tone_l_ += (wl - cl_tone_l_) * c; cl_tone_r_ += (wr - cl_tone_r_) * c;
        if (tone < 0.0f) { wl = cl_tone_l_; wr = cl_tone_r_; } else { wl -= cl_tone_l_; wr -= cl_tone_r_; }
        float dl = in[n].l / 32768.0f, dr = in[n].r / 32768.0f;
        l[n] = SoftLimit((dl * fo + wl * fi) * 0.5f); r[n] = SoftLimit((dr * fo + wr * fi) * 0.5f);   // the same output stage as Clouds
      }
    }
  } else
#endif
  if (id == M_REVERB) {
    int type = PI(slot, 4, 0);                     // 0 PLATE, 1 HALL, 2 SHIMMER
    float mix = P(slot, 3, 0), width = P(slot, 8, 0), duck = P(slot, 9, 0);
    float dry_l[BLOCK], dry_r[BLOCK], wet_l[BLOCK], wet_r[BLOCK];
    for (int n = 0; n < BLOCK; ++n) { dry_l[n] = l[n]; dry_r[n] = r[n]; }
#ifdef RACK_FM1
    if (slot == rv_early_) {   // its wet signal was made a block behind (early_work): keep this input for the next
      for (int n = 0; n < BLOCK; ++n) { rv_in_l_[n] = l[n]; rv_in_r_[n] = r[n]; wet_l[n] = rv_wet_l_[n]; wet_r[n] = rv_wet_r_[n]; }
    } else
#endif
    reverb_wet(slot, l, r, wet_l, wet_r);
    for (int n = 0; n < BLOCK; ++n) {
      float wet = mix * (type == 1 ? 0.75f : 0.6f);
      float wl = wet_l[n], wr = wet_r[n];
      if (width != 0.0f) { float m = (wl + wr) * 0.5f, sd = (wl - wr) * 0.5f * (1.0f + width); wl = m + sd; wr = m - sd; }
      if (duck > 0.0f) { float x = fmaxf(fabsf(dry_l[n]), fabsf(dry_r[n])); rv_duck_ += (x - rv_duck_) * (x > rv_duck_ ? 0.01f : 0.0002f); wet *= 1.0f - duck * clampf(rv_duck_ * 3.0f, 0.0f, 1.0f); }
      l[n] = dry_l[n] * (1.0f - mix * 0.4f) + wl * wet; r[n] = dry_r[n] * (1.0f - mix * 0.4f) + wr * wet;
    }
  } else if (id == M_DELAY) {
    static const float beats[] = { 0.25f, 0.5f, 0.75f, 1.0f, 2.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.5f, 3.0f };   // .. 1/2, then 1/8T 1/4T 1/4D 1/2D
    float bpm = base_[S_GLO][0] > 1.0f ? base_[S_GLO][0] : 112.0f;
    int ti = PI(slot, 0, 0); ti = ti < 0 ? 0 : ti > 8 ? 8 : ti;
    int samples = PI(slot, 9, 0) == 1 ? (int)(0.01f * powf(200.0f, P(slot, 10, 0)) * SR)   // SYNC FREE: 10 ms to 2 s
                                      : (int)(beats[ti] * 60.0f / bpm * SR);
    float width = P(slot, 6, 0), duck = P(slot, 7, 0), lc = P(slot, 8, 0);
    float lcc = lc > 0.0f ? 1.0f - expf(-6.2831853f * (20.0f + lc * lc * 980.0f) / SR) : 0.0f;
    if (samples > delay_max_) samples = delay_max_;
    float fb = P(slot, 1, 0) * 0.92f, tone = P(slot, 2, 0), mix = P(slot, 3, 0);
    int mode = PI(slot, 4, 0); float wow = P(slot, 5, 0);      // 0 PING, 1 MONO, 2 TAPE
    float lp = 0.05f + tone * 0.9f;
    for (int n = 0; n < BLOCK; ++n) {
      float dsamp = (float)samples;
      if (mode == 2) {           // wow and flutter swing the read point; the repeats saturate as they pile up
        tape_ph1_ += 0.45f / SR; if (tape_ph1_ >= 1.0f) tape_ph1_ -= 1.0f;
        tape_ph2_ += 6.3f / SR; if (tape_ph2_ >= 1.0f) tape_ph2_ -= 1.0f;
        dsamp += wow * (SIN01(tape_ph1_) * 90.0f + SIN01(tape_ph2_) * 6.0f);
      }
      float pos = delay_w_ - dsamp; while (pos < 0) pos += (float)dlen_;
      int i0 = (int)pos; float fr2 = pos - i0; int i1 = (i0 + 1) % dlen_; i0 %= dlen_;
      float dl = DG(delay_l_[i0]) * (1 - fr2) + DG(delay_l_[i1]) * fr2, dr = DG(delay_r_[i0]) * (1 - fr2) + DG(delay_r_[i1]) * fr2;
      delay_tone_l_ += (dl - delay_tone_l_) * lp; delay_tone_r_ += (dr - delay_tone_r_) * lp;
      if (lc > 0.0f) { dl_lc_l_ += (delay_tone_l_ - dl_lc_l_) * lcc; dl_lc_r_ += (delay_tone_r_ - dl_lc_r_) * lcc; delay_tone_l_ -= dl_lc_l_; delay_tone_r_ -= dl_lc_r_; }
      if (mode == 0) {           // ping-pong
        delay_l_[delay_w_] = DS(l[n] + delay_tone_r_ * fb);
        delay_r_[delay_w_] = DS(r[n] * 0.5f + delay_tone_l_ * fb);
      } else if (mode == 1) {
        float m = (l[n] + r[n]) * 0.5f;
        delay_l_[delay_w_] = DS(m + delay_tone_l_ * fb); delay_r_[delay_w_] = DS(m + delay_tone_r_ * fb);
      } else {
        delay_l_[delay_w_] = DS(SoftClip(l[n] + delay_tone_l_ * fb * 1.05f)); delay_r_[delay_w_] = DS(SoftClip(r[n] + delay_tone_r_ * fb * 1.05f));
      }
#ifndef RACK_FM1
      if (delay_q16_) { delay_l_[delay_w_] = q16(delay_l_[delay_w_]); delay_r_[delay_w_] = q16(delay_r_[delay_w_]); }
#endif
      delay_w_ = (delay_w_ + 1) % dlen_;
      float wl = delay_tone_l_, wr = delay_tone_r_, wm = mix;
      if (width != 0.0f) { float m = (wl + wr) * 0.5f, sd = (wl - wr) * 0.5f * (1.0f + width); wl = m + sd; wr = m - sd; }
      if (duck > 0.0f) { float x = fmaxf(fabsf(l[n]), fabsf(r[n])); dl_duck_ += (x - dl_duck_) * (x > dl_duck_ ? 0.01f : 0.0002f); wm *= 1.0f - duck * clampf(dl_duck_ * 3.0f, 0.0f, 1.0f); }
      l[n] += wl * wm; r[n] += wr * wm;
    }
  } else if (id == M_CHORUS) {
    float rate = 0.05f + P(slot, 0, 0) * 4.0f, depth = P(slot, 1, 0), width = P(slot, 2, 0), mix = P(slot, 3, 0);
    int cmode = PI(slot, 4, 0); float cfb = P(slot, 5, 0) * 0.9f, cdl = P(slot, 6, 0);   // MODE CHORUS/FLANGER/VIBRATO, FEEDBACK, DELAY
    float base = cmode == 1 ? 24.0f + cdl * 20.0f : 240.0f + cdl * 200.0f, swing = cmode == 1 ? 200.0f : 600.0f;
    if (cmode == 2) mix = 1.0f;
    // the delay's centre, its depth and the mix ramp across the block from the last block's: a knob turned moved
    // the taps in jumps (11 Nov 2026)
    static float ch_base0 = -1.0f, ch_dep0, ch_mix0;
    if (ch_base0 < 0.0f) { ch_base0 = base; ch_dep0 = depth; ch_mix0 = mix; }
    const float dbs = (base - ch_base0) * (1.0f / BLOCK), ddp = (depth - ch_dep0) * (1.0f / BLOCK), dmx = (mix - ch_mix0) * (1.0f / BLOCK);
    { float tb = base, td = depth, tm = mix; base = ch_base0; depth = ch_dep0; mix = ch_mix0; ch_base0 = tb; ch_dep0 = td; ch_mix0 = tm; }
    for (int n = 0; n < BLOCK; ++n) {
      base += dbs; depth += ddp; mix += dmx;
      chorus_phase_ += rate / SR; if (chorus_phase_ >= 1.0f) chorus_phase_ -= 1.0f;
      float s = SIN01(chorus_phase_);
      chorus_l_[chorus_w_] = DS(cfb > 0.0f ? l[n] + ch_fb_l_ * cfb : l[n]); chorus_r_[chorus_w_] = DS(cfb > 0.0f ? r[n] + ch_fb_r_ * cfb : r[n]);
#ifndef RACK_FM1
      if (delay_q16_) { chorus_l_[chorus_w_] = q16(chorus_l_[chorus_w_]); chorus_r_[chorus_w_] = q16(chorus_r_[chorus_w_]); }   // the FM-1 preview: 16-bit
#endif
      float dl = base + (1.0f + s) * depth * swing, dr = base + (1.0f - s * width) * depth * swing;
      auto tap = [](dsamp_t* buf, int w, float d) {
        float pos = w - d; while (pos < 0) pos += 4096.0f;
        int i0 = (int)pos; float fr = pos - i0;
        return DG(buf[i0 & 4095]) * (1.0f - fr) + DG(buf[(i0 + 1) & 4095]) * fr;
      };
      float wl = tap(chorus_l_, chorus_w_, dl), wr = tap(chorus_r_, chorus_w_, dr);
      chorus_w_ = (chorus_w_ + 1) & 4095;
      ch_fb_l_ = wl; ch_fb_r_ = wr;
      if (cmode == 2) { l[n] = wl; r[n] = wr; }
      else { l[n] = l[n] * (1.0f - mix * 0.5f) + wl * mix; r[n] = r[n] * (1.0f - mix * 0.5f) + wr * mix; }
    }
  }
}

// ------------------------------------------------------------------ block + render
#ifdef RACK_FM1
// The FM-1's second core (the platform's worker, given by set_worker): with 2 or more voices it renders the upper
// half (the larger half: the first core also runs the modulators, effects and master) into its own mix while the
// first core renders the rest; then the two mixes are added. Without a worker, or when it can't take the job or
// drops it, the first core renders every voice. The voices share nothing they write (each its own state and
// scratch; stmlib's Random is shared, so its numbers may repeat across cores); the parameters, routes and
// modulators they read only change between blocks.
typedef int (*rack_run_fn)(void (*)(uint32_t), uint32_t);
typedef int (*rack_wait_fn)(void);
static rack_run_fn par_run_ = 0;
static rack_wait_fn par_wait_ = 0;
static float par_l_[BLOCK], par_r_[BLOCK];
static int par_from_ = 0;
static uint32_t par_blocks_ = 0;    // blocks the second core took a share of
EXPORT void set_worker(rack_run_fn run, rack_wait_fn wait) { par_run_ = run; par_wait_ = wait; }
EXPORT uint32_t worker_blocks() { return par_blocks_; }
static void par_job(uint32_t) {
  memset(par_l_, 0, sizeof(par_l_)); memset(par_r_, 0, sizeof(par_r_));
  for (int i = par_from_; i < nvoices_; ++i) render_voice(i, par_l_, par_r_, 1);
}
static void early_work();
static void render_voices() {
  // the second core takes voices par_from_ up; the first runs Rings and the reverb a block behind (early_work), then
  // its own voices. With one core the same work runs in the same order (the modules share one random number
  // generator, so one order keeps one sound)
  // The first core's share evens the two out: its early work counts as so many voices (Rings about 1.85, with
  // STR+VERB 2.2, the reverb 1.4; from the 13 Nov 2026 run, against a Plaits voice), and each core takes half of
  // what is left (14 Nov: was a third with Rings, half without, which left the reverb's 28 % on top of the first
  // core's half: six 2OP FM voices with a reverb were 3 + reverb against 3; now 2 + reverb against 4)
  float early = 0.0f;
  if (mod_id_[S_SHP] == M_RINGS) early += PI(S_SHP, 0, 0) == 5 ? 2.2f : 1.85f;
  if (rv_early_ >= 0) early += 1.4f;
  int first = (int)floorf((nvoices_ - early) * 0.5f + 0.5f);
  par_from_ = first < 0 ? 0 : first > nvoices_ ? nvoices_ : first;
  bool shared = par_run_ && par_from_ < nvoices_ && par_run_(par_job, 0);
  early_work();
  for (int i = 0; i < par_from_; ++i) render_voice(i, blk_l_, blk_r_);
  if (shared && par_wait_() == 0) ++par_blocks_;
  else par_job(0);   // one core, or the second dropped it: the first does the second's share last
  for (int n = 0; n < BLOCK; ++n) { blk_l_[n] += par_l_[n]; blk_r_[n] += par_r_[n]; }
}
#endif
#ifdef RACK_FM1
static void run_rings_shared() {
  rings::Part& r = rings_[0];
  const VoiceState* v = &vs_[rings_last_];
  int rmodel = rings_fm1_model(PI(S_SHP, 0, v));
  if (rmodel == 5 && !rings_rev_) rmodel = 2;
  r.set_model((rings::ResonatorModel)rmodel);
  rings::Patch rp; rp.structure = P(S_SHP, 1, v); rp.brightness = P(S_SHP, 2, v);
  rp.damping = P(S_SHP, 3, v); rp.position = P(S_SHP, 4, v);
  float rt = P(S_SHP, 8, v), rf = P(S_SHP, 9, v), mix = P(S_SHP, 6, v), spread = P(S_SHP, 7, v);
  float level = P(S_AMP, 7, v), wet = 4.0f * level * level * mix;   // as a voice's AMP gain, without its envelope
  static float wet_last = -1.0f;
  float wet0 = wet_last < 0.0f ? wet : wet_last;
  wet_last = wet;
  // Rings runs at 24 kHz on the FM-1 (engine/shim/rings/dsp/dsp.h): its excitation down and its output back up
  // through the halfband filter (Half24Down/Up; a mean of each pair and a straight line between let the source's top
  // fold into the strings' middle, 11 Nov 2026)
  static Half24Down rdown; static Half24Up rup1, rup2;
  float x48[BLOCK], in[BLOCK / 2], o1[BLOCK / 2], o2[BLOCK / 2], w1[BLOCK], w2[BLOCK];
  for (int n = 0; n < BLOCK; ++n) x48[n] = rings_ex_[n];
  rdown.run(x48, in);
  rings::PerformanceState ps;
  ps.strum = rings_qn_ > 0;   // one strum a block (0.5 ms): a chord's notes follow each other that far apart
  if (ps.strum) { rings_note_ = rings_q_[0]; for (int k = 1; k < rings_qn_; ++k) rings_q_[k - 1] = rings_q_[k]; --rings_qn_; }
  ps.internal_exciter = PI(S_SHP, 10, v) == 1; ps.internal_strum = false; ps.internal_note = false;
  ps.tonic = 0.0f; ps.note = rt != 0.0f || rf != 0.0f ? rings_note_ + floorf(rt + 0.5f) + rf : rings_note_;
  ps.fm = 0.0f; ps.chord = PI(S_SHP, 11, v);
  r.Process(ps, rp, in, o1, o2, BLOCK / 2);
  rup1.run(o1, w1); rup2.run(o2, w2);
  for (int n = 0; n < BLOCK; ++n) {
    float c = (w1[n] + w2[n]) * 0.5f;
    float wn = wet0 + (wet - wet0) * (float)(n + 1) * (1.0f / BLOCK);   // ramped from the last block's (a knob, a macro)
    blk_l_[n] += (c + (w1[n] - c) * spread) * wn;
    blk_r_[n] += (c + (w2[n] - c) * spread) * wn;
  }
}
#endif
#ifdef RACK_FM1
static void early_work() {
  if (mod_id_[S_SHP] == M_RINGS) run_rings_shared();
  if (rv_early_ >= 0) reverb_wet(rv_early_, rv_in_l_, rv_in_r_, rv_wet_l_, rv_wet_r_);
}
#endif
static void process_block() {
  glide_step();
  float bpm = base_[S_GLO][0] > 1.0f ? base_[S_GLO][0] : 112.0f;
  clk_advance(BLOCK / SR * bpm / 60.0f * 4.0f);
  for (int i = 0; i < 6; ++i) ext_cur_[i] += (ext_tgt_[i] - ext_cur_[i]) * ext_slew_[i];
  run_lfos();
  run_mod(0); run_mod(1);
  if (shrink_to_) {   // fewer voices: once the ones going have faded out
    bool done = true;
    for (int i = shrink_to_; i < nvoices_; ++i) if (vs_[i].active) done = false;
    if (done) { nvoices_ = shrink_to_; shrink_to_ = 0; }
  }
  memset(blk_l_, 0, sizeof(blk_l_)); memset(blk_r_, 0, sizeof(blk_r_));
#ifdef RACK_FM1
  if (mod_id_[S_SHP] == M_RINGS) { memset(rings_in_, 0, sizeof(rings_in_)); rings_dry_g_ = 1.0f - P(S_SHP, 6, 0); }
  for (int k = 0; k < 2; ++k)   // an effect switched on again: its buffers cleared (set_fx_bypass)
    if (fx_clear_[k]) {
      fx_clear_[k] = 0; init_fx(mod_id_[S_FX1 + k]);
      if (mod_id_[S_FX1 + k] == M_REVERB) { memset(rv_wet_l_, 0, sizeof(rv_wet_l_)); memset(rv_wet_r_, 0, sizeof(rv_wet_r_)); }
    }
  rv_early_ = (mod_id_[S_FX1] == M_REVERB) != (mod_id_[S_FX2] == M_REVERB) ? (mod_id_[S_FX1] == M_REVERB ? S_FX1 : S_FX2) : -1;
  if (rv_early_ >= 0 && fx_off_[rv_early_ - S_FX1]) rv_early_ = -1;   // switched off: not run a block behind either
  render_voices();
  if (mod_id_[S_SHP] == M_RINGS) {   // this block's excitation and strums, for Rings next block
    for (int n = 0; n < BLOCK; ++n) rings_ex_[n] = rings_in_[0][n] + rings_in_[1][n];
    for (int c = 0; c < 2; ++c) { for (int k = 0; k < rings_nqn_[c] && rings_qn_ < 8; ++k) rings_q_[rings_qn_++] = rings_nq_[c][k]; rings_nqn_[c] = 0; }
    if (rings_nl_[1] >= 0) rings_last_ = rings_nl_[1]; else if (rings_nl_[0] >= 0) rings_last_ = rings_nl_[0];   // the highest voice struck
    rings_nl_[0] = rings_nl_[1] = -1;
  }
#else
  for (int i = 0; i < nvoices_; ++i) render_voice(i, blk_l_, blk_r_);
#endif
  run_fx(S_FX1, blk_l_, blk_r_);
  run_fx(S_FX2, blk_l_, blk_r_);
  for (int n = 0; n < BLOCK; ++n) { blk_l_[n] *= master_ * 0.9f; blk_r_[n] *= master_ * 0.9f; }
  run_master(blk_l_, blk_r_);
  for (int n = 0; n < BLOCK; ++n) { blk_l_[n] = SoftClip(blk_l_[n]); blk_r_[n] = SoftClip(blk_r_[n]); }
}

EXPORT void render(int n) {
  if (n > OUTN) n = OUTN;
  for (int i = 0; i < n; ++i) {
    if (blk_pos_ >= BLOCK) { process_block(); blk_pos_ = 0; }
    out_l_[i] = blk_l_[blk_pos_]; out_r_[i] = blk_r_[blk_pos_]; ++blk_pos_;
  }
  for (int i = 0; i < 128 && i < n; ++i) scope_[i] = (out_l_[i] + out_r_[i]) * 0.5f;
  for (int i = 0; i < n; ++i) { float a = fabsf(out_l_[i]), b = fabsf(out_r_[i]); if (a > peak_l_) peak_l_ = a; if (b > peak_r_) peak_r_ = b; }
}

// monitor: what the screen draws. 0-15 modulated params of `slot` (as the most recent voice hears them),
// 16 envs, 22 mod envs, 28 gates, 34 notes, 40 mod outs, 44 lfos, 47 clock (16ths), 48 peaks, 50 route values,
// 62 last voice, 63 voices; 64-159 Grids levels (3 x 32) when `slot` holds Grids; 160-161 master gain reduction; 168-183 params 16-31.
static float mon_[200];
EXPORT float* monitor(int slot) {
  const VoiceState* v = &vs_[last_voice_];
  if (slot >= 0 && slot < NSLOT) { for (int p = 0; p < 16; ++p) mon_[p] = P(slot, p, v); for (int p = 16; p < 32; ++p) mon_[168 + p - 16] = P(slot, p, v); }
  for (int i = 0; i < MAXV; ++i) {
    mon_[16 + i] = vs_[i].env; mon_[22 + i] = vs_[i].menv;
    mon_[28 + i] = i < nvoices_ ? (vs_[i].gate ? 1.0f : vs_[i].stage ? 0.5f : 0.0f) : -1.0f;
    mon_[34 + i] = vs_[i].cur_note;
  }
  for (int i = 0; i < 4; ++i) mon_[40 + i] = mod_out_[i];
  for (int i = 0; i < 3; ++i) mon_[44 + i] = lfo_val_[i];
  mon_[47] = CLK();
  mon_[48] = peak_l_; mon_[49] = peak_r_; peak_l_ *= 0.5f; peak_r_ *= 0.5f;
  for (int i = 0; i < 12; ++i) {
    const Route& r = routes_[i];
    float x = 0.0f;
    if (r.src != SR_NONE) { x = source_value(r.src, v) * r.amt; if (r.via != SR_NONE) x *= source_value(r.via, v); }
    mon_[50 + i] = x;
  }
  mon_[62] = (float)last_voice_; mon_[63] = (float)nvoices_;
  mon_[160] = comp_gr_db_; mon_[161] = lim_gr_db_;
  if ((slot == S_MODA || slot == S_MODB) && mod_id_[slot] == M_GRIDS)
    for (int inst = 0; inst < 3; ++inst) for (int st = 0; st < 32; ++st) mon_[64 + inst * 32 + st] = grids_level(slot, inst, st) / 255.0f;
  return mon_;
}
EXPORT float voice_env(int i) { return vs_[i].env; }
EXPORT float mod_value(int i) { return mod_out_[i]; }
