/* SPDX-License-Identifier: MPL-2.0
 * Modelled on fm1-base's template/app.c (Copyright (C) 2026 Charles Vestal, MPL-2.0), whose helpers it adapts. */
/* FM1 Rack's benchmark firmware: the app (part of fm1-base's unity build, after the platform: gfx.c is in scope).
 *
 * At power-on it checks the FPU, then runs every case in bench.c, one step per screen frame (setting a case up,
 * then 10 ms of sound at a time: 0.1 s untimed, then the timed runs), and shows the results as it goes. The sound is silence throughout: the engine runs in the
 * main loop, not in the audio interrupt, so a case that needs more than one core's time only takes longer to
 * measure; it can't make the FM-1 stutter or restart.
 *
 *   PRESET / ALGORITHM   scroll the results          PLAY   send them over USB-MIDI (reader.html shows them)
 *   REC                  run everything again        OCT- / OCT+   a page up / down */
#include "fastmath.h"
#include "bench.c"

static uint32_t ticks(void) { return plat_ticks24(); }

/* the run: the case being done and its step (0 set up, 1..NRUN timed runs), or done */
static struct {
    int k, step, done;                       /* step: 0 set up, then WARM_STEPS untimed, then NRUN timed */
    uint32_t fpu, fpu_ticks;
    uint32_t started, took_ms;
    int top;                                 /* the first result line shown */
    int redraw;
    uint32_t btn;
    char msg[24];
    uint32_t msg_until;
} bn;

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    (void)master_q12;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
}

void app_panic(void) { }

void app_init(void)
{
    bench_cases();
}

static void restart(void)
{
    bn.k = 0;
    bn.step = 0;
    bn.done = 0;
    bn.top = 0;
    bn.started = plat_ms();
    bn.fpu = fpu_check(ticks, &bn.fpu_ticks);
    bn.redraw = 1;
}

void app_say(const char *a, const char *b)
{
    uint32_t i = 0;
    while (a && *a && i < 22u)
        bn.msg[i++] = *a++;
    if (b && *b && i < 22u)
        bn.msg[i++] = ' ';
    while (b && *b && i < 22u)
        bn.msg[i++] = *b++;
    bn.msg[i] = 0;
    bn.msg_until = plat_ms() + 1500u;
    bn.redraw = 1;
}

/* ------------------------------------------------------------------ text --- */
static char *bn_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
    return d;
}

static void bn_int(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void bn_tenths(int32_t v, char *b)   /* 123 -> "12.3" */
{
    char t[12];
    if (v < 0) {
        *b++ = '-';
        v = -v;
    }
    bn_int(v / 10, b);
    t[0] = '.';
    t[1] = (char)('0' + v % 10);
    t[2] = 0;
    bn_cat(b, t);
}

static void bn_hex(uint32_t v, char *b)
{
    static const char H[] = "0123456789ABCDEF";
    int i;
    for (i = 0; i < 8; i++)
        b[i] = H[(v >> (28 - 4 * i)) & 15u];
    b[8] = 0;
}

/* a result as one line of text: group, name, CPU %, its own share, voices, flags */
static void bn_line(int k, char *t)
{
    const bcase_t *c = &bcases[k];
    const bres_t *r = &bres[k];
    char n[16];
    t[0] = 0;
    bn_cat(t, GROUP[c->group]);
    bn_cat(t, ",");
    bn_cat(t, c->name);
    bn_cat(t, ",");
    bn_tenths(bench_pct10(k, 1, PLAT_TICKS_PER_US), n);
    bn_cat(t, n);
    bn_cat(t, ",");
    if (c->minus >= 0) {
        bn_tenths(bench_pct10(k, 0, PLAT_TICKS_PER_US), n);
        bn_cat(t, n);
    }
    bn_cat(t, ",");
    bn_int((int32_t)(r->worst / PLAT_TICKS_PER_US), n);   /* the slowest run, microseconds */
    bn_cat(t, n);
    bn_cat(t, ",");
    bn_int(r->voices_on, n);
    bn_cat(t, n);
    bn_cat(t, "/");
    bn_int(r->voices_fit, n);
    bn_cat(t, n);
    bn_cat(t, ",");
    bn_int((int32_t)(r->arena / 1024u), n);
    bn_cat(t, n);
    bn_cat(t, ",");
    bn_int(r->dropped, n);
    bn_cat(t, n);
    bn_cat(t, r->bad ? ",NAN" : r->peak < 1e-4f && c->group != G_CORE ? ",SILENT" : ",OK");
}

/* ------------------------------------------------------------------ USB-MIDI --- */
/* each line as one SysEx frame: F0 7D (non-commercial) 'F' 'M' 'B' text F7; reader.html collects them */
static void bn_send_line(const char *s)
{
    uint8_t f[96];
    uint32_t n = 0;
    f[n++] = 0xF0;
    f[n++] = 0x7D;
    f[n++] = 'F';
    f[n++] = 'M';
    f[n++] = 'B';
    while (*s && n < sizeof f - 1u)
        f[n++] = (uint8_t)(*s++ & 0x7F);
    f[n++] = 0xF7;
    plat_sysex_send(f, n);
}

static void bn_send(void)
{
    char t[96], h[12];
    int k;
    t[0] = 0;
    bn_cat(t, "BEGIN,FM1 RACK BENCH,");
    bn_int((int32_t)bncases, h);
    bn_cat(t, h);
    bn_send_line(t);
    t[0] = 0;
    bn_cat(t, "FPU,");
    bn_hex(bn.fpu, h);
    bn_cat(t, h);
    bn_cat(t, bn.fpu == FPU_EXPECT ? ",EXACT," : ",DIFFERS,");
    bn_int((int32_t)(bn.fpu_ticks / PLAT_TICKS_PER_US), h);
    bn_cat(t, h);
    bn_cat(t, "us");
    bn_send_line(t);
    bn_send_line("COLUMNS,group,case,cpu % (one core),own cpu %,slowest run us,voices playing/fit,arena KB,"
                 "unloaded slots,check");
    for (k = 0; k < bncases; k++) {
        bn_line(k, t);
        bn_send_line(t);
        fm1_wdt_feed();
    }
    bn_send_line("END");
}

/* ------------------------------------------------------------------ the screen --- */
#define ROWS 9                               /* result lines on the screen at once */

static void band_text(int32_t y, const char *s, uint16_t c)
{
    cv_text(6, y, &FONT_XS, s, c);
}

static void draw(void)
{
    char t[48], n[16];
    int i, k;
    uint32_t now = plat_ms();
    /* the header: the name, and the FPU check */
    cv_begin(240, 40, C_BLACK);
    if (now < bn.msg_until)
        cv_text(6, 4, &FONT_B, bn.msg, C_WHITE);
    else
        cv_text(6, 4, &FONT_B, APP_NAME, C_WHITE);
    t[0] = 0;
    bn_cat(t, "FPU ");
    bn_cat(t, bn.fpu == FPU_EXPECT ? "OK: HARDWARE FLOAT, EXACT" : "RESULT DIFFERS ");
    if (bn.fpu != FPU_EXPECT) {
        bn_hex(bn.fpu, n);
        bn_cat(t, n);
    }
    cv_text(6, 24, &FONT_XS, t, bn.fpu == FPU_EXPECT ? C_HI : RGB(255, 80, 60));
    cv_blit(0, 0);
    /* progress, or done */
    cv_begin(240, 40, C_BLACK);
    t[0] = 0;
    if (!bn.done) {
        bn_cat(t, "RUNNING ");
        bn_int(bn.k + 1, n);
        bn_cat(t, n);
        bn_cat(t, " / ");
        bn_int(bncases, n);
        bn_cat(t, n);
        bn_cat(t, ": ");
        bn_cat(t, bcases[bn.k].name);
        band_text(4, t, C_WHITE);
        cv_rect(6, 24, 228, 6, C_LINE);
        cv_rect(6, 24, 228 * bn.k / bncases, 6, C_HI);
    } else {
        bn_cat(t, "DONE IN ");
        bn_int((int32_t)(bn.took_ms / 1000u), n);
        bn_cat(t, n);
        bn_cat(t, " S. PLAY: SEND OVER USB");
        band_text(4, t, C_WHITE);
        band_text(20, "CPU % OF ONE CORE  (OWN SHARE)  VOICES", C_GRAY);
    }
    cv_blit(0, 40);
    /* the results, ROWS lines in bands of 40 rows (3 lines each) */
    for (i = 0; i < ROWS; i += 3) {
        int j;
        cv_begin(240, 40, C_BLACK);
        for (j = 0; j < 3; j++) {
            int32_t x, y = j * 13;
            k = bn.top + i + j;
            if (k >= (bn.done ? bncases : bn.k))
                continue;
            t[0] = 0;
            bn_cat(t, GROUP[bcases[k].group]);
            cv_text(6, y, &FONT_XS, t, C_DIM);
            cv_text(56, y, &FONT_XS, bcases[k].name, C_GRAY);
            bn_tenths(bench_pct10(k, 1, PLAT_TICKS_PER_US), t);
            bn_cat(t, "%");
            x = 176 - text_w(&FONT_XS, t);
            cv_text(x, y, &FONT_XS, t, C_HI);
            t[0] = 0;
            if (bcases[k].minus >= 0) {
                bn_cat(t, "(");
                bn_tenths(bench_pct10(k, 0, PLAT_TICKS_PER_US), n);
                bn_cat(t, n);
                bn_cat(t, ")");
            } else {
                bn_int(bres[k].voices_on, t);
                bn_cat(t, "V");
            }
            if (bres[k].bad)
                bn_cat(t, " NAN");
            else if (bres[k].peak < 1e-4f && bcases[k].group != G_CORE)
                bn_cat(t, " SIL");
            cv_text(182, y, &FONT_XS, t, bres[k].bad ? RGB(255, 80, 60) : C_AMB);
        }
        cv_blit(0, 84 + i / 3 * 40);
    }
    /* the footer */
    cv_begin(240, 36, C_BLACK);
    band_text(4, "PRESET: SCROLL  OCT: PAGE  REC: AGAIN", C_DIM);
    cv_blit(0, 204);
}

static void scroll(int d)
{
    int last = (bn.done ? bncases : bn.k) - ROWS;
    bn.top += d;
    if (bn.top > last)
        bn.top = last;
    if (bn.top < 0)
        bn.top = 0;
    bn.redraw = 1;
}

static void input(void)
{
    uint32_t btn = plat_buttons(), press = btn & ~bn.btn;
    int32_t e = plat_enc(EN_PRESET) + plat_enc(EN_ALGO) + plat_enc(EN_SELECT);
    bn.btn = btn;
    if (e)
        scroll((int)e);
    if (press >> B_OCTDN & 1u)
        scroll(-ROWS);
    if (press >> B_OCTUP & 1u)
        scroll(ROWS);
    if ((press >> B_PLAY & 1u) && bn.done) {
        bn_send();
        app_say("SENT", "OVER USB");
    }
    if ((press >> B_REC & 1u) && bn.done)
        restart();
    plat_leds(bn.done ? 1u << B_PLAY | 1u << B_REC : 0u, 0u);
}

void app_input_only(void) { input(); }

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    restart();
}

void app_frame(void)
{
    static uint32_t last_draw;
    uint32_t now = plat_ms();
    input();
    if (!bn.done) {
        int k = bn.k;
        if (bn.step == 0)
            bench_setup(k);
        else if (bn.step <= WARM_STEPS)
            bench_warm();
        else
            bench_run(k, ticks);
        if (++bn.step > WARM_STEPS + NRUN) {
            bench_done(k);
            bn.step = 0;
            if (++bn.k >= bncases) {
                bn.k = bncases - 1;
                bn.done = 1;
                bn.took_ms = plat_ms() - bn.started;
                bn.top = 0;
                bn.redraw = 1;
            } else if (bn.k > bn.top + ROWS - 1) {
                bn.top = bn.k - ROWS + 1;     /* the list follows the run */
            }
        }
        if (now - last_draw > 250u)
            bn.redraw = 1;
    }
    if (bn.msg_until && now >= bn.msg_until) {
        bn.msg_until = 0;
        bn.redraw = 1;
    }
    if (bn.redraw) {
        bn.redraw = 0;
        last_draw = now;
        draw();
    }
}
