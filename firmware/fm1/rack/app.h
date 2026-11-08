/* SPDX-License-Identifier: MPL-2.0
 * Built on fm1-base's template/app.h (Copyright (C) 2026 Charles Vestal, MPL-2.0); the changes are MPL-2.0 too. */
/* FM1 Rack (Lite) firmware: an fm1-base app. The engine (engine/rack.cc) plays in the audio interrupt; app.c is
 * the screen and the controls, rack_core.c the presets, notes and audio. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "FM1 RACK"
#define APP_SPLASH_RGB RGB(90, 200, 255)
#define APP_BIG_FONT FONT_B                  /* the splash's name and the panel's screens: the bold font (8 Nov: FONT_L and FONT_M left out, about 30 KB) */
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity from the keys (lib/touch.h) */
#define APP_BLE_AT_BOOT 0
#define APP_OVERLOAD                     /* a block over 90 % of its time: one voice fewer (rack_core.c) */
void app_overload(void);
/* the screen is drawn in bands of 40 rows: a small canvas leaves the pool (336 KB) to the engine's arena (fm1.toml
 * RACK_ARENA_KB) */
#define APP_CANVAS_PX (240u * 40u)

/* the store: the settings (preset, octave, voices), the panel calibration (the platform's), Bluetooth's (unused) */
/* the 64 user presets, 8 to an object (save.c): 8 A/B sector pairs in the data area, 0xC8000-0xD7FFF; the
 * sequencer's 8 banks and its song + MSEQ (seq.c): 9 more, 0xB6000-0xC7FFF */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_USER0, OBJ_BANK0 = OBJ_USER0 + 8, OBJ_SONG = OBJ_BANK0 + 8, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u, 0xC8000u, 0xCA000u, 0xCC000u, 0xCE000u, 0xD0000u, 0xD2000u, \
                       0xD4000u, 0xD6000u, 0xB6000u, 0xB8000u, 0xBA000u, 0xBC000u, 0xBE000u, 0xC0000u, 0xC2000u, \
                       0xC4000u, 0xC6000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
