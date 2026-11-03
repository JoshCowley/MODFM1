// FM1 Rack app glue: the part of the firmware between the board (fm1_platform.h) and the engine (rack.h).
// Status: compiles and runs on a computer against a fake board (firmware/desktop/fake_board.cc); never run
// on an FM-1. The screen pages, the sequencer, chords and patch storage are still to be ported from web/src.
//
// What it does now:
//   - a lock-free queue carries engine calls from the UI/MIDI context to the audio interrupt, which applies
//     them before rendering (the engine must never be changed while render() runs);
//   - the audio callback renders 48 kHz floats and converts them to 16-bit with clipping;
//   - the 27 keys and USB/TRS MIDI notes play the engine; a starting patch is loaded at init.
#include "fm1_platform.h"
#include "../engine/rack.h"
#include <string.h>

namespace {

// ---- engine call queue: one producer (main context), one consumer (audio interrupt) ----
enum Fn : uint8_t { F_MODULE, F_PARAM, F_META, F_ROUTE, F_MACRO, F_MASTER, F_VOICES, F_NOTE_ON, F_NOTE_OFF, F_ALL_OFF };
struct Call { Fn fn; int16_t a, b, c; float x, y, z; };
const int QN = 256;   // power of two
Call queue_[QN];
volatile uint32_t q_head_ = 0, q_tail_ = 0;   // head: written by main; tail: written by audio

bool push(const Call& c) {
  uint32_t h = q_head_;
  if (h - q_tail_ >= (uint32_t)QN) return false;   // full: drop (the UI resends state on change)
  queue_[h & (QN - 1)] = c;
  __atomic_thread_fence(__ATOMIC_RELEASE);          // OPEN: check this builds to a barrier on pi32v2
  q_head_ = h + 1;
  return true;
}
void apply_queue() {
  uint32_t t = q_tail_, h = q_head_;
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  for (; t != h; ++t) {
    const Call& c = queue_[t & (QN - 1)];
    switch (c.fn) {
      case F_MODULE: set_module(c.a, c.b); break;
      case F_PARAM: set_param(c.a, c.b, c.x); break;
      case F_META: set_meta(c.a, c.b, c.x, c.y, c.z); break;
      case F_ROUTE: set_route(c.a, (int)c.x, c.b, c.c, c.y, (int)c.z); break;
      case F_MACRO: set_macro(c.a, c.x); break;
      case F_MASTER: set_master(c.x); break;
      case F_VOICES: set_voices(c.a); break;
      case F_NOTE_ON: note_on(c.x, c.y); break;
      case F_NOTE_OFF: note_off(c.x); break;
      case F_ALL_OFF: all_off(); break;
    }
  }
  q_tail_ = t;
}
void call(Fn fn, int a = 0, int b = 0, int c = 0, float x = 0, float y = 0, float z = 0) { push(Call{ fn, (int16_t)a, (int16_t)b, (int16_t)c, x, y, z }); }

// a starting patch, as the page's INIT: Plaits (virtual analog) > filter > reverb. Values are the page's
// defaults; ranges (set_meta) are what modulation needs: lists 0..n-1, bipolar -1..1, the rest 0..1.
void load_init_patch() {
  call(F_MODULE, 0, 1);   // SRC Plaits
  call(F_MODULE, 1, 12);  // SHP filter
  call(F_MODULE, 5, 32);  // FX 2 reverb
  const float plaits[] = { 8, .42f, .6f, .31f, 0, .5f, .5f, .8f };
  for (int i = 0; i < 8; ++i) { call(F_META, 0, i, 0, 0, i == 0 ? 23 : 1, i == 0 ? 23 : 1); call(F_PARAM, 0, i, 0, plaits[i]); }
  const float filt[] = { 0, .55f, .3f, .2f };
  for (int i = 0; i < 4; ++i) { call(F_META, 1, i, 0, 0, i == 0 ? 5 : 1, i == 0 ? 5 : 1); call(F_PARAM, 1, i, 0, filt[i]); }
  const float verb[] = { .6f, .5f, .55f, .3f };
  for (int i = 0; i < 4; ++i) { call(F_META, 5, i, 0, 0, 1, 1); call(F_PARAM, 5, i, 0, verb[i]); }
  const float ampv[] = { .02f, .4f, .7f, .35f, .6f, 0, .3f, .8f };
  for (int i = 0; i < 8; ++i) { call(F_META, 6, i, 0, 0, 1, 1); call(F_PARAM, 6, i, 0, ampv[i]); }
  call(F_PARAM, 7, 0, 0, 112);   // tempo
  call(F_MASTER, 0, 0, 0, .7f);
  call(F_VOICES, 6);
}

const int KEY_BASE = 53;   // key 0 = F3
int octave_ = 0;

}  // namespace

extern "C" void fm1_app_init(void) {
  init();
  // OPEN QUESTION: the engine runs at 48 kHz. If the codec runs at ~44.1 kHz, either the platform runs it
  // at 48 kHz, or the engine's rate is changed (SR in rack.cc and the modules' own rate constants), or the
  // output is resampled. Until that's decided, at 44.1 kHz everything plays about 1.5 semitones flat.
  (void)fm1_hal_sample_rate();
  load_init_patch();
}

extern "C" void fm1_app_audio(int16_t* lr, int frames) {
  apply_queue();
  int done = 0;
  while (done < frames) {
    int n = frames - done; if (n > 256) n = 256;   // out_l/out_r hold 1024 frames
    render(n);
    const float* l = out_l(); const float* r = out_r();
    for (int i = 0; i < n; ++i) {
      float a = l[i], b = r[i];
      a = a > 1 ? 1 : a < -1 ? -1 : a; b = b > 1 ? 1 : b < -1 ? -1 : b;
      lr[2 * (done + i)] = (int16_t)(a * 32767.0f);
      lr[2 * (done + i) + 1] = (int16_t)(b * 32767.0f);
    }
    done += n;
  }
}

extern "C" void fm1_app_ui(uint32_t) {
  // TODO port: screen pages (web/src/2-screen.js, 3-vis.js, 4-pages.js), sequencer, chords, storage
}

extern "C" void fm1_app_key(int key, int down, float velocity) {
  float note = (float)(KEY_BASE + key + 12 * octave_);
  if (down) call(F_NOTE_ON, 0, 0, 0, note, velocity); else call(F_NOTE_OFF, 0, 0, 0, note);
}

extern "C" void fm1_app_knob(int knob, int delta) {
  (void)knob; (void)delta;   // TODO port: encoder roles from web/src/5-controls.js
}

extern "C" void fm1_app_button(int button, int down) {
  if (!down) return;
  if (button == 100) octave_ = octave_ > -3 ? octave_ - 1 : octave_;   // OCT-  (button ids: to be fixed by the port)
  if (button == 101) octave_ = octave_ < 3 ? octave_ + 1 : octave_;    // OCT+
}

extern "C" void fm1_app_midi(const uint8_t* m, int n) {
  if (n < 3) return;
  uint8_t st = m[0] & 0xF0;
  if (st == 0x90 && m[2]) call(F_NOTE_ON, 0, 0, 0, (float)m[1], m[2] / 127.0f);
  else if (st == 0x80 || (st == 0x90 && !m[2])) call(F_NOTE_OFF, 0, 0, 0, (float)m[1]);
  else if (st == 0xB0 && m[1] == 123) call(F_ALL_OFF);
}
