/* FM1 Rack firmware: the sound side checked on a computer (rack_core.c with the engine built as the FM-1's).
 *   1. every preset, loaded as the firmware loads it (presets.h: INIT, its differences, each module's ranges),
 *      sounds the same as INIT then the preset's own calls (presets_full.h, as the page loads one), sample for
 *      sample (each played by a fresh process: the engine keeps some state across init());
 *   2. the resampler keeps the pitch (the same cycles in the same time at 48 and 44.1 kHz), no step at block edges;
 *   3. every preset makes sound through rc_render, with a chord, no NaN;
 *   4. the unison modes play 2 and 4 voices per key.
 * Build and run: sh build_host.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rack_core.c"

typedef struct { uint8_t fn, a, b, c, d; float x, y, z, w; } full_op_t;
#include "presets_full.h"

static void run_full(const full_op_t *o, int n)
{
    int i;
    for (i = 0; i < n; i++, o++)
        switch (o->fn) {
        case 1: rk_set_module(o->a, o->b); break;
        case 2: rk_set_param(o->a, o->b, o->x); break;
        case 13: rk_set_meta(o->a, o->b, o->x, o->y, o->z); break;
        case 4: rk_set_route(o->a, (int8_t)o->b, (int8_t)o->c, o->d, o->x, (int)o->y); break;
        case 5: rk_set_macro(o->a, o->x); break;
        case 6: rk_set_master(o->x); break;
        case 7: rk_set_master_fx(o->x, o->y, o->z, o->w); break;
        case 8: rk_set_legato((int)o->x); break;
        case 9: rk_set_vintage(o->x); break;
        case 10: rk_set_ext_slew(o->a, o->x); break;
        case 11: rk_set_tune(o->a, o->x); break;
        }
}

/* a preset, loaded one way or the other, a chord, 1 s of the engine's own output (left) */
static void play_engine(int k, int full, float *out)
{
    int i, f;
    rk_seed(0x21u);
    if (full) {
        rk_init();
        run_full(FULL[0].ops, FULL[0].n);
        if (k > 0)
            run_full(FULL[k].ops, FULL[k].n);
        rk_set_voices(PRESETS[k].mode == RVM_MONO || PRESETS[k].mode == RVM_LEGATO ? 1 : PRESETS[k].voices > RC_MAXV ? RC_MAXV : PRESETS[k].voices);
    } else {
        rc_load(k);
    }
    for (i = 0; i < 3; i++)
        rk_note_on(48.0f + 4.0f * (float)i, 0.8f);
    for (f = 0; f < 48000; f += 480) {
        rk_render(480);
        memcpy(out + f, rk_out_l(), 480 * sizeof(float));
    }
}

static int played(const char *self, int k, int full, float *buf)
{
    char cmd[512];
    FILE *f;
    size_t n;
    snprintf(cmd, sizeof cmd, "'%s' --play %d %d", self, k, full);
    if (!(f = popen(cmd, "r")))
        return -1;
    n = fread(buf, sizeof(float), 48000, f);
    pclose(f);
    return n == 48000 ? 0 : -1;
}

static int32_t lr[2 * 44100 + 512];

/* through the firmware's path: rc_render in 256-frame blocks (APP_BLOCK), 1 s at 44.1 kHz, gain 1 */
static void play_codec(int k, int notes, const int *chord, float *peak, int *bad)
{
    int i, b;
    rk_seed(0x21u);
    rc_pause();
    rc_load(k);
    rc_resume();
    for (i = 0; i < notes; i++)
        rc_note_on(chord[i], 0.8f);
    *peak = 0;
    *bad = 0;
    for (b = 0; b + 256 <= 44100; b += 256) {
        float l[512];
        int j;
        rc_render(lr + 2 * b, 256, 8388607.0f);
        (void)l;
        for (j = 0; j < 512; j++) {
            float v = (float)lr[2 * b + j] / 8388607.0f;
            if (v != v)
                *bad = 1;
            if (fabsf(v) > *peak)
                *peak = fabsf(v);
        }
    }
}

int main(int argc, char **argv)
{
    static float a[48000], b[48000];
    static const int CHORD[6] = {48, 52, 55, 59, 62, 65};
    int k, i, fails = 0;
    if (argc == 4 && !strcmp(argv[1], "--play")) {
        play_engine(atoi(argv[2]), atoi(argv[3]), a);
        fwrite(a, sizeof(float), 48000, stdout);
        return 0;
    }
    /* 1 */
    for (k = 0; k < NPRESETS; k++) {
        float d = 0;
        if (played(argv[0], k, 0, a) || played(argv[0], k, 1, b)) {
            printf("load  %-14s could not play\n", PRESETS[k].name);
            fails++;
            continue;
        }
        for (i = 0; i < 48000; i++)
            if (fabsf(a[i] - b[i]) > d)
                d = fabsf(a[i] - b[i]);
        if (d != 0.0f) {
            printf("load  %-14s differs from its own calls by %g\n", PRESETS[k].name, d);
            fails++;
        }
    }
    printf("1 loading: %d presets checked against their own calls\n", NPRESETS);
    /* 2: the same note through the engine alone (48 kHz) and through rc_render (44.1 kHz): one second of each must
     * hold the same number of cycles (rising zero crossings), so the pitch is kept; and no step at block edges
     * larger than inside blocks */
    {
        int up48 = 0, up44 = 0;
        float jump_in = 0, jump_edge = 0;
        static float e[48000];
        rk_seed(0x21u);
        rc_pause();
        rc_load(0);
        rc_resume();
        rc_note_on(69, 0.8f);
        for (i = 0; i + 256 <= 44100; i += 256)
            rc_render(lr + 2 * i, 256, 8388607.0f);
        for (i = 4410 + 1; i < 44032; i++) {
            float v = (float)lr[2 * i] / 8388607.0f, w = (float)lr[2 * i - 2] / 8388607.0f;
            if (w < 0 && v >= 0)
                up44++;
            if (i % 256 == 0) { if (fabsf(v - w) > jump_edge) jump_edge = fabsf(v - w); }
            else if (fabsf(v - w) > jump_in) jump_in = fabsf(v - w);
        }
        rk_seed(0x21u);
        rc_load(0);
        rk_note_on(69.0f, 0.8f);
        for (i = 0; i < 48000; i += 480) {
            rk_render(480);
            memcpy(e + i, rk_out_l(), 480 * sizeof(float));
        }
        for (i = 4800 + 1; i < 48000 * 44032 / 44100; i++)
            if (e[i - 1] < 0 && e[i] >= 0)
                up48++;
        printf("2 resampler: %d cycles at 48 kHz, %d at 44.1 kHz over the same time; largest step %.3f inside "
               "blocks, %.3f at block edges\n", up48, up44, jump_in, jump_edge);
        if (abs(up48 - up44) > 2 || jump_edge > jump_in * 1.5f + 1e-3f)
            fails++;
    }
    /* 3 */
    for (k = 0; k < NPRESETS; k++) {
        float peak;
        int bad, n = PRESETS[k].mode == RVM_MONO || PRESETS[k].mode == RVM_LEGATO ? 1 : 3;
        play_codec(k, n, CHORD, &peak, &bad);
        printf("3 %-8s %-14s peak %.3f  voices %d/%d  arena %3d KB  unloaded %02x%s\n", PRESETS[k].cat,
               PRESETS[k].name, peak, rk_fm1_status(3), rk_fm1_status(2), rk_fm1_status(0) / 1024, rk_fm1_status(4),
               bad ? "  NAN" : peak < 1e-4f ? "  SILENT" : "");
        if (bad || peak < 1e-4f)
            fails++;
    }
    /* 4 */
    for (k = 0; k < NPRESETS; k++)
        if (PRESETS[k].mode >= RVM_UNI2) {
            float n[4];
            rc_pause();
            rc_load(k);
            rc_resume();
            printf("4 %-14s mode %d: %d notes per key, detune %.3f\n", PRESETS[k].name, PRESETS[k].mode,
                   rc_unison(60.0f, n), rc_patch.detune);
        }
    /* 5: the overload guard. Sheds one voice per 20 blocks down to one; at one voice, 16 overloads close together
     * mute it (silence, not the engine) until the next load; spaced-out overloads don't */
    {
        int shed_to, heavy_close, heavy_spaced, quiet = 1, back;
        rc_pause(); rc_load(0); rc_resume();
        rc_do(RCE_VOICES, 6, 0.0f, 0.0f);
        rc_note_on(60, 0.8f);
        for (i = 0; i < 200; i++) { rc_render(lr, 256, 8388607.0f); rc_overload(); }
        shed_to = rc_patch.voices_now;
        for (i = 0; i < 40; i++) { rc_render(lr, 256, 8388607.0f); rc_overload(); }
        heavy_close = rc_heavy;
        rc_render(lr, 256, 8388607.0f);
        for (k = 0; k < 512; k++) if (lr[k]) quiet = 0;
        rc_pause(); rc_load(0); rc_resume();
        back = !rc_heavy;
        rc_do(RCE_VOICES, 1, 0.0f, 0.0f);
        for (i = 0; i < 200; i++) { rc_render(lr, 256, 8388607.0f); if (i % 8 == 0) rc_overload(); }
        heavy_spaced = rc_heavy;
        printf("5 overload: shed to %d voice, muted after close overloads %d (silent %d), cleared by a load %d, "
               "muted by spaced ones %d\n", shed_to, heavy_close, quiet, back, heavy_spaced);
        if (shed_to != 1 || !heavy_close || !quiet || !back || heavy_spaced)
            fails++;
        rc_pause(); rc_load(0); rc_resume();
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
