/* FM1 Rack firmware: the clicks of a preset change and of a knob turn, measured on a computer (update 35).
 *   swclick       per preset change (each preset to the next, a chord held through it): the biggest jump between two
 *                 samples around the change, the old way (silence at once) and the new (rc_fade_pause's fade, then the
 *                 fade in); then AMP LEVEL turned 0.8 -> 0.3 in one step, at once (rc_set) and gliding (rc_set_glide).
 *                 dB against the biggest jump while the chord sustains: well above it is a click.
 *   swclick knobs [preset]   every continuous knob of each preset's modules moved a quarter of its range, at once and
 *                 gliding, against the same moment with nothing turned and the sound settled after (dB; over +6 listed)
 * Built by build_host.sh beside click. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rack_core.c"
#include "ui_defs.h"
uint32_t plat_ms(void) { return 0; }

#define FR 256
#define LEN (44100 * 2)
static int32_t lr[2 * LEN];
static const int CH[3] = {48, 55, 64};

static float jump(int a, int b)
{
    float m = 0;
    int i;
    if (a < 1) a = 1;
    for (i = a; i < b && i < LEN; i++) {
        float l = fabsf((float)(lr[2 * i] - lr[2 * i - 2])), r = fabsf((float)(lr[2 * i + 1] - lr[2 * i - 1]));
        if (l > m) m = l;
        if (r > m) m = r;
    }
    return m / 8388607.0f;
}
static int jump_at(int a, int b)
{
    float m = 0;
    int i, w = a;
    for (i = a; i < b && i < LEN; i++) {
        float l = fabsf((float)(lr[2 * i] - lr[2 * i - 2])), r = fabsf((float)(lr[2 * i + 1] - lr[2 * i - 1]));
        if (l > m) { m = l; w = i; }
        if (r > m) { m = r; w = i; }
    }
    return w;
}
static float db(float x) { return 20.0f * log10f(x + 1e-9f); }

/* a chord on preset a; at `at` the change to preset b (fade: the new way): the cut and the restart, no new notes */
static void change(int a, int b, int fade, int *at)
{
    int i, pos;
    rk_seed(0x21u);
    rc_pause(); rc_load(a); rc_resume();
    rc_audio.fin = RC_FIN;                    /* (the start isn't measured) */
    for (i = 0; i < 3; i++) rc_note_on(CH[i], 0.8f);
    for (pos = 0; pos + FR <= LEN / 2; pos += FR)
        rc_render(lr + 2 * pos, FR, 8388607.0f * 0.7f);
    if (fade) {                               /* rc_fade_pause, as the device's audio interrupt meets it */
        rc_audio.faded = 0;
        rc_audio.fade_req = 1;
        rc_render(lr + 2 * pos, FR, 8388607.0f * 0.7f);
        pos += FR;
    }
    *at = pos;
    rc_pause();
    rc_load(b);
    rc_resume();
    if (!fade) rc_audio.fin = RC_FIN;         /* the old way: no fade in either */
    for (; pos + FR <= LEN; pos += FR)
        rc_render(lr + 2 * pos, FR, 8388607.0f * 0.7f);
}
static void knob(int k, int glide, int *at)
{
    int i, pos;
    rk_seed(0x21u);
    rc_pause(); rc_load(k); rc_resume();
    rc_audio.fin = RC_FIN;
    rc_set(RS_AMP, 7, 0.8f);
    for (i = 0; i < 3; i++) rc_note_on(CH[i], 0.8f);
    for (pos = 0; pos + FR <= LEN; pos += FR) {
        if (pos == LEN / 2 / FR * FR) {
            *at = pos;
            if (glide) rc_set_glide(RS_AMP, 7, 0.3f);
            else rc_set(RS_AMP, 7, 0.3f);
        }
        rc_render(lr + 2 * pos, FR, 8388607.0f * 0.7f);
    }
}

/* every continuous knob of the preset's modules: the value moved 0.25 of its range at once / gliding */
static float knob2(int k, int slot, int pi, float to, int glide, int *at)
{
    int i, pos, len = 44100 * 3 / 4;
    rk_seed(0x21u);
    rc_pause(); rc_load(k); rc_resume();
    rc_audio.fin = RC_FIN;
    for (i = 0; i < 3; i++) rc_note_on(CH[i], 0.8f);
    for (pos = 0; pos + FR <= len; pos += FR) {
        if (pos == len / 2 / FR * FR) {
            *at = pos;
            if (glide) rc_set_glide(slot, pi, to); else rc_set(slot, pi, to);
        }
        rc_render(lr + 2 * pos, FR, 8388607.0f * 0.7f);
    }
    return jump(*at - 8820, *at - 1024);
}
static void all_knobs(int only)
{
    int k, s, pi, at, n = 0, bad_old = 0, bad_new = 0;
    float wo = -200, wn = -200;
    for (k = 1; k < NPRESETS; k++) {
        if (only && only != k) continue;
        rc_pause(); rc_load(k); rc_resume();
        for (s = 0; s < RS_AMP; s++) {
            const ui_mod_t *m = 0;
            int id = rc_patch.mod[s], j;
            if (!id || s == RS_MODA || s == RS_MODB) continue;
            for (j = 0; j < UI_NMODS; j++) if (UI_MODS[j].id == id) m = &UI_MODS[j];
            if (!m) continue;
            for (pi = 0; pi < m->nparams; pi++) {
                const ui_param_t *p = &m->p[pi];
                float v, to, sus, jo, jn, ref, after;
                if (p->t != UT_NUM && p->t != UT_BIP) continue;
                rc_pause(); rc_load(k); rc_resume();
                v = rk_get_param(s, pi);
                to = v + 0.25f * (float)(p->hi - p->lo);
                if (to > (float)p->hi) to = v - 0.25f * (float)(p->hi - p->lo);
                sus = knob2(k, s, pi, v, 0, &at);     /* the control: the same notes, nothing turned */
                if (sus < 1e-5f) continue;
                ref = jump(at, at + 2 * FR);
                if (ref < sus) ref = sus;
                knob2(k, s, pi, to, 0, &at);          /* (and the sound settled after the turn: brighter is no click) */
                after = jump(at + 4410, at + 15000);
                jo = db(jump(at, at + 2 * FR)) - db(ref > after ? ref : after);
                knob2(k, s, pi, to, 1, &at);
                after = jump(at + 4410, at + 15000);
                jn = db(jump(at, at + 2 * FR)) - db(ref > after ? ref : after);
                n++;
                if (jo > 6.0f) bad_old++;
                if (jn > 6.0f) { bad_new++; printf("%2d %-14s %s %-8s at once %+6.1f dB  gliding %+6.1f dB\n", k, PRESETS[k].name, m->name, p->l, jo, jn); }
                if (jo > wo) wo = jo;
                if (jn > wn) wn = jn;
            }
        }
    }
    printf("knobs: %d turns, over +6 dB: at once %d, gliding %d; worst at once %+.1f dB, gliding %+.1f dB\n", n, bad_old, bad_new, wo, wn);
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "knobs")) { all_knobs(argc > 2 ? atoi(argv[2]) : 0); return 0; }
    int k, at, n = 0;
    float worst_old = -200, worst_new = -200, sum_old = 0, sum_new = 0, kw_old = -200, kw_new = -200;
    for (k = 1; k < NPRESETS - 1; k++) {
        float sus, jo, jn;
        if (argc > 1 && atoi(argv[1]) != k) continue;
        change(k, k + 1, 0, &at);
        sus = jump(at - 22050, at - 4410);
        jo = db(jump(at - FR - 64, at + 8 * FR)) - db(sus);
        change(k, k + 1, 1, &at);
        jn = db(jump(at - FR - 64, at + 8 * FR)) - db(sus);
        if (argc > 1) printf("new: the biggest jump %d frames from the change (sustain %.5f, jump %.5f)\n",
                             jump_at(at - FR - 64, at + 8 * FR) - at, sus, jump(at - FR - 64, at + 8 * FR));
        if (sus < 1e-5f) continue;            /* a preset silent with this chord: nothing to compare */
        printf("%2d %-14s -> %-14s change: old %+6.1f dB  new %+6.1f dB\n", k, PRESETS[k].name, PRESETS[k + 1].name, jo, jn);
        if (jo > worst_old) worst_old = jo;
        if (jn > worst_new) worst_new = jn;
        sum_old += jo; sum_new += jn; n++;
    }
    for (k = 1; k < NPRESETS; k++) {
        float sus, jo, jn;
        if (argc > 1 && atoi(argv[1]) != k) continue;
        knob(k, 0, &at);
        sus = jump(at - 22050, at - 4410);
        if (sus < 1e-5f) continue;
        jo = db(jump(at, at + 2 * FR)) - db(sus);
        knob(k, 1, &at);
        jn = db(jump(at, at + 2 * FR)) - db(jump(at - 22050, at - 4410));
        if (argc > 1 || jn > 3.0f) printf("%2d %-14s LEVEL: at once %+6.1f dB  gliding %+6.1f dB\n", k, PRESETS[k].name, jo, jn);
        if (jo > kw_old) kw_old = jo;
        if (jn > kw_new) kw_new = jn;
    }
    printf("preset changes: %d, worst old %+.1f dB new %+.1f dB, mean old %+.1f new %+.1f (against the sustain)\n",
           n, worst_old, worst_new, n ? sum_old / n : 0, n ? sum_new / n : 0);
    printf("LEVEL 0.8 -> 0.3: worst at once %+.1f dB, gliding %+.1f dB\n", kw_old, kw_new);
    return 0;
}
