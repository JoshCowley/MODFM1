// The engine's C interface (engine/rack.h) under names of FM1 Rack's own: fm1-base's unity build, which the
// firmware's C is part of, has functions called init, render and so on of its own. Built as one of the C++ units.
// (fm1/bench has its own copy, as it was built and tested.)
#include <stdint.h>
#include "../../engine/rack.h"
#include "stmlib/utils/random.h"

extern "C" {
void rk_init(void) { init(); }
void rk_seed(uint32_t s) { stmlib::Random::Seed(s); }   // each case starts from the same random numbers
void rk_set_module(int slot, int id) { set_module(slot, id); }
void rk_set_param(int slot, int param, float v) { set_param(slot, param, v); }
void rk_set_meta(int slot, int param, float lo, float hi, float span) { set_meta(slot, param, lo, hi, span); }
void rk_set_route(int i, int src, int slot, int param, float amt, int via) { set_route(i, src, slot, param, amt, via); }
void rk_set_macro(int i, float v) { set_macro(i, v); }
void rk_set_master(float v) { set_master(v); }
void rk_set_master_fx(float lo, float mid, float hi, float comp) { set_master_fx(lo, mid, hi, comp); }
void rk_set_legato(int on) { set_legato(on); }
void rk_set_vintage(float v) { set_vintage(v); }
void rk_set_ext_slew(int i, float s) { set_ext_slew(i, s); }
void rk_set_tune(int note, float semis) { set_tune(note, semis); }
void rk_set_voices(int n) { set_voices(n); }
void rk_note_on(float note, float vel) { note_on(note, vel); }
void rk_note_off(float note) { note_off(note); }
void rk_set_bend(float semis) { set_bend(semis); }
void rk_all_off(void) { all_off(); }
void rk_render(int n) { render(n); }
float *rk_out_l(void) { return out_l(); }
float *rk_out_r(void) { return out_r(); }
int rk_fm1_status(int what) { return fm1_status(what); }
void rk_fm1_fit(void) { fm1_fit(); }
float rk_voice_env(int i) { return voice_env(i); }
// the second core: the platform's worker (plat_cpu2_run, plat_cpu2_wait) renders half the voices (rack.cc)
void rk_set_worker(int (*run)(void (*)(uint32_t), uint32_t), int (*wait)(void)) { set_worker(run, wait); }
uint32_t rk_worker_blocks(void) { return worker_blocks(); }
}
