// Keeps every engine function and module in a size-check link (desktop/size_pi32v2.sh): nothing here runs.
#include "../engine/rack.h"
extern "C" void fm1_entry(void) {
  init(); set_module(0, 1); set_module(0, 2); set_module(0, 3); set_module(1, 10); set_module(1, 11); set_module(1, 12);
  set_module(2, 20); set_module(2, 21); set_module(2, 22); set_module(2, 23); set_module(4, 30); set_module(4, 31); set_module(5, 32); set_module(5, 33);
  set_param(0, 0, 0); set_meta(0, 0, 0, 1, 1); set_route(0, 0, 0, 0, 0, 0); set_macro(0, 0); set_master(0); set_master_fx(0, 0, 0, 0);
  set_voices(1); set_legato(0); set_vintage(0); set_tune(0, 0); set_bend(0); set_ext(0, 0); set_ext_slew(0, 0); note_on(60, 1); note_off(60); all_off(); reset_clock();
  render(64); out_l(); out_r(); monitor(0); scope(); fm1_status(0);
}
