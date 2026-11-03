/* FM1 Rack benchmark: the cases and how each one is timed. Plain C, shared by the firmware (app.c includes it)
 * and the computer's check (bench_host.cc). The engine is engine/rack.cc built with -DRACK_FM1 -DRACK_LITE.
 *
 * A case: a factory preset (INIT, or INIT plus the preset's differences: cases.h), changes on top of it (a model,
 * a module taken out), a number of notes held. It is set up and played for 0.1 s untimed, then timed NRUN times over
 * 480 frames (10 ms at the engine's 48 kHz). The engine has to render 48,000 frames a second on the FM-1 too (the
 * codec runs at 44.1 kHz, so the port resamples or moves the engine to 44.1 kHz), so
 *     CPU % of one core = microseconds for 480 frames / 100.
 * The lowest of the runs is the figure (interrupts only ever add time); the highest is kept as well. */
#include <stdint.h>

/* the engine, through rk.cc (its C interface under names of the benchmark's own: the platform's unity build has
 * functions called init, render and so on of its own) */
void rk_init(void);
void rk_seed(uint32_t s);
void rk_set_module(int slot, int id);
void rk_set_param(int slot, int param, float v);
void rk_set_meta(int slot, int param, float lo, float hi, float span);
void rk_set_route(int i, int src, int slot, int param, float amt, int via);
void rk_set_macro(int i, float v);
void rk_set_master(float v);
void rk_set_master_fx(float lo, float mid, float hi, float comp);
void rk_set_legato(int on);
void rk_set_vintage(float v);
void rk_set_ext_slew(int i, float s);
void rk_set_tune(int note, float semis);
void rk_set_voices(int n);
void rk_note_on(float note, float vel);
void rk_all_off(void);
void rk_render(int n);
float *rk_out_l(void);
float *rk_out_r(void);
int rk_fm1_status(int what);

typedef struct { uint8_t fn, a, b, c, d; float x, y, z, w; } bop_t;
typedef struct { const char *name; const bop_t *ops; uint16_t nops; uint8_t voices, mode; } bpreset_t;
#include "cases.h"

enum { P_INIT, P_FOLDED_BASS, P_KARPLUS, P_TIDAL_FM, P_GRID_GATE, P_CH_GLASS_PNO, P_BS_SUB_MONO, P_LD_SQUARE_CHIP,
       P_PLAYED };   /* cases.h order (make_cases.py PRESETS): the ones the cases are built on, then the ones played */
enum { S_SRC, S_SHP, S_MODA, S_MODB, S_FX1, S_FX2 };

static void run_ops(const bop_t *o, int n)
{
    int i;
    for (i = 0; i < n; i++, o++)
        switch (o->fn) {
        case 1: rk_set_module(o->a, o->b); break;
        case 2: rk_set_param(o->a, o->b, o->x); break;
        case 3: rk_set_meta(o->a, o->b, o->x, o->y, o->z); break;
        case 4: rk_set_route(o->a, o->b, o->c, o->d, o->x, (int)o->y); break;
        case 5: rk_set_macro(o->a, o->x); break;
        case 6: rk_set_master(o->x); break;
        case 7: rk_set_master_fx(o->x, o->y, o->z, o->w); break;
        case 8: rk_set_legato((int)o->x); break;
        case 9: rk_set_vintage(o->x); break;
        case 10: rk_set_ext_slew(o->a, o->x); break;
        case 11: rk_set_tune(o->a, o->x); break;
        }
}

/* ------------------------------------------------------------------ the cases --- */
enum { G_CORE, G_PLAITS, G_BRAIDS, G_RINGS, G_SHP, G_MOD, G_FX, G_VOICES, G_PRESET, NGROUP };
static const char *const GROUP[NGROUP] = {"CORE", "PLAITS", "BRAIDS", "RINGS", "FILTER", "MOD", "FX", "VOICES",
                                           "PRESETS"};
static const char *const PLAITS_MODEL[24] = {"VA VCF", "PHASE D", "6OP FM 1", "6OP FM 2", "6OP FM 3", "TERRAIN",
    "STR MACH", "CHIPTUNE", "PAIR VA", "WAVESHP", "2OP FM", "FORMANT", "ADDITIVE", "WAVETBL", "CHORDS", "SPEECH",
    "SWARM", "NOISE", "PARTICLE", "STRING", "MODAL", "BASS DR", "SNARE", "HI-HAT"};
static const char *const BRAIDS_MODEL[47] = {"CSAW", "/\\-_", "//-_", "FOLD", "UUUU", "SUB-", "SUB/", "SYN-",
    "SYN/", "//x3", "-_x3", "/\\x3", "SIx3", "RING", "////", "//uu", "TOY*", "ZLPF", "ZPKF", "ZBPF", "ZHPF", "VOSM",
    "VOWL", "VFOF", "HARM", "FM", "FBFM", "WTFM", "PLUK", "BOWD", "BLOW", "FLUT", "BELL", "DRUM", "KICK", "CYMB",
    "SNAR", "WTBL", "WMAP", "WLIN", "WTx4", "NOIS", "TWLN", "CLKN", "CLOU", "PRTC", "QPSK"};
static const char *const RINGS_MODEL[6] = {"MODAL", "SYMPATH", "STRING", "FM VOICE", "SYM QNT", "STR+VERB"};

/* a case: the preset, up to four changes (fn 1 set_module, 2 set_param; 0 none), notes held, and the case it is
 * compared with (the module's own cost is the difference; -1 none) */
typedef struct { uint8_t group, preset, notes; int8_t minus; uint8_t ch[4][3]; float chv[4]; const char *name; } bcase_t;

#define NCASE_MAX 120
static bcase_t bcases[NCASE_MAX];
static int bncases;

static int bc_add(int group, int preset, int notes, const char *name)
{
    bcase_t *c = &bcases[bncases];
    c->group = (uint8_t)group;
    c->preset = (uint8_t)preset;
    c->notes = (uint8_t)notes;
    c->minus = -1;
    c->name = name;
    return bncases++;
}
static void bc_change(int k, int i, int fn, int slot, int param, float v)
{
    bcases[k].ch[i][0] = (uint8_t)fn;
    bcases[k].ch[i][1] = (uint8_t)slot;
    bcases[k].ch[i][2] = (uint8_t)param;
    bcases[k].chv[i] = v;
}

static void bench_cases(void)
{
    int i, k, base;
    bncases = 0;
    /* the engine with no source: envelopes, matrix, LFOs, master, for one voice */
    k = bc_add(G_CORE, P_INIT, 1, "NO SOURCE");
    bc_change(k, 0, 1, S_SRC, 0, 0);
    /* Plaits, each model, one voice (INIT: Plaits alone) */
    for (i = 0; i < 24; i++) {
        k = bc_add(G_PLAITS, P_INIT, 1, PLAITS_MODEL[i]);
        bc_change(k, 0, 2, S_SRC, 0, (float)i);
    }
    /* Braids, each shape, one voice (FOLDED BASS with its filter taken out) */
    for (i = 0; i < 47; i++) {
        k = bc_add(G_BRAIDS, P_FOLDED_BASS, 1, BRAIDS_MODEL[i]);
        bc_change(k, 0, 1, S_SHP, 0, 0);
        bc_change(k, 1, 2, S_SRC, 0, (float)i);
    }
    /* Rings, each model, one voice: KARPLUS (Braids PLUK into Rings) without its delay; minus the same without
     * Rings */
    base = bc_add(G_RINGS, P_KARPLUS, 1, "(BRAIDS ONLY)");
    bc_change(base, 0, 1, S_FX1, 0, 0);
    bc_change(base, 1, 1, S_SHP, 0, 0);
    for (i = 0; i < 6; i++) {
        k = bc_add(G_RINGS, P_KARPLUS, 1, RINGS_MODEL[i]);
        bc_change(k, 0, 1, S_FX1, 0, 0);
        bc_change(k, 1, 2, S_SHP, 0, (float)i);
        bcases[k].minus = (int8_t)base;
    }
    /* the filter: FOLDED BASS (Braids into the filter), minus the same without it */
    base = bc_add(G_SHP, P_FOLDED_BASS, 1, "(BRAIDS ONLY)");
    bc_change(base, 0, 1, S_SHP, 0, 0);
    k = bc_add(G_SHP, P_FOLDED_BASS, 1, "FILTER");
    bcases[k].minus = (int8_t)base;
    /* the modulation modules, once a block whatever the voices: TIDAL FM's Tides and Peaks, GRID GATE's Grids */
    base = bc_add(G_MOD, P_TIDAL_FM, 1, "(NO MOD)");
    bc_change(base, 0, 1, S_MODA, 0, 0);
    bc_change(base, 1, 1, S_MODB, 0, 0);
    bc_change(base, 2, 1, S_FX1, 0, 0);
    bc_change(base, 3, 1, S_FX2, 0, 0);
    k = bc_add(G_MOD, P_TIDAL_FM, 1, "TIDES");
    bc_change(k, 0, 1, S_MODB, 0, 0);
    bc_change(k, 1, 1, S_FX1, 0, 0);
    bc_change(k, 2, 1, S_FX2, 0, 0);
    bcases[k].minus = (int8_t)base;
    k = bc_add(G_MOD, P_TIDAL_FM, 1, "PEAKS");
    bc_change(k, 0, 1, S_MODA, 0, 0);
    bc_change(k, 1, 1, S_FX1, 0, 0);
    bc_change(k, 2, 1, S_FX2, 0, 0);
    bcases[k].minus = (int8_t)base;
    base = bc_add(G_MOD, P_GRID_GATE, 1, "(NO GRIDS)");
    bc_change(base, 0, 1, S_MODA, 0, 0);
    bc_change(base, 1, 1, S_FX1, 0, 0);
    bc_change(base, 2, 1, S_FX2, 0, 0);
    k = bc_add(G_MOD, P_GRID_GATE, 1, "GRIDS");
    bc_change(k, 0, 1, S_FX1, 0, 0);
    bc_change(k, 1, 1, S_FX2, 0, 0);
    bcases[k].minus = (int8_t)base;
    /* the effects, once a block: LD SQUARE CHIP's delay, CH GLASS PNO's reverb, BS SUB MONO's chorus, each minus
     * its preset without it (all three on Plaits) */
    base = bc_add(G_FX, P_LD_SQUARE_CHIP, 1, "(NO DELAY)");
    bc_change(base, 0, 1, S_FX1, 0, 0);
    k = bc_add(G_FX, P_LD_SQUARE_CHIP, 1, "DELAY");
    bcases[k].minus = (int8_t)base;
    base = bc_add(G_FX, P_CH_GLASS_PNO, 1, "(NO REVERB)");
    bc_change(base, 0, 1, S_FX1, 0, 0);
    k = bc_add(G_FX, P_CH_GLASS_PNO, 1, "REVERB");
    bcases[k].minus = (int8_t)base;
    base = bc_add(G_FX, P_BS_SUB_MONO, 1, "(NO CHORUS)");
    bc_change(base, 0, 1, S_FX1, 0, 0);
    k = bc_add(G_FX, P_BS_SUB_MONO, 1, "CHORUS");
    bcases[k].minus = (int8_t)base;
    /* voices: INIT's Plaits (VA VCF) with 1, 2, 4 and 6 notes held */
    bc_add(G_VOICES, P_INIT, 1, "VA VCF x1");
    bc_add(G_VOICES, P_INIT, 2, "VA VCF x2");
    bc_add(G_VOICES, P_INIT, 4, "VA VCF x4");
    bc_add(G_VOICES, P_INIT, 6, "VA VCF x6");
    /* the factory presets the Lite firmware can play, a chord of as many notes as the preset has voices (one for
     * the mono presets) */
    for (i = P_PLAYED; i < NPRESETS; i++) {
        const bpreset_t *p = &PRESETS[i];
        bc_add(G_PRESET, i, p->mode ? 1 : p->voices > 6 ? 6 : p->voices, p->name);
    }
}

/* ------------------------------------------------------------------ running one --- */
#define WARM_FRAMES 4800
#define RUN_FRAMES 480
#define NRUN 5
#define WARM_STEPS (WARM_FRAMES / RUN_FRAMES)   /* the untimed 0.1 s, in steps of RUN_FRAMES (the firmware does one a frame) */

typedef struct {
    uint32_t best, worst;        /* ticks (PLAT_TICKS_PER_US a microsecond) for RUN_FRAMES */
    float peak;                  /* the loudest sample in the timed runs */
    uint8_t voices_fit, voices_on, dropped, bad;   /* fm1_status 2, 3, 4; bad: a NaN or infinity came out */
    uint32_t arena;              /* bytes of the arena used */
} bres_t;
static bres_t bres[NCASE_MAX];

static const float CHORD[6] = {48, 52, 55, 59, 62, 65};   /* C3 E3 G3 B3 D4 F4 */

static void bench_setup(int k)
{
    const bcase_t *c = &bcases[k];
    const bpreset_t *p = &PRESETS[c->preset];
    int i;
    rk_seed(0x21u);
    rk_init();
    run_ops(PRESETS[P_INIT].ops, PRESETS[P_INIT].nops);
    if (c->preset != P_INIT)
        run_ops(p->ops, p->nops);
    for (i = 0; i < 4; i++) {
        if (c->ch[i][0] == 1)
            rk_set_module(c->ch[i][1], (int)c->chv[i]);
        else if (c->ch[i][0] == 2)
            rk_set_param(c->ch[i][1], c->ch[i][2], c->chv[i]);
    }
    rk_set_voices(c->notes);
    for (i = 0; i < c->notes; i++)
        rk_note_on(CHORD[i], 0.8f);
    bres[k].best = 0xFFFFFFFFu;
    bres[k].worst = 0;
    bres[k].peak = 0.0f;
    bres[k].bad = 0;
}

static void bench_warm(void) { rk_render(RUN_FRAMES); }

/* one timed run (ticks: the platform's microsecond clock times PLAT_TICKS_PER_US) */
static void bench_run(int k, uint32_t (*ticks)(void))
{
    uint32_t t0, t;
    const float *l, *r;
    int i;
    t0 = ticks();
    rk_render(RUN_FRAMES);
    t = ticks() - t0;
    if (t < bres[k].best)
        bres[k].best = t;
    if (t > bres[k].worst)
        bres[k].worst = t;
    l = rk_out_l();
    r = rk_out_r();
    for (i = 0; i < RUN_FRAMES; i++) {
        float a = l[i] < 0 ? -l[i] : l[i], b = r[i] < 0 ? -r[i] : r[i];
        if (!(a <= 1e9f) || !(b <= 1e9f))   /* NaN or infinity */
            bres[k].bad = 1;
        else {
            if (a > bres[k].peak)
                bres[k].peak = a;
            if (b > bres[k].peak)
                bres[k].peak = b;
        }
    }
}

static void bench_done(int k)
{
    rk_all_off();
    bres[k].voices_fit = (uint8_t)rk_fm1_status(2);
    bres[k].voices_on = (uint8_t)rk_fm1_status(3);
    bres[k].dropped = (uint8_t)rk_fm1_status(4);
    bres[k].arena = (uint32_t)rk_fm1_status(0);
}

/* CPU % of one core, in tenths: microseconds for 480 frames / 100; minus its comparison case if it has one */
static int32_t bench_pct10(int k, int own, uint32_t ticks_per_us)
{
    int32_t us = (int32_t)(bres[k].best / ticks_per_us);
    if (!own && bcases[k].minus >= 0)
        us -= (int32_t)(bres[bcases[k].minus].best / ticks_per_us);
    return us / 10;
}

/* ------------------------------------------------------------------ the FPU check --- */
/* single-precision arithmetic as the compiler emits it with -mfprev1 (hardware float): a fixed sum whose exact
 * IEEE result is known (bench_host prints it; the same on any IEEE single-precision machine without fused
 * multiply-add) */
#define FPU_EXPECT 0x416F0A3Du   /* 14.94 */
static volatile float fpu_seed = 1.0001f;
static uint32_t fpu_check(uint32_t (*ticks)(void), uint32_t *took)
{
    union { float f; uint32_t u; } v;
    float x = 1.0f, m = fpu_seed;
    uint32_t t0 = ticks();
    int i;
    for (i = 1; i <= 10000; i++)
        x = x * m + 0.5f / (float)i;
    *took = ticks() - t0;
    v.f = x;
    return v.u;
}
