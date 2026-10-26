// FM1 Rack engine: Mutable Instruments modules (MIT; GPL-3 project) slotted
// into one voice chain, for the browser mockup. Block = 24 frames @ 48 kHz.

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
#include "elements/dsp/part.h"
#include "clouds/dsp/granular_processor.h"
#include "clouds/dsp/fx/reverb.h"
#include "peaks/modulations/lfo.h"
#include "peaks/modulations/multistage_envelope.h"
#include "tides2/poly_slope_generator.h"
#include "warps/dsp/modulator.h"
#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"
#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
namespace marbles { extern const Scale preset_scales[6]; }
namespace grids_data {
extern const uint8_t node_0[], node_1[], node_2[], node_3[], node_4[], node_5[], node_6[], node_7[], node_8[], node_9[],
  node_10[], node_11[], node_12[], node_13[], node_14[], node_15[], node_16[], node_17[], node_18[], node_19[],
  node_20[], node_21[], node_22[], node_23[], node_24[];
}

#define EXPORT extern "C" __attribute__((visibility("default")))

using namespace stmlib;

static const float SR = 48000.0f;
static const int BLOCK = 24;
static const int MAXV = 6;

enum Slot { S_SRC, S_SHP, S_MODA, S_MODB, S_FX1, S_FX2, S_AMP, S_GLO, S_LFO, NSLOT };
enum ModId { M_NONE = 0, M_PLAITS = 1, M_BRAIDS = 2, M_ELEMENTS = 3,
             M_RINGS = 10, M_WARPS = 11, M_FILTER = 12,
             M_PEAKS = 20, M_TIDES = 21, M_MARBLES = 22, M_GRIDS = 23,
             M_CLOUDS = 30, M_DELAY = 31, M_REVERB = 32, M_CHORUS = 33 };
enum Src { SR_NONE, SR_M1, SR_M2, SR_M3, SR_M4, SR_VELO, SR_KEY, SR_RAND, SR_ENV,
           SR_MA0, SR_MA1, SR_MB0, SR_MB1, SR_LFO1, SR_LFO2, SR_LFO3, SR_MENV, NSRC };

static const int NP = 16;
static float base_[NSLOT][NP];
static float lo_[NSLOT][NP], hi_[NSLOT][NP], span_[NSLOT][NP];
static int mod_id_[NSLOT];
static float macros_[4];
static float mod_out_[4];       // MOD A out0/out1, MOD B out0/out1
static float pitch_mod_dummy;

struct Route { int src, slot, param, via; float amt; };
static Route routes_[12];

// ------------------------------------------------------------------ voice
struct VoiceState {
  bool gate, retrig, active;
  float note, cur_note, vel, rnd;
  float env; int stage;          // 0 idle 1 att 2 dec 3 sus 4 rel
  uint32_t age;
  float drift;
  float pan;
  bool prev_gate_el;
  float menv; int mstage;        // mod env: 0 idle 1 att 2 dec
};

static VoiceState vs_[MAXV];
static plaits::Voice plaits_[MAXV];
static char plaits_buf_[MAXV][16384];
static braids::MacroOscillator braids_[MAXV];
static rings::Part rings_[MAXV];
static uint16_t rings_rev_[MAXV][32768];
static elements::Part elements_[MAXV];
static uint16_t elements_rev_[MAXV][32768];
static Svf svf_[MAXV];
static warps::Modulator warps_[MAXV];
static int nvoices_ = 2;
static int last_voice_ = 0;
static uint32_t age_counter_ = 0;

// ------------------------------------------------------------------ shared
static clouds::GranularProcessor clouds_;
static uint8_t clouds_large_[118784];
static uint8_t clouds_small_[65536 - 128];
static clouds::Reverb reverb_;
static uint16_t reverb_buf_[16384];
static float delay_l_[96000], delay_r_[96000];
static int delay_w_ = 0;
static float chorus_l_[4096], chorus_r_[4096];
static int chorus_w_ = 0;
static float chorus_phase_ = 0.0f;
static float delay_tone_l_ = 0.0f, delay_tone_r_ = 0.0f;

static peaks::Lfo peaks_lfo_[2];
static peaks::MultistageEnvelope peaks_env_[2];
static tides::PolySlopeGenerator tides_[2];
static GateFlags mod_prev_gate_[2];
static float tap_last_[2], tap_period_[2];
static float tides_raw_ = 0.0f;
// marbles
static marbles::RandomGenerator marbles_rng_;
static marbles::RandomStream marbles_stream_;
static marbles::TGenerator marbles_t_[2];
static marbles::XYGenerator marbles_xy_[2];
static float marbles_t_env_[2];
// grids
static int grids_step_ = -1;
static float grids_env_[2][2];   // [slot][trigger, accent]
static bool grids_gate_ = false;
// built-in lfos
static float lfo_phase_[3], lfo_val_[3], lfo_sh_[3], lfo_sh_prev_[3];
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
static double clock_phase_ = 0.0;   // in 16ths
static bool any_gate_ = false;
static float master_ = 0.7f;

// io
static float out_l_[1024], out_r_[1024];
static float peak_l_ = 0.0f, peak_r_ = 0.0f;
static float scope_[256];
static float blk_l_[BLOCK], blk_r_[BLOCK];
static int blk_pos_ = BLOCK;

static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
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
    default: return 0.0f;
  }
}

static float mod_sum(int slot, int param, const VoiceState* v) {
  float m = 0.0f;
  for (int i = 0; i < 12; ++i) {
    const Route& r = routes_[i];
    if (r.src == SR_NONE || r.slot != slot || r.param != param) continue;
    float s = source_value(r.src, v) * r.amt;
    if (r.via != SR_NONE) s *= source_value(r.via, v);
    m += s;
  }
  return m;
}

static float P(int slot, int param, const VoiceState* v) {
  float x = base_[slot][param] + mod_sum(slot, param, v) * span_[slot][param];
  return clampf(x, lo_[slot][param], hi_[slot][param]);
}
static int PI(int slot, int param, const VoiceState* v) {
  return (int)(P(slot, param, v) + 0.5f);
}

// ------------------------------------------------------------------ exports
EXPORT float* out_l() { return out_l_; }
EXPORT float* out_r() { return out_r_; }
EXPORT float* scope() { return scope_; }

EXPORT void set_param(int slot, int param, float value) { base_[slot][param] = value; }
EXPORT void set_meta(int slot, int param, float lo, float hi, float span) {
  lo_[slot][param] = lo; hi_[slot][param] = hi; span_[slot][param] = span;
}
EXPORT void set_macro(int i, float v) { macros_[i] = v; }
EXPORT void set_master(float v) { master_ = v; }
EXPORT void set_route(int i, int src, int slot, int param, float amt, int via) {
  routes_[i].src = src; routes_[i].slot = slot; routes_[i].param = param;
  routes_[i].amt = amt; routes_[i].via = via;
}

static void reset_voice_modules(int slot) {
  for (int i = 0; i < MAXV; ++i) {
    if (slot == S_SRC) {
      BufferAllocator alloc(plaits_buf_[i], 16384);
      if (mod_id_[S_SRC] == M_PLAITS) plaits_[i].Init(&alloc);
      if (mod_id_[S_SRC] == M_BRAIDS) braids_[i].Init();
      if (mod_id_[S_SRC] == M_ELEMENTS) { elements_[i].Init(elements_rev_[i]); }
    } else if (slot == S_SHP) {
      if (mod_id_[S_SHP] == M_RINGS) { rings_[i].Init(rings_rev_[i]); rings_[i].set_polyphony(1); }
      if (mod_id_[S_SHP] == M_WARPS) warps_[i].Init(96000.0f);
      svf_[i].Init();
    }
  }
}

EXPORT void set_module(int slot, int id) {
  if (mod_id_[slot] == id) return;
  mod_id_[slot] = id;
  if (slot == S_SRC || slot == S_SHP) reset_voice_modules(slot);
  if (slot == S_MODA || slot == S_MODB) {
    int k = slot - S_MODA;
    peaks_lfo_[k].Init(); peaks_env_[k].Init(); tides_[k].Init(); mod_prev_gate_[k] = 0;
    marbles_t_[k].Init(&marbles_stream_, SR); marbles_xy_[k].Init(&marbles_stream_, SR);
    for (int s = 0; s < 6; ++s) marbles_xy_[k].LoadScale(s, marbles::preset_scales[s]);
    marbles_t_env_[k] = 0.0f; grids_env_[k][0] = grids_env_[k][1] = 0.0f;
    mod_out_[k * 2] = mod_out_[k * 2 + 1] = 0.0f;
  }
  if (slot == S_FX1 || slot == S_FX2) {
    if (id == M_CLOUDS) {
      clouds_.Init(clouds_large_, sizeof(clouds_large_), clouds_small_, sizeof(clouds_small_));
      clouds_.set_num_channels(2); clouds_.set_low_fidelity(false);
    }
    if (id == M_REVERB) reverb_.Init(reverb_buf_);
    if (id == M_DELAY) { memset(delay_l_, 0, sizeof(delay_l_)); memset(delay_r_, 0, sizeof(delay_r_)); }
    if (id == M_CHORUS) { memset(chorus_l_, 0, sizeof(chorus_l_)); memset(chorus_r_, 0, sizeof(chorus_r_)); }
  }
}

EXPORT void set_voices(int n) {
  n = n < 1 ? 1 : n > MAXV ? MAXV : n;
  if (n < nvoices_) for (int i = n; i < MAXV; ++i) { vs_[i].gate = false; vs_[i].active = false; vs_[i].stage = 0; vs_[i].env = 0; }
  nvoices_ = n;
}

EXPORT void note_on(float note, float vel) {
  any_gate_ = true;
  if (nvoices_ == 1) {
    VoiceState& v = vs_[0];
    bool legato = v.gate;
    v.note = note; v.vel = vel; v.gate = true; v.active = true;
    if (!legato) { v.retrig = true; v.stage = 1; v.rnd = randf() * 2.0f - 1.0f; }
    v.age = ++age_counter_; last_voice_ = 0;
    return;
  }
  // free voice, else oldest
  int pick = -1; uint32_t oldest = 0xffffffff;
  for (int i = 0; i < nvoices_; ++i) if (!vs_[i].active) { pick = i; break; }
  if (pick < 0) for (int i = 0; i < nvoices_; ++i) if (!vs_[i].gate && vs_[i].age < oldest) { oldest = vs_[i].age; pick = i; }
  if (pick < 0) { oldest = 0xffffffff; for (int i = 0; i < nvoices_; ++i) if (vs_[i].age < oldest) { oldest = vs_[i].age; pick = i; } }
  VoiceState& v = vs_[pick];
  v.note = note; v.cur_note = note; v.vel = vel; v.gate = true; v.active = true; v.retrig = true;
  v.stage = 1; v.rnd = randf() * 2.0f - 1.0f; v.age = ++age_counter_;
  v.pan = (pick % 2 ? 1.0f : -1.0f) * (0.3f + 0.7f * (pick / 2) / 2.0f);
  last_voice_ = pick;
}
EXPORT void reset_clock() { clock_phase_ = 0.0; }

EXPORT void note_off(float note) {
  for (int i = 0; i < MAXV; ++i) if (vs_[i].gate && fabsf(vs_[i].note - note) < 0.01f) { vs_[i].gate = false; vs_[i].stage = 4; }
  any_gate_ = false;
  for (int i = 0; i < nvoices_; ++i) if (vs_[i].gate) any_gate_ = true;
}
EXPORT void all_off() { for (int i = 0; i < MAXV; ++i) { vs_[i].gate = false; if (vs_[i].stage) vs_[i].stage = 4; } any_gate_ = false; }

EXPORT void init() {
  memset(base_, 0, sizeof(base_));
  for (int s = 0; s < NSLOT; ++s) for (int p = 0; p < NP; ++p) { lo_[s][p] = -1e9f; hi_[s][p] = 1e9f; span_[s][p] = 1.0f; }
  for (int i = 0; i < MAXV; ++i) { memset(&vs_[i], 0, sizeof(VoiceState)); svf_[i].Init(); }
  for (int k = 0; k < 2; ++k) { peaks_lfo_[k].Init(); peaks_env_[k].Init(); tides_[k].Init(); tap_period_[k] = 0.5f; }
  for (int i = 0; i < 12; ++i) routes_[i].src = SR_NONE;
  marbles_rng_.Init(0x2357bd1);
  marbles_stream_.Init(&marbles_rng_);
}

// ------------------------------------------------------------------ envelopes
static inline float time_coef(float t01, float min_s, float max_s) {
  float secs = min_s * powf(max_s / min_s, t01);
  return 1.0f - expf(-1.0f / (secs * SR / BLOCK) * 4.0f);
}

static void run_env(VoiceState& v) {
  float a = P(S_AMP, 0, &v), d = P(S_AMP, 1, &v), s = P(S_AMP, 2, &v), r = P(S_AMP, 3, &v);
  switch (v.stage) {
    case 1: v.env += (1.15f - v.env) * time_coef(a, 0.001f, 6.0f); if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 2; } break;
    case 2: v.env += (s - v.env) * time_coef(d, 0.005f, 8.0f); if (fabsf(v.env - s) < 0.001f) v.stage = 3; break;
    case 3: v.env = s; break;
    case 4: v.env += (0.0f - v.env) * time_coef(r, 0.005f, 10.0f); if (v.env < 0.0005f) { v.env = 0.0f; v.stage = 0; v.active = false; } break;
    default: v.env = 0.0f;
  }
  // mod env (AD, optional loop): AMP params 8 attack, 9 decay, 10 loop
  float ma = P(S_AMP, 8, &v), md = P(S_AMP, 9, &v); bool loop = PI(S_AMP, 10, &v) == 1;
  if (v.retrig) v.mstage = 1;
  if (v.mstage == 1) { v.menv += (1.15f - v.menv) * time_coef(ma, 0.001f, 6.0f); if (v.menv >= 1.0f) { v.menv = 1.0f; v.mstage = 2; } }
  else if (v.mstage == 2) { v.menv += (0.0f - v.menv) * time_coef(md, 0.005f, 8.0f); if (v.menv < 0.002f) { v.menv = 0.0f; v.mstage = loop && v.gate ? 1 : 0; } }
}

// ------------------------------------------------------------------ mod slots
static float division_gate(int which) {   // 0 NOTE 1 GRID 2 1/16 3 1/8 4 1/4 5 BAR
  static const float div[] = { 0, 0, 1, 2, 4, 16 };
  if (which == 0) return any_gate_ ? 1.0f : 0.0f;
  if (which == 1) return grids_gate_ && fmodf((float)clock_phase_, 1.0f) < 0.5f ? 1.0f : 0.0f;
  float p = fmodf((float)clock_phase_, div[which]) / div[which];
  return p < 0.5f ? 1.0f : 0.0f;
}

static void run_mod(int k) {
  int slot = S_MODA + k;
  int id = mod_id_[slot];
  float* o = &mod_out_[k * 2];
  if (id == M_PEAKS) {
    int func = PI(slot, 0, 0);
    float k1 = P(slot, 1, 0), k2 = P(slot, 2, 0);
    bool g = division_gate(PI(slot, 3, 0)) > 0.5f;
    GateFlags flags[BLOCK]; int16_t out[BLOCK];
    for (int i = 0; i < BLOCK; ++i) { flags[i] = ExtractGateFlags(mod_prev_gate_[k], g); mod_prev_gate_[k] = flags[i]; }
    if (func == 0) {
      peaks_env_[k].set_adsr((uint16_t)(k1 * 65535), (uint16_t)(k2 * 65535), 0, (uint16_t)(k2 * 65535));
      peaks_env_[k].Process(flags, out, BLOCK);
      o[0] = out[BLOCK - 1] / 32767.0f; o[1] = o[0];
    } else {
      peaks_lfo_[k].set_sync(func == 2);
      peaks_lfo_[k].set_rate((uint16_t)(k1 * 65535));
      peaks_lfo_[k].set_shape_parameter_preset((uint16_t)(k2 * 65535));
      peaks_lfo_[k].Process(flags, out, BLOCK);
      o[0] = out[BLOCK - 1] / 32767.0f; o[1] = o[0] * 0.5f + 0.5f;
    }
  } else if (id == M_TIDES) {
    int mode = PI(slot, 0, 0);                // AD LOOP AR
    float freq = P(slot, 1, 0), shape = P(slot, 2, 0), slope = P(slot, 3, 0);
    float smooth = P(slot, 4, 0); int range = PI(slot, 5, 0);
    bool g = division_gate(PI(slot, 6, 0)) > 0.5f;
    float hz = (range == 0 ? 0.02f : 0.25f) * powf(2.0f, freq * 8.0f);
    GateFlags flags[BLOCK];
    for (int i = 0; i < BLOCK; ++i) { flags[i] = ExtractGateFlags(mod_prev_gate_[k], g); mod_prev_gate_[k] = flags[i]; }
    tides::PolySlopeGenerator::OutputSample out[BLOCK];
    tides::RampMode rm = mode == 0 ? tides::RAMP_MODE_AD : mode == 1 ? tides::RAMP_MODE_LOOPING : tides::RAMP_MODE_AR;
    tides_[k].Render(rm, tides::OUTPUT_MODE_GATES, tides::RANGE_CONTROL,
        hz / SR, clampf(slope * 0.5f + 0.5f, 0.01f, 0.99f), shape, smooth * 0.5f + 0.5f, 1.0f,
        flags, NULL, out, BLOCK);
    tides_raw_ = out[BLOCK - 1].channel[0];
    float raw = out[BLOCK - 1].channel[0];
    float u = clampf(mode == 1 ? raw / 4.5f * 0.5f + 0.5f : raw / TIDES_AD_MAX, 0.0f, 1.0f);
    o[1] = u;                    // TIDE+ unipolar
    o[0] = u * 2.0f - 1.0f;      // TIDES bipolar
  } else if (id == M_MARBLES) {
    // RATE SPREAD BIAS STEPS | DEJA VU LENGTH JITTER CLOCK
    static const int lengths[] = { 1, 2, 3, 4, 5, 6, 8, 16 };
    static const float divs[] = { 0, 1, 2, 4 };
    int clock = PI(slot, 7, 0);
    float dv = P(slot, 4, 0);
    marbles::TGenerator& t = marbles_t_[k];
    t.set_model(marbles::T_GENERATOR_MODEL_COMPLEMENTARY_BERNOULLI);
    t.set_range(marbles::T_GENERATOR_RANGE_1X);
    t.set_rate((P(slot, 0, 0) - 0.5f) * 72.0f);
    t.set_bias(P(slot, 2, 0)); t.set_jitter(P(slot, 6, 0));
    t.set_deja_vu(dv); t.set_length(lengths[PI(slot, 5, 0)]);
    t.set_pulse_width_mean(0.5f); t.set_pulse_width_std(0.0f);
    float rb[BLOCK * 4]; bool gates[BLOCK * 2]; float volts[BLOCK * 4];
    marbles::Ramps ramps; ramps.master = rb; ramps.external = rb + BLOCK; ramps.slave[0] = rb + BLOCK * 2; ramps.slave[1] = rb + BLOCK * 3;
    GateFlags ext[BLOCK];
    bool ext_gate = clock ? (fmodf((float)clock_phase_, divs[clock]) / divs[clock]) < 0.5f : false;
    for (int i = 0; i < BLOCK; ++i) { ext[i] = ExtractGateFlags(mod_prev_gate_[k], ext_gate); mod_prev_gate_[k] = ext[i]; }
    t.Process(clock != 0, ext, ramps, gates, BLOCK);
    marbles::GroupSettings x, y;
    x.control_mode = marbles::CONTROL_MODE_IDENTICAL; x.voltage_range = marbles::VOLTAGE_RANGE_NARROW;
    x.register_mode = false; x.register_value = 0.0f;
    x.spread = P(slot, 1, 0); x.bias = P(slot, 2, 0); x.steps = P(slot, 3, 0); x.deja_vu = dv;
    x.scale_index = 0; x.length = lengths[PI(slot, 5, 0)]; x.ratio.p = 1; x.ratio.q = 1;
    y = x; y.deja_vu = 0.0f; y.length = 1;
    marbles_xy_[k].Process(marbles::CLOCK_SOURCE_INTERNAL_T1_T2_T3, x, y, ext, ramps, volts, BLOCK);
    o[0] = volts[(BLOCK - 1) * 4 + 0];            // X1 in volts, 0..2 V: 1.0 = one octave on PITCH
    bool tg = gates[(BLOCK - 1) * 2];
    marbles_t_env_[k] = tg ? 1.0f : marbles_t_env_[k] * 0.9f;
    o[1] = marbles_t_env_[k];
  } else if (id == M_GRIDS) {
    // X Y FILL OUT | CHAOS

    int step = ((int)clock_phase_) & 31;
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
    env[0] *= 0.993f; env[1] *= 0.993f;   // ~70 ms trigger envelopes
  } else {
    o[0] = o[1] = 0.0f;
  }
}

static void run_lfos() {
  static const float shapes_div[] = { 0, 1, 2, 4, 8, 16, 32 };   // OFF 1/16 1/8 1/4 1/2 1BAR 2BARS
  bool any = any_gate_;
  bool rising = any && !lfo_any_gate_prev_;
  lfo_any_gate_prev_ = any;
  for (int l = 0; l < 3; ++l) {
    int shape = PI(S_LFO, l * 4 + 0, 0);
    float rate = P(S_LFO, l * 4 + 1, 0);
    int sync = PI(S_LFO, l * 4 + 2, 0);
    bool retrig = PI(S_LFO, l * 4 + 3, 0) == 1;
    float ph;
    if (sync > 0) {
      ph = fmodf((float)clock_phase_, shapes_div[sync]) / shapes_div[sync];
    } else {
      float hz = 0.03f * powf(2.0f, rate * 10.0f);     // 0.03 .. 30 Hz
      lfo_phase_[l] += hz * BLOCK / SR;
      if (retrig && rising) lfo_phase_[l] = 0.0f;
      lfo_phase_[l] -= floorf(lfo_phase_[l]);
      ph = lfo_phase_[l];
    }
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
      default: v = lfo_val_[l] + (lfo_sh_[l] - lfo_val_[l]) * 0.05f; break;
    }
    lfo_val_[l] = v;
  }
}

// ------------------------------------------------------------------ voice render
static float vbuf_[BLOCK], vaux_[BLOCK], tmp_[BLOCK];

static void render_voice(int i, float* mix_l, float* mix_r) {
  VoiceState& v = vs_[i];
  if (!v.active && v.stage == 0) return;
  run_env(v);

  // glide (mono only)
  float glide = P(S_AMP, 5, &v);
  if (nvoices_ == 1 && glide > 0.001f) v.cur_note += (v.note - v.cur_note) * (1.0f - powf(glide, 0.08f) * 0.995f);
  else v.cur_note = v.note;
  float note = v.cur_note + mod_sum(-1, 0, &v) * 12.0f;

  // ---- SRC
  int src = mod_id_[S_SRC];
  if (src == M_PLAITS) {
    plaits::Patch p; plaits::Modulations m;
    memset(&p, 0, sizeof(p)); memset(&m, 0, sizeof(m));
    p.engine = PI(S_SRC, 0, &v);
    p.note = note; p.harmonics = P(S_SRC, 1, &v); p.timbre = P(S_SRC, 2, &v); p.morph = P(S_SRC, 3, &v);
    bool lpg = PI(S_SRC, 4, &v) == 1;
    p.decay = P(S_SRC, 5, &v); p.lpg_colour = P(S_SRC, 6, &v);
    m.trigger_patched = true;
    m.level_patched = !lpg; m.level = 0.4f + 0.6f * v.vel;
    plaits::Voice::Frame fr[BLOCK];
    for (int h = 0; h < 2; ++h) {
      m.trigger = (v.retrig && h == 0) || !v.gate ? 0.0f : 1.0f;   // gate level; a retrigger drops it for half a block
      plaits_[i].Render(p, m, fr + h * 12, 12);
    }
    float lvl = P(S_SRC, 7, &v);
    for (int n = 0; n < BLOCK; ++n) vbuf_[n] = fr[n].out / 32768.0f * lvl;
  } else if (src == M_BRAIDS) {
    braids::MacroOscillator& b = braids_[i];
    b.set_shape((braids::MacroOscillatorShape)PI(S_SRC, 0, &v));
    v.drift += (randf() * 2.0f - 1.0f) * 0.02f; v.drift *= 0.995f;
    float n2 = note + v.drift * P(S_SRC, 3, &v) * 0.6f;
    b.set_pitch((int16_t)clampf((n2 - 12.0f) * 128.0f, 0, 16383));   // Braids runs at 96 kHz natively
    b.set_parameters((int16_t)(P(S_SRC, 1, &v) * 32767), (int16_t)(P(S_SRC, 2, &v) * 32767));
    if (v.retrig) b.Strike();
    int16_t buf[BLOCK]; uint8_t sync[BLOCK]; memset(sync, 0, sizeof(sync));
    b.Render(sync, buf, BLOCK);
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
  } else if (src == M_ELEMENTS) {
    elements::Patch* ep = elements_[i].mutable_patch();
    ep->exciter_bow_level = P(S_SRC, 0, &v); ep->exciter_blow_level = P(S_SRC, 1, &v);
    ep->exciter_strike_level = P(S_SRC, 2, &v); ep->exciter_envelope_shape = P(S_SRC, 3, &v);
    ep->resonator_geometry = P(S_SRC, 4, &v); ep->resonator_brightness = P(S_SRC, 5, &v);
    ep->resonator_damping = P(S_SRC, 6, &v); ep->resonator_position = P(S_SRC, 7, &v);
    ep->exciter_blow_meta = P(S_SRC, 8, &v); ep->exciter_strike_meta = P(S_SRC, 9, &v);
    ep->exciter_bow_timbre = P(S_SRC, 10, &v); ep->space = P(S_SRC, 11, &v) * 2.0f;
    ep->exciter_blow_timbre = 0.5f; ep->exciter_strike_timbre = ep->resonator_brightness;
    ep->exciter_signature = 0.0f; ep->resonator_modulation_frequency = 0.0f; ep->resonator_modulation_offset = 0.0f;
    ep->reverb_diffusion = 0.625f; ep->reverb_lp = 0.7f; ep->modulation_frequency = 0.0f;
    elements::PerformanceState ps;
    ps.gate = v.gate; ps.note = note - 7.02f;   // Elements runs at 32 kHz natively
    ps.modulation = 0.0f; ps.strength = 0.4f + 0.6f * v.vel;
    float zero[12] = { 0 }, a[12], b2[12];
    for (int h = 0; h < 2; ++h) {
      elements_[i].Process(ps, zero, zero, a, b2, 12);
      for (int n = 0; n < 12; ++n) { vbuf_[h * 12 + n] = a[n] * 2.4f; vaux_[h * 12 + n] = b2[n] * 2.4f; }
    }
  } else {
    memset(vbuf_, 0, sizeof(vbuf_));
  }
  bool stereo = src == M_ELEMENTS;
  if (!stereo) memcpy(vaux_, vbuf_, sizeof(vbuf_));

  // ---- SHP
  int shp = mod_id_[S_SHP];
  if (shp == M_RINGS) {
    rings::Part& r = rings_[i];
    r.set_model((rings::ResonatorModel)PI(S_SHP, 0, &v));
    rings::Patch rp; rp.structure = P(S_SHP, 1, &v); rp.brightness = P(S_SHP, 2, &v);
    rp.damping = P(S_SHP, 3, &v); rp.position = P(S_SHP, 4, &v);
    rings::PerformanceState ps;
    ps.strum = v.retrig; ps.internal_exciter = false; ps.internal_strum = false; ps.internal_note = false;
    ps.tonic = 0.0f; ps.note = note; ps.fm = 0.0f; ps.chord = 0;
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
  } else if (shp == M_WARPS) {
    // ALGO TIMBRE CARRIER MOD IN | CAR LVL MOD LVL MIX
    warps::Modulator& w = warps_[i];
    warps::Parameters* wp = w.mutable_parameters();
    int mod_in = PI(S_SHP, 3, &v);                 // 0 OSC, 1 USB IN, 2 NOISE
    wp->modulation_algorithm = P(S_SHP, 0, &v);
    wp->modulation_parameter = P(S_SHP, 1, &v);
    wp->carrier_shape = mod_in == 0 ? 1 + PI(S_SHP, 2, &v) : 0;
    wp->note = note - 12.0f;                       // Warps runs at 96 kHz natively
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
  } else if (shp == M_FILTER) {
    int type = PI(S_SHP, 0, &v);
    float cutoff = P(S_SHP, 1, &v), reso = P(S_SHP, 2, &v), drive = P(S_SHP, 3, &v);
    float env_amt = P(S_SHP, 4, &v), ktrk = P(S_SHP, 5, &v);
    float semis = cutoff * 120.0f - 30.0f + env_amt * v.env * 60.0f + ktrk * (note - 60.0f);
    float f = clampf(440.0f * SemitonesToRatio(semis - 69.0f + 60.0f) / SR, 0.0001f, 0.45f);
    svf_[i].set_f_q<FREQUENCY_FAST>(f, 0.5f + reso * reso * 25.0f);
    float g = 1.0f + drive * 6.0f;
    for (int n = 0; n < BLOCK; ++n) {
      float x = tanhf((vbuf_[n] + vaux_[n]) * 0.5f * g) / (drive > 0.0f ? sqrtf(g) : 1.0f);
      float y = type == 0 ? svf_[i].Process<FILTER_MODE_LOW_PASS>(x) : type == 1 ? svf_[i].Process<FILTER_MODE_BAND_PASS>(x) : svf_[i].Process<FILTER_MODE_HIGH_PASS>(x);
      vbuf_[n] = vaux_[n] = y;
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
  for (int n = 0; n < BLOCK; ++n) { mix_l[n] += vbuf_[n] * gl * 1.41f; mix_r[n] += vaux_[n] * gr * 1.41f; }
}

// ------------------------------------------------------------------ FX
static void run_fx(int slot, float* l, float* r) {
  int id = mod_id_[slot];
  if (id == M_CLOUDS) {
    clouds::Parameters* p = clouds_.mutable_parameters();
    p->position = P(slot, 0, 0); p->size = P(slot, 1, 0); p->pitch = P(slot, 2, 0) * 24.0f;
    p->density = P(slot, 3, 0); p->texture = P(slot, 4, 0); p->dry_wet = P(slot, 5, 0);
    p->stereo_spread = P(slot, 6, 0); p->feedback = P(slot, 7, 0); p->reverb = P(slot, 8, 0);
    clouds_.set_playback_mode((clouds::PlaybackMode)PI(slot, 9, 0));
    p->freeze = PI(slot, 10, 0) == 1;
    p->trigger = false; p->gate = false;
    clouds_.Prepare();
    clouds::ShortFrame in[BLOCK], out[BLOCK];
    for (int n = 0; n < BLOCK; ++n) {
      in[n].l = (int16_t)(clampf(l[n], -1.0f, 1.0f) * 32767.0f);
      in[n].r = (int16_t)(clampf(r[n], -1.0f, 1.0f) * 32767.0f);
    }
    clouds_.Process(in, out, BLOCK);
    for (int n = 0; n < BLOCK; ++n) { l[n] = out[n].l / 32768.0f; r[n] = out[n].r / 32768.0f; }
  } else if (id == M_REVERB) {
    reverb_.set_amount(P(slot, 3, 0) * 0.6f);
    reverb_.set_time(0.35f + P(slot, 0, 0) * 0.63f);
    reverb_.set_diffusion(0.3f + 0.6f * P(slot, 2, 0));
    reverb_.set_lp(0.95f - P(slot, 1, 0) * 0.6f);
    reverb_.set_input_gain(0.25f);
    clouds::FloatFrame fr[BLOCK];
    for (int n = 0; n < BLOCK; ++n) { fr[n].l = l[n]; fr[n].r = r[n]; }
    reverb_.Process(fr, BLOCK);
    for (int n = 0; n < BLOCK; ++n) { l[n] = fr[n].l; r[n] = fr[n].r; }
  } else if (id == M_DELAY) {
    static const float beats[] = { 0.25f, 0.5f, 0.75f, 1.0f, 2.0f };
    float bpm = base_[S_GLO][0] > 1.0f ? base_[S_GLO][0] : 112.0f;
    int samples = (int)(beats[PI(slot, 0, 0)] * 60.0f / bpm * SR);
    if (samples > 95999) samples = 95999;
    float fb = P(slot, 1, 0) * 0.92f, tone = P(slot, 2, 0), mix = P(slot, 3, 0);
    float lp = 0.05f + tone * 0.9f;
    for (int n = 0; n < BLOCK; ++n) {
      int rd = delay_w_ - samples; if (rd < 0) rd += 96000;
      float dl = delay_l_[rd], dr = delay_r_[rd];
      delay_tone_l_ += (dl - delay_tone_l_) * lp; delay_tone_r_ += (dr - delay_tone_r_) * lp;
      delay_l_[delay_w_] = l[n] + delay_tone_r_ * fb;    // ping-pong
      delay_r_[delay_w_] = r[n] * 0.5f + delay_tone_l_ * fb;
      delay_w_ = (delay_w_ + 1) % 96000;
      l[n] += (delay_tone_l_ - 0.0f) * mix; r[n] += delay_tone_r_ * mix;
    }
  } else if (id == M_CHORUS) {
    float rate = 0.05f + P(slot, 0, 0) * 4.0f, depth = P(slot, 1, 0), width = P(slot, 2, 0), mix = P(slot, 3, 0);
    for (int n = 0; n < BLOCK; ++n) {
      chorus_phase_ += rate / SR; if (chorus_phase_ >= 1.0f) chorus_phase_ -= 1.0f;
      float s = sinf(chorus_phase_ * 6.2831853f);
      chorus_l_[chorus_w_] = l[n]; chorus_r_[chorus_w_] = r[n];
      float dl = 240.0f + (1.0f + s) * depth * 600.0f, dr = 240.0f + (1.0f - s * width) * depth * 600.0f;
      auto tap = [](float* buf, int w, float d) {
        float pos = w - d; while (pos < 0) pos += 4096.0f;
        int i0 = (int)pos; float fr = pos - i0;
        return buf[i0 & 4095] * (1.0f - fr) + buf[(i0 + 1) & 4095] * fr;
      };
      float wl = tap(chorus_l_, chorus_w_, dl), wr = tap(chorus_r_, chorus_w_, dr);
      chorus_w_ = (chorus_w_ + 1) & 4095;
      l[n] = l[n] * (1.0f - mix * 0.5f) + wl * mix; r[n] = r[n] * (1.0f - mix * 0.5f) + wr * mix;
    }
  }
}

// ------------------------------------------------------------------ block + render
static void process_block() {
  float bpm = base_[S_GLO][0] > 1.0f ? base_[S_GLO][0] : 112.0f;
  clock_phase_ += BLOCK / SR * bpm / 60.0 * 4.0;
  if (clock_phase_ > 4096.0) clock_phase_ -= 4096.0;
  run_lfos();
  run_mod(0); run_mod(1);
  memset(blk_l_, 0, sizeof(blk_l_)); memset(blk_r_, 0, sizeof(blk_r_));
  for (int i = 0; i < nvoices_; ++i) render_voice(i, blk_l_, blk_r_);
  run_fx(S_FX1, blk_l_, blk_r_);
  run_fx(S_FX2, blk_l_, blk_r_);
  for (int n = 0; n < BLOCK; ++n) {
    blk_l_[n] = SoftClip(blk_l_[n] * master_ * 0.9f);
    blk_r_[n] = SoftClip(blk_r_[n] * master_ * 0.9f);
  }
}

EXPORT void render(int n) {
  for (int i = 0; i < n; ++i) {
    if (blk_pos_ >= BLOCK) { process_block(); blk_pos_ = 0; }
    out_l_[i] = blk_l_[blk_pos_]; out_r_[i] = blk_r_[blk_pos_]; ++blk_pos_;
  }
  for (int i = 0; i < 128 && i < n; ++i) scope_[i] = (out_l_[i] + out_r_[i]) * 0.5f;
  for (int i = 0; i < n; ++i) { float a = fabsf(out_l_[i]), b = fabsf(out_r_[i]); if (a > peak_l_) peak_l_ = a; if (b > peak_r_) peak_r_ = b; }
}

// monitor: what the screen draws. 0-15 modulated params of `slot` (as the most recent voice hears them),
// 16 envs, 22 mod envs, 28 gates, 34 notes, 40 mod outs, 44 lfos, 47 clock (16ths), 48 peaks, 50 route values,
// 62 last voice, 63 voices; 64-159 Grids levels (3 x 32) when `slot` holds Grids.
static float mon_[160];
EXPORT float* monitor(int slot) {
  const VoiceState* v = &vs_[last_voice_];
  if (slot >= 0 && slot < NSLOT) for (int p = 0; p < 16; ++p) mon_[p] = P(slot, p, v);
  for (int i = 0; i < MAXV; ++i) {
    mon_[16 + i] = vs_[i].env; mon_[22 + i] = vs_[i].menv;
    mon_[28 + i] = i < nvoices_ ? (vs_[i].gate ? 1.0f : vs_[i].stage ? 0.5f : 0.0f) : -1.0f;
    mon_[34 + i] = vs_[i].cur_note;
  }
  for (int i = 0; i < 4; ++i) mon_[40 + i] = mod_out_[i];
  for (int i = 0; i < 3; ++i) mon_[44 + i] = lfo_val_[i];
  mon_[47] = (float)clock_phase_;
  mon_[48] = peak_l_; mon_[49] = peak_r_; peak_l_ *= 0.5f; peak_r_ *= 0.5f;
  for (int i = 0; i < 12; ++i) {
    const Route& r = routes_[i];
    float x = 0.0f;
    if (r.src != SR_NONE) { x = source_value(r.src, v) * r.amt; if (r.via != SR_NONE) x *= source_value(r.via, v); }
    mon_[50 + i] = x;
  }
  mon_[62] = (float)last_voice_; mon_[63] = (float)nvoices_;
  if ((slot == S_MODA || slot == S_MODB) && mod_id_[slot] == M_GRIDS)
    for (int inst = 0; inst < 3; ++inst) for (int st = 0; st < 32; ++st) mon_[64 + inst * 32 + st] = grids_level(slot, inst, st) / 255.0f;
  return mon_;
}
EXPORT float voice_env(int i) { return vs_[i].env; }
EXPORT float mod_value(int i) { return mod_out_[i]; }
