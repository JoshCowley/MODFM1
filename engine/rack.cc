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
  float menv; int mstage;        // mod env: 0 idle 1 att 2 dec 3 sus 4 rel
  float tm;                      // seconds spent in the AMP delay or hold stage
  float fenv; int fstage;        // the filter's own ADSR
  float benv; int bstage;        // Braids' internal AD
};

static VoiceState vs_[MAXV];
static plaits::Voice plaits_[MAXV];
static char plaits_buf_[MAXV][16384];
static braids::MacroOscillator braids_[MAXV];
static rings::Part rings_[MAXV];
static uint16_t rings_rev_[MAXV][32768];
static elements::Part elements_[MAXV];
static uint16_t elements_rev_[MAXV][32768];
static Svf svf_[MAXV], svf2_[MAXV];
static warps::Modulator warps_[MAXV];
static int nvoices_ = 2;
static int last_voice_ = 0;
static uint32_t age_counter_ = 0;

// ------------------------------------------------------------------ shared
static clouds::GranularProcessor clouds_;
static int clouds_quality_ = 0;
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
// shimmer: an octave-up pitch shifter (two crossfaded grains) feeding the reverb
static int16_t shim_buf_[4096]; static int shim_w_ = 0; static float shim_ph_ = 0.0f, shim_fb_ = 0.0f;
static int16_t pre_buf_l_[8192], pre_buf_r_[8192]; static int pre_w_ = 0;
static float tape_ph1_ = 0.0f, tape_ph2_ = 0.0f;
static float rv_lc_l_ = 0, rv_lc_r_ = 0, rv_duck_ = 0, dl_lc_l_ = 0, dl_lc_r_ = 0, dl_duck_ = 0, ch_fb_l_ = 0, ch_fb_r_ = 0;

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
    case SR_SEQ1: case SR_SEQ2: case SR_SEQ3: case SR_SEQ4: case SR_WHEEL: case SR_PRESS: return ext_cur_[s - SR_SEQ1];
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
EXPORT void set_ext(int i, float v) { if (i >= 0 && i < 6) ext_tgt_[i] = v; }
EXPORT void set_ext_slew(int i, float s) { if (i >= 0 && i < 6) ext_slew_[i] = clampf(s, 0.001f, 1.0f); }
EXPORT void set_tune(int note, float semis) { if (note >= 0 && note < 128) tune_[note] = semis; }
EXPORT void set_bend(float semis) { bend_ = semis; }
EXPORT void set_vintage(float a) { vintage_ = a; }
EXPORT void set_legato(int on) { legato_ = on != 0; }
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
      svf_[i].Init(); svf2_[i].Init();
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
      clouds_.set_num_channels(2); clouds_.set_low_fidelity(false); clouds_quality_ = 0;
    }
    if (id == M_REVERB) { reverb_.Init(reverb_buf_); memset(shim_buf_, 0, sizeof(shim_buf_)); memset(pre_buf_l_, 0, sizeof(pre_buf_l_)); memset(pre_buf_r_, 0, sizeof(pre_buf_r_)); shim_fb_ = 0; }
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
    bool legato = v.gate && legato_;
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
  for (int i = 0; i < MAXV; ++i) { memset(&vs_[i], 0, sizeof(VoiceState)); svf_[i].Init(); svf2_[i].Init(); card_[i] = randf() * 2.0f - 1.0f; vdrift_[i] = 0.0f; }
  memset(tune_, 0, sizeof(tune_)); bend_ = 0.0f; vintage_ = 0.0f; legato_ = true;
  for (int i = 0; i < 6; ++i) ext_tgt_[i] = ext_cur_[i] = 0.0f;
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
    tides_[k].Render(rm, tides::OUTPUT_MODE_GATES, tides::RANGE_CONTROL,
        hz / SR, clampf(slope * 0.5f + 0.5f, 0.01f, 0.99f), shape, smooth * 0.5f + 0.5f, 1.0f,
        flags, NULL, out, BLOCK);
    tides_raw_ = out[BLOCK - 1].channel[0];
    float raw = out[BLOCK - 1].channel[0];
    float u = clampf(mode == 1 ? raw / 4.5f * 0.5f + 0.5f : raw / TIDES_AD_MAX, 0.0f, 1.0f);
    if (PI(slot, 7, 0) == 1) u = 1.0f - u;   // INVERT
    o[1] = u;                    // TIDE+ unipolar
    o[0] = u * 2.0f - 1.0f;      // TIDES bipolar
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
    bool ext_gate = clock ? (fmodf((float)clock_phase_, divs[clock]) / divs[clock]) < 0.5f : false;
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
      ph = fmodf((float)clock_phase_, shapes_div[sync]) / shapes_div[sync];
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
    for (int h = 0; h < 2; ++h) {
      m.trigger = (v.retrig && h == 0) || !v.gate ? 0.0f : 1.0f;   // gate level; a retrigger drops it for half a block
      plaits_[i].Render(p, m, fr + h * 12, 12);
    }
    float lvl = P(S_SRC, 7, &v);
    if (aux > 0.0f) for (int n = 0; n < BLOCK; ++n) vbuf_[n] = (fr[n].out * (1.0f - aux) + fr[n].aux * aux) / 32768.0f * lvl;
    else for (int n = 0; n < BLOCK; ++n) vbuf_[n] = fr[n].out / 32768.0f * lvl;
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
    if (vca) for (int n = 0; n < BLOCK; ++n) vbuf_[n] *= v.benv;
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
    float g = 1.0f + drive * 6.0f, norm = drive > 0.0f ? sqrtf(g) : 1.0f;
    for (int n = 0; n < BLOCK; ++n) {
      float in = (vbuf_[n] + vaux_[n]) * 0.5f, xg = in * g, x;
      switch (sat) {
        case 1: x = clampf(xg, -1.0f, 1.0f) / norm; break;                        // HARD
        case 2: x = sinf(clampf(xg, -4.0f, 4.0f) * 1.5707963f) / norm; break;      // FOLD
        case 3: x = (xg >= 0.0f ? tanhf(xg) : tanhf(xg * 0.6f) / 0.6f) / norm; break;   // ASYM
        default: x = tanhf(xg) / norm;                                             // SOFT (as before)
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
  for (int n = 0; n < BLOCK; ++n) { mix_l[n] += vbuf_[n] * gl * 1.41f; mix_r[n] += vaux_[n] * gr * 1.41f; }
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
static float comp_env_ = 0.0f, comp_gr_db_ = 0.0f, lim_gain_ = 1.0f, lim_gr_db_ = 0.0f;
static float lim_buf_l_[64], lim_buf_r_[64]; static int lim_w_ = 0;
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
    for (int n = 0; n < BLOCK; ++n) {
      float x = fmaxf(fabsf(l[n]), fabsf(r[n]));
      comp_env_ += (x - comp_env_) * (x > comp_env_ ? 0.02f : 0.0004f);
      float lvl = 20.0f * log10f(comp_env_ + 1e-6f), over = lvl - thr;
      float gr = over > 0 ? over * (1.0f - 1.0f / ratio) : 0.0f;
      comp_gr_db_ = gr;
      float g = powf(10.0f, (makeup - gr) / 20.0f);
      l[n] *= g; r[n] *= g;
    }
  } else comp_gr_db_ = 0.0f;
  // limiter: looks 48 samples (1 ms) ahead, so a loud attack is already turned down when it arrives
  for (int n = 0; n < BLOCK; ++n) {
    lim_buf_l_[lim_w_] = l[n]; lim_buf_r_[lim_w_] = r[n];
    float peak = 0.0f; for (int k = 0; k < 48; ++k) { int i2 = (lim_w_ - k) & 63; peak = fmaxf(peak, fmaxf(fabsf(lim_buf_l_[i2]), fabsf(lim_buf_r_[i2]))); }
    float target = peak > 0.89f ? 0.89f / peak : 1.0f;
    lim_gain_ += (target - lim_gain_) * (target < lim_gain_ ? 0.3f : 0.0005f);
    int rd = (lim_w_ - 47) & 63;
    l[n] = lim_buf_l_[rd] * lim_gain_; r[n] = lim_buf_r_[rd] * lim_gain_;
    lim_w_ = (lim_w_ + 1) & 63;
  }
  lim_gr_db_ = -20.0f * log10f(lim_gain_ + 1e-6f);
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
    // page 3-4: QUALITY (16/8-bit, stereo/mono, as the hardware's button), IN GAIN, NOTE TRG (a grain on each new note), PITCH TRK
    int qual = PI(slot, 11, 0); if (qual != clouds_quality_) { clouds_quality_ = qual; clouds_.set_quality(qual); }
    static bool cl_prev = false; bool trg = PI(slot, 13, 0) == 1 && any_gate_ && !cl_prev; cl_prev = any_gate_;
    p->trigger = trg; p->gate = trg;
    if (PI(slot, 14, 0) == 1) p->pitch = clampf(p->pitch + vs_[last_voice_].note - 60.0f, -48.0f, 48.0f);
    float ig = P(slot, 12, 0), igain = ig != 0.0f ? powf(10.0f, ig * 12.0f / 20.0f) : 1.0f;
    clouds_.Prepare();
    clouds::ShortFrame in[BLOCK], out[BLOCK];
    for (int n = 0; n < BLOCK; ++n) {
      in[n].l = (int16_t)(clampf(l[n] * igain, -1.0f, 1.0f) * 32767.0f);
      in[n].r = (int16_t)(clampf(r[n] * igain, -1.0f, 1.0f) * 32767.0f);
    }
    clouds_.Process(in, out, BLOCK);
    for (int n = 0; n < BLOCK; ++n) { l[n] = out[n].l / 32768.0f; r[n] = out[n].r / 32768.0f; }
  } else if (id == M_REVERB) {
    int type = PI(slot, 4, 0);                     // 0 PLATE, 1 HALL, 2 SHIMMER
    float size = P(slot, 0, 0), mix = P(slot, 3, 0);
    float time = type == 0 ? 0.35f + size * 0.63f : 0.6f + size * 0.39f;
    reverb_.set_amount(1.0f);
    reverb_.set_time(time);
    reverb_.set_diffusion(0.3f + 0.6f * P(slot, 2, 0));
    reverb_.set_lp((type == 1 ? 0.8f : 0.95f) - P(slot, 1, 0) * 0.6f);
    reverb_.set_input_gain(type == 0 ? 0.25f : 0.2f);
    int pre = (int)(P(slot, 5, 0) * 0.15f * SR); if (pre > 7999) pre = 7999;
    float lc = P(slot, 7, 0), width = P(slot, 8, 0), duck = P(slot, 9, 0);
    float lcc = lc > 0.0f ? 1.0f - expf(-6.2831853f * (20.0f + lc * lc * 980.0f) / SR) : 0.0f;
    float shim = type == 2 ? P(slot, 6, 0) : 0.0f;
    clouds::FloatFrame fr[BLOCK];
    float dry_l[BLOCK], dry_r[BLOCK];
    for (int n = 0; n < BLOCK; ++n) {
      dry_l[n] = l[n]; dry_r[n] = r[n];
      pre_buf_l_[pre_w_] = (int16_t)(clampf(l[n], -1.0f, 1.0f) * 32767.0f); pre_buf_r_[pre_w_] = (int16_t)(clampf(r[n], -1.0f, 1.0f) * 32767.0f);
      int rd = (pre_w_ - pre) & 8191; pre_w_ = (pre_w_ + 1) & 8191;
      float in_l = pre_buf_l_[rd] / 32768.0f, in_r = pre_buf_r_[rd] / 32768.0f;
      if (lc > 0.0f) { rv_lc_l_ += (in_l - rv_lc_l_) * lcc; rv_lc_r_ += (in_r - rv_lc_r_) * lcc; in_l -= rv_lc_l_; in_r -= rv_lc_r_; }   // LO CUT: thin the tail
      if (shim > 0.0f) {         // the tail, an octave up, back into the tank
        shim_buf_[shim_w_] = (int16_t)(clampf(shim_fb_, -1.0f, 1.0f) * 32767.0f);
        shim_ph_ += 1.0f / 2048.0f; if (shim_ph_ >= 1.0f) shim_ph_ -= 1.0f;
        float d1 = (1.0f - shim_ph_) * 2048.0f, d2 = (1.0f - fmodf(shim_ph_ + 0.5f, 1.0f)) * 2048.0f;
        auto rdbuf = [](float d) { float pos = shim_w_ - d; while (pos < 0) pos += 4096.0f; int i0 = (int)pos; float f = pos - i0; return (shim_buf_[i0 & 4095] * (1 - f) + shim_buf_[(i0 + 1) & 4095] * f) / 32768.0f; };
        float w1 = sinf(3.14159265f * shim_ph_), w2 = sinf(3.14159265f * fmodf(shim_ph_ + 0.5f, 1.0f));
        float up = rdbuf(d1) * w1 + rdbuf(d2) * w2;
        shim_w_ = (shim_w_ + 1) & 4095;
        in_l += up * shim * 0.7f; in_r += up * shim * 0.7f;
      }
      fr[n].l = in_l; fr[n].r = in_r;
    }
    reverb_.Process(fr, BLOCK);
    for (int n = 0; n < BLOCK; ++n) {
      shim_fb_ = (fr[n].l + fr[n].r) * 0.5f;
      float wet = mix * (type == 1 ? 0.75f : 0.6f);
      float wl = fr[n].l, wr = fr[n].r;
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
    if (samples > 95999) samples = 95999;
    float fb = P(slot, 1, 0) * 0.92f, tone = P(slot, 2, 0), mix = P(slot, 3, 0);
    int mode = PI(slot, 4, 0); float wow = P(slot, 5, 0);      // 0 PING, 1 MONO, 2 TAPE
    float lp = 0.05f + tone * 0.9f;
    for (int n = 0; n < BLOCK; ++n) {
      float dsamp = (float)samples;
      if (mode == 2) {           // wow and flutter swing the read point; the repeats saturate as they pile up
        tape_ph1_ += 0.45f / SR; if (tape_ph1_ >= 1.0f) tape_ph1_ -= 1.0f;
        tape_ph2_ += 6.3f / SR; if (tape_ph2_ >= 1.0f) tape_ph2_ -= 1.0f;
        dsamp += wow * (sinf(tape_ph1_ * 6.2831853f) * 90.0f + sinf(tape_ph2_ * 6.2831853f) * 6.0f);
      }
      float pos = delay_w_ - dsamp; while (pos < 0) pos += 96000.0f;
      int i0 = (int)pos; float fr2 = pos - i0; int i1 = (i0 + 1) % 96000; i0 %= 96000;
      float dl = delay_l_[i0] * (1 - fr2) + delay_l_[i1] * fr2, dr = delay_r_[i0] * (1 - fr2) + delay_r_[i1] * fr2;
      delay_tone_l_ += (dl - delay_tone_l_) * lp; delay_tone_r_ += (dr - delay_tone_r_) * lp;
      if (lc > 0.0f) { dl_lc_l_ += (delay_tone_l_ - dl_lc_l_) * lcc; dl_lc_r_ += (delay_tone_r_ - dl_lc_r_) * lcc; delay_tone_l_ -= dl_lc_l_; delay_tone_r_ -= dl_lc_r_; }
      if (mode == 0) {           // ping-pong
        delay_l_[delay_w_] = l[n] + delay_tone_r_ * fb;
        delay_r_[delay_w_] = r[n] * 0.5f + delay_tone_l_ * fb;
      } else if (mode == 1) {
        float m = (l[n] + r[n]) * 0.5f;
        delay_l_[delay_w_] = m + delay_tone_l_ * fb; delay_r_[delay_w_] = m + delay_tone_r_ * fb;
      } else {
        delay_l_[delay_w_] = SoftClip(l[n] + delay_tone_l_ * fb * 1.05f); delay_r_[delay_w_] = SoftClip(r[n] + delay_tone_r_ * fb * 1.05f);
      }
      delay_w_ = (delay_w_ + 1) % 96000;
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
    for (int n = 0; n < BLOCK; ++n) {
      chorus_phase_ += rate / SR; if (chorus_phase_ >= 1.0f) chorus_phase_ -= 1.0f;
      float s = sinf(chorus_phase_ * 6.2831853f);
      chorus_l_[chorus_w_] = cfb > 0.0f ? l[n] + ch_fb_l_ * cfb : l[n]; chorus_r_[chorus_w_] = cfb > 0.0f ? r[n] + ch_fb_r_ * cfb : r[n];
      float dl = base + (1.0f + s) * depth * swing, dr = base + (1.0f - s * width) * depth * swing;
      auto tap = [](float* buf, int w, float d) {
        float pos = w - d; while (pos < 0) pos += 4096.0f;
        int i0 = (int)pos; float fr = pos - i0;
        return buf[i0 & 4095] * (1.0f - fr) + buf[(i0 + 1) & 4095] * fr;
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
static void process_block() {
  float bpm = base_[S_GLO][0] > 1.0f ? base_[S_GLO][0] : 112.0f;
  clock_phase_ += BLOCK / SR * bpm / 60.0 * 4.0;
  if (clock_phase_ > 4096.0) clock_phase_ -= 4096.0;
  for (int i = 0; i < 6; ++i) ext_cur_[i] += (ext_tgt_[i] - ext_cur_[i]) * ext_slew_[i];
  run_lfos();
  run_mod(0); run_mod(1);
  memset(blk_l_, 0, sizeof(blk_l_)); memset(blk_r_, 0, sizeof(blk_r_));
  for (int i = 0; i < nvoices_; ++i) render_voice(i, blk_l_, blk_r_);
  run_fx(S_FX1, blk_l_, blk_r_);
  run_fx(S_FX2, blk_l_, blk_r_);
  for (int n = 0; n < BLOCK; ++n) { blk_l_[n] *= master_ * 0.9f; blk_r_[n] *= master_ * 0.9f; }
  run_master(blk_l_, blk_r_);
  for (int n = 0; n < BLOCK; ++n) { blk_l_[n] = SoftClip(blk_l_[n]); blk_r_[n] = SoftClip(blk_r_[n]); }
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
  mon_[47] = (float)clock_phase_;
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
