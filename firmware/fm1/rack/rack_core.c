/* FM1 Rack firmware: the sound side, shared by the firmware (app.c includes it) and the computer's check
 * (rack_host.c). Plain C over the engine (engine/rack.cc built -DRACK_FM1 -DRACK_LITE, through common/rk.cc).
 *
 *   - presets: loaded from presets.h (INIT, then the preset's differences, then each module's parameter ranges);
 *   - notes: a queue from the main loop to the audio interrupt, which applies it before rendering (the engine is
 *     never changed while it renders); the voice modes MONO, LEGATO, UNI 2 and UNI 4 as the page plays them;
 *   - audio: the engine renders 48 kHz and the codec plays 44.1 kHz, so the output is resampled (linear) to the
 *     codec's rate, then scaled to the platform's 24-bit samples;
 *   - a preset load: the main loop asks the audio interrupt to stand aside (it plays silence), loads, then lets it
 *     go on: loading lays out the arena and must not overlap a render. */
#include <stdint.h>
#ifndef RC_IN_UNITY   /* in fm1-base's unity build, felucca-bsp's chip/libc.c already has memset and memcpy */
#include <string.h>
#endif

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
void rk_set_bend(float semis);
void rk_note_on(float note, float vel);
void rk_note_off(float note);
void rk_all_off(void);
void rk_render(int n);
float *rk_out_l(void);
float *rk_out_r(void);
int rk_fm1_status(int what);
void rk_fm1_fit(void);
float rk_voice_env(int i);
void rk_set_worker(int (*run)(void (*)(uint32_t), uint32_t), int (*wait)(void));
uint32_t rk_worker_blocks(void);

typedef struct { const char *name, *cat; uint16_t at, len; uint8_t voices, mode; } rk_preset_t;
typedef struct { uint8_t param; float lo, hi, span; } rk_meta_t;
#include "presets.h"

enum { RS_SRC, RS_SHP, RS_MODA, RS_MODB, RS_FX1, RS_FX2, RS_AMP, RS_GLO, RS_LFO, RS_N };
enum { RVM_POLY, RVM_MONO, RVM_LEGATO, RVM_UNI2, RVM_UNI4 };
#define RC_MAXV 6

/* ------------------------------------------------------------------ presets --- */
static struct {
    int8_t mod[RS_N];       /* the module in each slot, as the preset loaded it */
    float detune;            /* AMP DETUNE (unison spread) */
    float macro[4];          /* the macros' values (the knobs move them from here) */
    int voices_now;          /* voices the render plays: voices, less any shed on overload */
    int mode, voices;        /* voice mode, voices asked for */
    int index;               /* the preset loaded, -1 none */
} rc_patch = {{0}, 0.0f, {0.5f, 0.5f, 0.5f, 0.5f}, 1, 0, 1, -1};

static float rd_f(const uint8_t *p)
{
    float f;
    memcpy(&f, p, 4);
    return f;
}

/* the packed calls of make_presets.py: an op, its small numbers (signed bytes), its values (floats) */
static void rc_run(const uint8_t *p, uint32_t n)
{
    const uint8_t *end = p + n;
    while (p < end) {
        int op = *p++;
        int8_t a = (int8_t)p[0], b = (int8_t)p[1], c = (int8_t)p[2], d = (int8_t)p[3];
        switch (op) {
        case 1: rk_set_module(a, b); if (a >= 0 && a < RS_N) rc_patch.mod[a] = b; p += 2; break;
        case 2: rk_set_param(a, b, rd_f(p + 2)); if (a == RS_AMP && b == 12) rc_patch.detune = rd_f(p + 2); p += 6; break;
        case 3: rk_set_meta(a, b, rd_f(p + 2), rd_f(p + 6), rd_f(p + 10)); p += 14; break;
        case 4: rk_set_route(a, b, c, d, rd_f(p + 4), (int)rd_f(p + 8)); p += 12; break;
        case 5: rk_set_macro(a, rd_f(p + 1)); if (a >= 0 && a < 4) rc_patch.macro[a] = rd_f(p + 1); p += 5; break;
        case 6: rk_set_master(rd_f(p)); p += 4; break;
        case 7: rk_set_master_fx(rd_f(p), rd_f(p + 4), rd_f(p + 8), rd_f(p + 12)); p += 16; break;
        case 8: rk_set_legato((int)rd_f(p)); p += 4; break;
        case 9: rk_set_vintage(rd_f(p)); p += 4; break;
        case 10: rk_set_ext_slew(a, rd_f(p + 1)); p += 5; break;
        case 11: rk_set_tune(a, rd_f(p + 1)); p += 5; break;
        default: return;    /* (never: make_presets.py writes only these) */
        }
    }
}

static void rc_meta(int slot, int key)
{
    int i, j;
    for (i = 0; i < NMETA_OF; i++)
        if (META_OF[i].key == key)
            for (j = 0; j < META_OF[i].n; j++) {
                const rk_meta_t *m = &META[META_OF[i].at + j];
                rk_set_meta(slot, m->param, m->lo, m->hi, m->span);
            }
}

/* the engine only: call with the audio interrupt standing aside (rc_pause), or before the audio starts */
static void rc_load(int k)
{
    const rk_preset_t *p = &PRESETS[k];
    int s;
    memset(rc_patch.mod, 0, sizeof rc_patch.mod);
    rk_init();
    rc_run(PRESET_OPS + PRESETS[0].at, PRESETS[0].len);
    if (k > 0)
        rc_run(PRESET_OPS + p->at, p->len);
    for (s = 0; s < RS_N; s++)               /* each module's ranges (slots 6-8: the slot's own) */
        rc_meta(s, s >= RS_AMP ? 100 + s : rc_patch.mod[s]);
    rk_fm1_fit();                            /* the big buffers sized to this preset: the room left goes to voices */
    rc_patch.mode = p->mode;
    rc_patch.voices = p->mode == RVM_MONO || p->mode == RVM_LEGATO ? 1 : p->voices < 1 ? 1 : p->voices > RC_MAXV ? RC_MAXV : p->voices;
    rk_set_voices(rc_patch.voices);
    rc_patch.voices_now = rc_patch.voices;
    rc_patch.index = k;
}

/* ------------------------------------------------------------------ notes --- */
enum { RCE_ON, RCE_OFF, RCE_ALL_OFF, RCE_MACRO, RCE_VOICES, RCE_BEND };
typedef struct { uint8_t type, a; float x, y; } rc_ev_t;
#define RC_QN 64u                                /* a power of two */
static rc_ev_t rc_q[RC_QN];
static volatile uint32_t rc_qhead, rc_qtail;  /* head: the main loop's; tail: the audio interrupt's */

static int rc_push(int type, int a, float x, float y)
{
    uint32_t h = rc_qhead;
    if (h - rc_qtail >= RC_QN)
        return 0;                             /* full: dropped (a key held through it plays on) */
    rc_q[h & (RC_QN - 1u)].type = (uint8_t)type;
    rc_q[h & (RC_QN - 1u)].a = (uint8_t)a;
    rc_q[h & (RC_QN - 1u)].x = x;
    rc_q[h & (RC_QN - 1u)].y = y;
    rc_qhead = h + 1u;                        /* (one core, interrupts: the entry is written before the index) */
    return 1;
}

/* unison: the page's expandUni (5c-tools.js): offsets of DETUNE x 0.3 semitones */
static int rc_unison(float note, float *out)
{
    float d = rc_patch.detune * 0.3f;
    if (rc_patch.mode == RVM_UNI2) {
        out[0] = note - d;
        out[1] = note + d;
        return 2;
    }
    if (rc_patch.mode == RVM_UNI4) {
        out[0] = note - d;
        out[1] = note - d / 3.0f;
        out[2] = note + d / 3.0f;
        out[3] = note + d;
        return 4;
    }
    out[0] = note;
    return 1;
}

/* one event, in the audio interrupt (from the queue, or MIDI in, which the interrupt reads itself) */
static void rc_do(int type, int a, float x, float y)
{
    float n[4];
    int i, k;
    switch (type) {
    case RCE_ON: k = rc_unison(x, n); for (i = 0; i < k; i++) rk_note_on(n[i], y); break;
    case RCE_OFF: k = rc_unison(x, n); for (i = 0; i < k; i++) rk_note_off(n[i]); break;
    case RCE_ALL_OFF: rk_all_off(); break;
    case RCE_MACRO: rk_set_macro(a, x); break;
    case RCE_VOICES: rk_set_voices(a); rc_patch.voices_now = a; break;
    case RCE_BEND: rk_set_bend(x); break;
    }
}

/* the audio interrupt, before it renders */
static void rc_apply(void)
{
    uint32_t t = rc_qtail, h = rc_qhead;
    for (; t != h; t++) {
        const rc_ev_t *e = &rc_q[t & (RC_QN - 1u)];
        rc_do(e->type, e->a, e->x, e->y);
    }
    rc_qtail = t;
}

/* the audio interrupt, when a block took over 90 % of its time: one voice fewer (not below one), at most every
 * 20 blocks; a preset load or the voices setting gives them back.
 * Too heavy even with one voice (the benchmark measured Rings' MODAL at 102 % of a core with one voice): after
 * RC_HEAVY_N overloads at one voice, each within RC_HEAVY_GAP blocks of the last, the render plays silence until the
 * next preset load, so the interrupt no longer takes the whole core and the screen and controls keep working. */
#define RC_HEAVY_N 16u
#define RC_HEAVY_GAP 4u
static uint32_t rc_shed_wait;
static uint32_t rc_shed_count;
static uint32_t rc_blocks, rc_heavy_last, rc_heavy_streak;
static volatile int rc_heavy;                 /* 1: muted, too heavy for one core (shown on the screen) */
static void rc_overload(void)
{
    if (rc_patch.voices_now <= 1) {
        rc_heavy_streak = rc_blocks - rc_heavy_last <= RC_HEAVY_GAP ? rc_heavy_streak + 1u : 1u;
        rc_heavy_last = rc_blocks;
        if (rc_heavy_streak >= RC_HEAVY_N)
            rc_heavy = 1;
        return;
    }
    if (rc_shed_wait)
        return;
    rc_do(RCE_VOICES, rc_patch.voices_now - 1, 0.0f, 0.0f);
    rc_shed_wait = 20u;
    rc_shed_count++;
}

static void rc_note_on(int note, float vel) { rc_push(RCE_ON, 0, (float)note, vel); }
static void rc_note_off(int note) { rc_push(RCE_OFF, 0, (float)note, 0.0f); }
static void rc_all_off(void) { rc_push(RCE_ALL_OFF, 0, 0.0f, 0.0f); }
static void rc_macro(int i, float v)
{
    rc_patch.macro[i & 3] = v;
    rc_push(RCE_MACRO, i, v, 0.0f);
}
static void rc_voices(int n) { rc_push(RCE_VOICES, n, 0.0f, 0.0f); }
static void rc_bend(float semis) { rc_push(RCE_BEND, 0, semis, 0.0f); }

/* ------------------------------------------------------------------ audio --- */
#define ENGINE_HZ 48000.0f
#define CODEC_HZ 44100.0f
#define RC_STEP (ENGINE_HZ / CODEC_HZ)        /* engine frames per output frame */

static struct {
    float hist_l, hist_r;                     /* the last engine frame used, position 0 */
    float t;                                  /* where the next output frame falls after it, 0..1 */
    float peak;                               /* the loudest output since the screen last read it */
    volatile int pause_req, paused;           /* paused: the interrupt saw the request (shown, not needed) */
} rc_audio;
static float rc_buf_l[1025], rc_buf_r[1025];  /* position 0: the last frame; then this block's */
/* the screen's scope: the last 1024 output frames (mono, 16-bit), written by the audio interrupt, read by the main
 * loop (a frame torn between the two only draws one wrong pixel) */
#define RC_SCOPE_N 1024u
static int16_t rc_scope[RC_SCOPE_N];
static volatile uint32_t rc_scope_w;

/* n output frames at the codec's rate into lr (stereo, interleaved), gain: full scale (24-bit) times MASTER */
static void rc_render(int32_t *lr, uint32_t n, float gain)
{
    float pos_end, a, b;
    uint32_t i, m, k;
    if (rc_audio.pause_req) {                 /* a preset is loading: stand aside */
        rc_audio.paused = 1;
        memset(lr, 0, 8u * n);
        return;
    }
    rc_audio.paused = 0;
    rc_blocks++;
    if (rc_shed_wait)
        rc_shed_wait--;
    rc_apply();
    if (rc_heavy) {                           /* too heavy for one core: silence until another preset loads */
        memset(lr, 0, 8u * n);
        return;
    }
    pos_end = rc_audio.t + (float)n * RC_STEP;
    m = (uint32_t)pos_end;                    /* engine frames this block: positions 1..m */
    if (m > 1024u)
        m = 1024u;
    rk_render((int)m);
    rc_buf_l[0] = rc_audio.hist_l;
    rc_buf_r[0] = rc_audio.hist_r;
    memcpy(rc_buf_l + 1, rk_out_l(), m * sizeof(float));
    memcpy(rc_buf_r + 1, rk_out_r(), m * sizeof(float));
    for (i = 0; i < n; i++) {
        float p = rc_audio.t + (float)i * RC_STEP, f;
        k = (uint32_t)p;
        if (k >= m)
            k = m - 1u;
        f = p - (float)k;
        a = rc_buf_l[k] + (rc_buf_l[k + 1u] - rc_buf_l[k]) * f;
        b = rc_buf_r[k] + (rc_buf_r[k + 1u] - rc_buf_r[k]) * f;
        if (a > rc_audio.peak) rc_audio.peak = a;
        if (-a > rc_audio.peak) rc_audio.peak = -a;
        a = a > 1.0f ? 1.0f : a < -1.0f ? -1.0f : a;
        b = b > 1.0f ? 1.0f : b < -1.0f ? -1.0f : b;
        lr[2u * i] = (int32_t)(a * gain);
        lr[2u * i + 1u] = (int32_t)(b * gain);
        rc_scope[(rc_scope_w + i) & (RC_SCOPE_N - 1u)] = (int16_t)((a + b) * 16383.0f);
    }
    rc_scope_w += n;
    rc_audio.hist_l = rc_buf_l[m];
    rc_audio.hist_r = rc_buf_r[m];
    rc_audio.t = pos_end - (float)m;
}

/* the main loop: the audio interrupt stands aside from its next block on. It interrupts the main loop and runs to
 * the end, so a render is never in progress while the main loop runs: once this flag is set, no render touches the
 * engine until rc_resume (volatile: the compiler keeps the store before the load's first engine call) */
static void rc_pause(void)
{
    rc_audio.pause_req = 1;
}
static void rc_resume(void)
{
    rc_qtail = rc_qhead;                      /* notes queued for the old preset are dropped */
    rc_audio.hist_l = rc_audio.hist_r = 0.0f;
    rc_audio.t = 0.0f;
    rc_heavy = 0;
    rc_heavy_streak = 0;
    rc_audio.pause_req = 0;
}
