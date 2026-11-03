/* SPDX-License-Identifier: MPL-2.0
 * Built on fm1-base's template/app.h (Copyright (C) 2026 Charles Vestal, MPL-2.0); the changes are MPL-2.0 too. */
/* FM1 Rack's benchmark firmware: an fm1-base app (fm1-base's template/app.h is the model). It times FM1 Rack's
 * engine and modules on the FM-1 and shows the results; it makes no sound. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "FM1 BENCH"
#define APP_SPLASH_RGB RGB(90, 200, 255)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz (the render is silence) */
#define APP_BLE_AT_BOOT 0
/* the screen is drawn in bands of 40 rows: a small canvas leaves the pool (336 KB) to the engine's arena (fm1.toml
 * RACK_ARENA_KB), enough for the delay and the reverb together with a Plaits voice */
#define APP_CANVAS_PX (240u * 40u)

/* the store: the template's objects (the panel calibration is the platform's); the benchmark saves nothing */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
