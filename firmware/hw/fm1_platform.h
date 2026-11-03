// The seam between FM1 Rack and the FM-1 board. NOT YET IMPLEMENTED: nothing in this repo runs on an FM-1.
//
// The plan (docs: firmware/README.md and the developer guide) is to fork Felucca, keep its platform layer
// (hal/) and update loader (loader/), and replace its app with FM1 Rack. Felucca's own function names are
// not copied here on purpose: whoever does the port maps each fm1_* function below onto Felucca's hal/ after
// reading it, and records any difference (sample rate, block size, value ranges) in PORTING.md.
//
// Direction of calls:
//   board -> app   fm1_app_audio(), fm1_app_key(), fm1_app_knob(), fm1_app_button(), fm1_app_midi()
//   app -> board   fm1_hal_*()
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// ---- implemented by the app (fm1_app.cc) -------------------------------------------------------
void fm1_app_init(void);
// Audio interrupt: fill `frames` stereo frames of 16-bit audio. Must finish well inside one buffer period.
void fm1_app_audio(int16_t* interleaved_lr, int frames);
// UI loop, called from the main (non-audio) context as often as possible.
void fm1_app_ui(uint32_t ms_now);
// Input events, from the main context.
void fm1_app_key(int key, int down, float velocity);        // 0-26: F3 to G5 (27 keys)
void fm1_app_knob(int knob, int delta);                     // encoders: 0 MASTER 1 SELECT 2 PRESETS 3 ALGORITHM 4-7 KNOB 1-4
void fm1_app_button(int button, int down);                  // page buttons, OCT-, OCT+, PLAY, REC ...
void fm1_app_midi(const uint8_t* bytes, int n);             // USB-MIDI or TRS in, complete messages

// ---- provided by the platform (to be written over Felucca's hal/) ------------------------------
int      fm1_hal_sample_rate(void);                         // OPEN QUESTION: stock firmware reportedly 44,118 Hz
void     fm1_hal_display_rect(int x, int y, int w, int h, const uint16_t* rgb565);   // OPEN: real panel size
void     fm1_hal_led(int button, uint8_t level);
void     fm1_hal_midi_out(const uint8_t* bytes, int n);     // USB only: TRS MIDI out reportedly needs a hardware change
// Patch storage. Writes must be atomic: write the new bank, verify, then switch to it (as Felucca does).
int      fm1_hal_flash_read(uint32_t offset, void* dst, uint32_t n);
int      fm1_hal_flash_write(uint32_t offset, const void* src, uint32_t n);
uint32_t fm1_hal_flash_size(void);
uint32_t fm1_hal_cycles(void);                              // for CPU measurement; OPEN: is a cycle counter readable?

#ifdef __cplusplus
}
#endif
