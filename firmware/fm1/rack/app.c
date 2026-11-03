/* SPDX-License-Identifier: MPL-2.0
 * Modelled on fm1-base's template/app.c (Copyright (C) 2026 Charles Vestal, MPL-2.0), whose helpers it adapts. */
/* FM1 Rack (Lite) firmware: the screen and the controls (part of fm1-base's unity build, after the platform:
 * gfx.c and lib/touch.h are in scope). The sound is rack_core.c's; how keys and MIDI become notes (scales, chords, the
 * arpeggiator, tunings, MIDI in and out) is play.c's; the pages are menus.c's (editing), save.c's (SAVE, the library).
 *
 * On the home page:
 *   PRESET        the next / previous preset (loads when you stop turning)
 *   ALGORITHM     the next / previous category
 *   SELECT        voices, 1-6 (poly presets)
 *   K1-K4         macros 1-4
 *   OCT- / OCT+   octave          HOME   the rack          SAVE   name and store what is playing
 *   EDIT ENV LFO FX ARP SEL GLO   the pages (menus.c) */
#include "fastmath.h"
#define RC_IN_UNITY
#include "rack_core.c"

#define SET_MAGIC 0x524B4C32u                 /* "RKL2": the settings record; "RKL1" (stage 2) keeps its first three */
#define SET_MAGIC1 0x524B4C31u
typedef struct {
    uint32_t magic;
    uint8_t preset;
    int8_t octave;
    uint8_t voices;                           /* 0: the preset's own (GLOBAL > VOICES AUTO) */
    uint8_t rsv;
    float glo[19], sel[8], arp[8];            /* the GLOBAL, SEL and ARP pages (play.c G_*, C_*, A_*) */
} settings_t;
static settings_t set;
#include "ui_defs.h"
#include "play.c"

static const char *module_name(int id)
{
    switch (id) {
    case 1: return "PLAITS";
    case 2: return "BRAIDS";
    case 10: return "RINGS";
    case 12: return "FILTER";
    case 20: return "PEAKS";
    case 21: return "TIDES";
    case 23: return "GRIDS";
    case 31: return "DELAY";
    case 32: return "REVERB";
    case 33: return "CHORUS";
    default: return 0;
    }
}
static const char *const MODE_NAME[] = {"POLY", "MONO", "LEGATO", "UNI 2", "UNI 4"};

/* ------------------------------------------------------------------ sound --- */
static float gain_now;                        /* glides to MASTER: no step at a block's edge */
static volatile uint32_t midi_count;

static void midi_in(void)                     /* audio interrupt: the packets that came in, played at once */
{
    uint32_t pkt;
    int port, want = GI(G_MIDICH);
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24;
        if (st == 0xF8u) {                    /* the clock: CLOCK MIDI steps the arp */
            pl_clock_tick();
            continue;
        }
        if (st == 0xFAu)
            pl_clock_start();
        if (st >= 0xF0u)
            continue;
        if (want && (int)(st & 15u) + 1 != want)   /* GLOBAL > MIDI CH */
            continue;
        midi_count++;
        if ((st & 0xF0u) == 0x90u && d2)
            pl_key_on((int)d1, (int)d1, (float)d2 / 127.0f);
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
            pl_key_off((int)d1);
        else if ((st & 0xF0u) == 0xE0u)       /* pitch bend: +- GLOBAL > BEND semitones */
            rc_do(RCE_BEND, 0, ((float)((int32_t)(d2 << 7 | d1) - 8192) / 8192.0f) * (float)GI(G_BEND), 0.0f);
        else if ((st & 0xF0u) == 0xC0u && GI(G_PROG))
            pl_prog_req = (int)d1 + 1;
        else if ((st & 0xF0u) == 0xB0u && d1 == 64u)
            pl_sustain(d2 >= 64u);
        else if ((st & 0xF0u) == 0xB0u && (d1 == 123u || d1 == 120u))
            rc_do(RCE_ALL_OFF, 0, 0.0f, 0.0f);
        else if ((st & 0xF0u) == 0xB0u && d1 >= 20u && d1 <= 23u)   /* CC 20-23: the macros */
            rc_do(RCE_MACRO, (int)d1 - 20, (float)d2 / 127.0f, 0.0f);
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;
    uint32_t i;
    if (!rc_audio.pause_req)
        midi_in();
    pl_block(n);                              /* the arp's steps and the strums falling due */
    rc_render(lr, n, 8300000.0f);
    for (i = 0; i < n; i++) {                 /* MASTER, gliding over about 5 ms */
        float g;
        gain_now += (gain - gain_now) * 0.004f;
        g = gain_now / 8300000.0f;
        lr[2u * i] = (int32_t)((float)lr[2u * i] * g);
        lr[2u * i + 1u] = (int32_t)((float)lr[2u * i + 1u] * g);
    }
}

void app_overload(void) { rc_overload(); }

void app_panic(void)
{
    rc_pause();
}

/* ------------------------------------------------------------------ presets --- */
/* poly presets start with at most this many voices, unless SAVE remembered another number. The second benchmark
 * (3 Nov 2026, the lighter engine) measured VA VCF at about 6 % plus 15 % per voice on one core (4 voices 66 %,
 * 6 voices 95 %) and about 7 % per voice on two (6 voices 52 %): so the preset's own voices (up to 6) on two cores,
 * 4 on one. The shedding takes off what doesn't fit (PD STRINGS: 104 % with 6 on two cores) */
#define START_VOICES (plat_cpu2_on() ? 6 : 4)
static void load_preset(int k)
{
    rc_pause();
    rc_load(k);
    if (set.voices && rc_patch.mode == RVM_POLY) {
        rk_set_voices(set.voices);
        rc_patch.voices_now = set.voices;
    } else if (rc_patch.mode == RVM_POLY && rc_patch.voices_now > START_VOICES) {
        rk_set_voices(START_VOICES);          /* (SELECT asks for more) */
        rc_patch.voices_now = START_VOICES;
    }
    glo_apply();                              /* what GLOBAL keeps across presets: tempo, EQ, tuning */
    rc_resume();
    rc_push(PLE_RESET, 0, 0.0f, 0.0f);        /* the notes counted sounding went with the old preset */
}


/* ------------------------------------------------------------------ UI --- */
static struct {
    int shown;                                /* the preset on the screen (turning PRESET: not loaded yet) */
    uint32_t turned_at;                       /* when PRESET or ALGORITHM last turned */
    uint32_t keys, btn;
    char msg[2][24];
    uint32_t msg_until;
    int redraw, clear;
    uint32_t zkeys;                           /* the keys held as SEL's zone keys */
} ui;
static int cur_entry;                         /* the preset playing: < NPRESETS a factory one, else user slot - NPRESETS */

static void cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)
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

void app_say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {
        char t[48] = "";
        cat(t, ui.msg[0]);
        cat(t, " ");
        cat(t, ui.msg[1]);
        ann_say(t, 0, ANN_NOW);
    }
}

static void say_preset(int k)
{
    if (ann_on())
        ann_say(PRESETS[k].name, 0, ANN_NOW);
}

static int same(const char *a, const char *b)   /* (no C library: strcmp) */
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static int cat_step(int from, int d)         /* the first preset of the next / previous category */
{
    int k = from;
    const char *c = PRESETS[from].cat;
    if (d > 0) {
        while (k < NPRESETS && same(PRESETS[k].cat, c))
            k++;
        return k >= NPRESETS ? 0 : k;
    }
    while (k > 0 && same(PRESETS[k].cat, c))
        k--;
    c = PRESETS[k].cat;                       /* the previous category: back to its first */
    while (k > 0 && same(PRESETS[k - 1].cat, c))
        k--;
    return k;
}

static void octave_step(int d)
{
    int o = set.octave + d;
    char t[4];
    if (o < -3 || o > 3)
        return;
    set.octave = (int8_t)o;
    t[0] = o < 0 ? '-' : '+';
    t[1] = (char)('0' + (o < 0 ? -o : o));
    t[2] = 0;
    app_say("OCTAVE", t);
}


/* ------------------------------------------------------------------ the screen --- */
/* The page's look (web/src/2-screen.js): its colours, its 12 x 12 module icons, a header bar with the category in a
 * coloured tab, a dark well with a dot grid (here: the output's scope, with a soft halo), the chain as icon tiles, the
 * macros as cells with rounded bars, and a footer with CPU and RAM bars, the voices' lights and the output meter.
 * Drawn in bands (the canvas is 240 x 40 pixels: the pool goes to the engine's arena). */
#include "icons.h"
#define K_BG RGB(0, 0, 0)
#define K_FG RGB(242, 239, 230)
#define K_DIM RGB(125, 122, 114)
#define K_FAINT RGB(43, 42, 39)
#define K_WELL RGB(11, 11, 10)
#define K_GRID RGB(21, 21, 19)
#define K_MID RGB(29, 29, 27)
#define K_BAR RGB(17, 17, 16)
#define K_CELL RGB(18, 18, 17)
#define K_OFF RGB(26, 26, 24)
#define K_WARN RGB(255, 107, 94)
#define K_OK RGB(95, 224, 138)
#define K_GOLD RGB(255, 210, 122)
#define K_SRC RGB(255, 179, 71)
#define K_SHP RGB(79, 209, 197)
#define K_MOD RGB(180, 140, 255)
#define K_FX RGB(106, 169, 255)

/* a over b, t of the way (0-256), in RGB565 */
static uint16_t mix565(uint16_t a, uint16_t b, uint32_t t)
{
    uint32_t r = ((a >> 11) * t + (b >> 11) * (256u - t)) >> 8;
    uint32_t g = (((a >> 5) & 63u) * t + ((b >> 5) & 63u) * (256u - t)) >> 8;
    uint32_t l = ((a & 31u) * t + (b & 31u) * (256u - t)) >> 8;
    return (uint16_t)(r << 11 | g << 5 | l);
}

/* a rectangle with corners rounded by r (0-4) */
static void rrect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, int32_t r)
{
    static const uint8_t INSET[5][4] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}};
    int32_t i;
    if (r > 4)
        r = 4;
    if (r * 2 > h)
        r = h / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (i = 0; i < r; i++) {
        int32_t in = INSET[r][i];
        cv_rect(x + in, y + i, w - 2 * in, 1, c);
        cv_rect(x + in, y + h - 1 - i, w - 2 * in, 1, c);
    }
}

/* the page's bar(): a faint track, the value over it, rounded */
static void vbar(int32_t x, int32_t y, int32_t w, int32_t h, float v, uint16_t c)
{
    int32_t f;
    v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    f = (int32_t)((float)w * v + 0.5f);
    rrect(x, y, w, h, K_FAINT, h / 2);
    if (f < h)
        f = h;
    rrect(x, y, f, h, c, h / 2);
}

static void icon(int ic, int32_t x, int32_t y, uint16_t c)
{
    int32_t r, b;
    for (r = 0; r < 12; r++)
        for (b = 0; b < 12; b++)
            if (ICON[ic][r] >> (11 - b) & 1u)
                cv_pset(x + b, y + r, c);
}

static int module_icon(int id)
{
    switch (id) {
    case 1: return IC_PLAITS;
    case 2: return IC_BRAIDS;
    case 10: return IC_RINGS;
    case 12: return IC_FILTER;
    case 20: return IC_PEAKS;
    case 21: return IC_TIDES;
    case 23: return IC_GRIDS;
    case 31: return IC_DELAY;
    case 32: return IC_REVERB;
    case 33: return IC_CHORUS;
    default: return IC_EMPTY;
    }
}

/* the scope: the output, a rising zero crossing near the start so a steady note stands still, every second frame
 * (about 10 ms across), drawn twice as the page's glow(): a soft halo, then the line */
static void scope(int32_t w, int32_t h, uint16_t c)
{
    static int16_t y[228];
    uint32_t end = rc_scope_w, start = end - RC_SCOPE_N + 8u, k, i;
    int32_t mid = h / 2, x, prev;
    uint16_t halo = mix565(c, K_WELL, 46);
    for (k = 0; k < 400u; k++) {              /* the first rise through zero in the oldest part */
        int16_t a = rc_scope[(start + k) & (RC_SCOPE_N - 1u)], b = rc_scope[(start + k + 1u) & (RC_SCOPE_N - 1u)];
        if (a < 0 && b >= 0)
            break;
    }
    if (k == 400u)
        k = 0;
    for (x = 0; x < w && x < 228; x++) {
        int32_t v = rc_scope[(start + k + 2u * (uint32_t)x) & (RC_SCOPE_N - 1u)];
        int32_t py = mid - (v * (mid - 3)) / 16384;
        y[x] = (int16_t)(py < 1 ? 1 : py > h - 2 ? h - 2 : py);
    }
    for (i = 0; i < 2; i++)
        for (x = 1, prev = y[0]; x < w && x < 228; prev = y[x], x++) {
            if (i == 0) {
                cv_line(x - 1, prev - 1, x, y[x] - 1, halo);
                cv_line(x - 1, prev + 1, x, y[x] + 1, halo);
            } else {
                cv_line(x - 1, prev, x, y[x], c);
            }
        }
}

static void tile(int32_t x, int slot, uint16_t kind)
{
    int id = rc_patch.mod[slot];
    const char *n = module_name(id);
    int lost = rk_fm1_status(4) >> slot & 1;   /* asked for but unloaded: no room in the arena */
    uint16_t c = lost ? K_WARN : n ? kind : K_FAINT;
    rrect(x, 0, 36, 38, n ? K_CELL : K_BG, 4);
    icon(lost ? module_icon(id) : n ? module_icon(id) : IC_EMPTY, x + 12, 5, c);
    if (n)
        cv_text(x + 18 - text_w(&FONT_T, n) / 2, 21, &FONT_T, n, lost ? K_WARN : K_DIM);
}

#include "menus.c"
#include "save.c"

static void draw_footer(void)
{
    char t[32], n[12];
    int i;
    /* the footer: CPU and RAM bars, the voices' lights (lit by their envelopes), cores and octave, the meter */
    cv_begin(240, 26, K_BAR);
    {
        uint32_t cpu = plat_cpu_pct();
        float ram = (float)rk_fm1_status(0) / (float)rk_fm1_status(1);
        float pk = rc_audio.peak;
        int32_t h;
        cv_text(6, 2, &FONT_T, "CPU", cpu > 85u ? K_WARN : K_DIM);
        vbar(6, 16, 44, 3, (float)cpu / 100.0f, cpu > 85u ? K_WARN : cpu > 70u ? K_GOLD : K_OK);
        cv_text(56, 2, &FONT_T, "RAM", K_DIM);
        vbar(56, 16, 44, 3, ram, ram > 0.95f ? K_GOLD : K_OK);
        for (i = 0; i < RC_MAXV; i++) {
            int on = i < rc_patch.voices_now && i < rk_fm1_status(2);
            float e = on ? rk_voice_env(i) : 0.0f;
            uint32_t a = (uint32_t)(38.0f + 218.0f * (e > 1.0f ? 1.0f : e));
            rrect(108 + i * 8, 9, 6, 8, on ? mix565(K_OK, K_OFF, a) : K_OFF, 1);
        }
        t[0] = 0;
        if (rc_heavy)
            cat(t, "HEAVY");
        else if (rc_shed_count) {
            cat(t, "SHED ");
            int_text((int32_t)rc_shed_count, n);
            cat(t, n);
        } else {
            cat(t, plat_cpu2_on() ? "2C " : plat_cpu2_gave_up() ? "1C* " : "1C ");
            cat(t, MODE_NAME[rc_patch.mode < 5 ? rc_patch.mode : 0]);
        }
        cv_text(160, 2, &FONT_T, t, rc_heavy ? K_WARN : rc_shed_count ? K_GOLD : K_DIM);
        t[0] = 'O';
        t[1] = 'C';
        t[2] = 'T';
        t[3] = set.octave < 0 ? '-' : '+';
        t[4] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave));
        t[5] = 0;
        cv_text(160, 13, &FONT_T, t, ui.keys ? K_OK : K_DIM);
        rc_audio.peak = 0.0f;
        pk = pk > 1.0f ? 1.0f : pk * 1.3f > 1.0f ? 1.0f : pk * 1.3f;
        h = (int32_t)(18.0f * pk + 0.5f);
        cv_rect(228, 4, 6, 18, K_FAINT);
        cv_rect(228, 22 - h, 6, h, pk > 0.9f ? K_WARN : K_OK);
    }
    cv_blit(0, 214);
}

static void draw_home(void)
{
    char t[32], n[12];
    uint32_t now = plat_ms();
    int32_t x;
    int i, pending = ui.shown != cur_entry, c = entry_cat(ui.shown);
    char name[24];
    const char *tag = ui.shown < NPRESETS ? PRESETS[ui.shown].cat : CAT_NAME[c];
    (void)now;
    entry_name(ui.shown, name);
    /* the header: the source's icon, the category in a tab (its colour), the preset; its place on the right */
    cv_begin(240, 26, K_BAR);
    icon(module_icon(rc_patch.mod[RS_SRC]), 6, 7, K_SRC);
    x = text_w(&FONT_T, tag) + 8;
    rrect(22, 6, x, 15, c ? CAT_COL[c] : K_SRC, 3);
    cv_text(26, 7, &FONT_T, tag, K_BG);
    cv_text(28 + x, 5, &FONT_B, name, pending ? K_DIM : K_FG);
    if (pending) {
        cv_text(234 - text_w(&FONT_T, "LOADING"), 8, &FONT_T, "LOADING", K_GOLD);
    } else if (ui.shown < NPRESETS) {
        int_text(ui.shown + 1, t);
        cat(t, "/");
        int_text(NPRESETS, n);
        cat(t, n);
        cv_text(234 - text_w(&FONT_T, t), 8, &FONT_T, t, K_DIM);
    } else {
        cv_text(234 - text_w(&FONT_T, "USER"), 8, &FONT_T, "USER", K_GOLD);
    }
    cv_blit(0, 0);
    /* the well: the output's scope over the page's dot grid */
    cv_begin(228, 42, K_BG);
    rrect(0, 0, 228, 42, K_WELL, 4);
    for (x = 12; x < 228; x += 12)
        for (i = 6; i < 42; i += 6)
            cv_pset(x, i, K_GRID);
    cv_rect(4, 21, 220, 1, K_MID);
    if (rc_heavy)
        cv_text(114 - text_w(&FONT_XS, "TOO HEAVY FOR FM-1: MUTED") / 2, 14, &FONT_XS, "TOO HEAVY FOR FM-1: MUTED", K_WARN);
    else
        scope(228, 42, K_FG);
    home_chips();
    cv_blit(6, 29);
    /* the chain as tiles: source, shaper, modulators, effects (red: unloaded, no room) */
    cv_begin(240, 38, K_BG);
    tile(6, RS_SRC, K_SRC);
    tile(44, RS_SHP, K_SHP);
    tile(82, RS_MODA, K_MOD);
    tile(120, RS_MODB, K_MOD);
    tile(158, RS_FX1, K_FX);
    tile(196, RS_FX2, K_FX);
    cv_blit(0, 74);
    /* the macros on K1-K4: cells as the page's, a rounded bar */
    for (i = 0; i < 4; i++) {
        float v = rc_patch.macro[i];
        cv_begin(112, 46, K_BG);
        rrect(0, 0, 112, 46, K_CELL, 4);
        t[0] = 'K';
        t[1] = (char)('1' + i);
        t[2] = 0;
        cv_text(6, 3, &FONT_T, t, K_DIM);
        cv_text(22, 3, &FONT_T, "MACRO", K_DIM);
        int_text((int32_t)(v * 100.0f + 0.5f), n);
        cv_text(6, 16, &FONT_B, n, K_FG);
        vbar(6, 36, 100, 4, v, K_GOLD);
        cv_blit(i & 1 ? 122u : 6u, i < 2 ? 116u : 166u);
    }
    draw_footer();
}


/* the lit buttons: the page's button (as the page's syncLeds) */
static uint32_t page_leds(void)
{
    if (sv.open)
        return 1u << B_SAVE;
    switch (mn.page) {
    case PG_HOME: case PG_RACK: return 1u << B_HOME;
    case PG_SRC: return 1u << B_EDIT;
    case PG_SHP: case PG_AMP: return 1u << B_ENV;
    case PG_MODA: case PG_MODB: case PG_LFO: return 1u << B_LFO;
    case PG_FX1: case PG_FX2: return 1u << B_FX;
    case PG_MATRIX: case PG_ARP: return 1u << B_ARP;
    case PG_SEL: return 1u << B_SEL;
    case PG_GLO: return 1u << B_GLO;
    }
    return 0;
}

/* the page's drawToast(): a light box with the message, over the lower part of the page */
static void draw_toast(void)
{
    int32_t w, x;
    if (plat_ms() >= ui.msg_until)
        return;
    w = text_w(&FONT_XS, ui.msg[0]) + (ui.msg[1][0] ? text_w(&FONT_XS, ui.msg[1]) + 6 : 0) + 34;
    w = w > 228 ? 228 : w;
    x = 120 - w / 2;
    cv_begin((uint32_t)w, 24, K_BG);
    rrect(0, 0, w, 24, K_FG, 5);
    icon(IC_HOME, 8, 6, RGB(17, 17, 17));
    {
        int32_t e = cv_text(26, 4, &FONT_XS, ui.msg[0], RGB(17, 17, 17));
        cv_text(e + 6, 4, &FONT_XS, ui.msg[1], RGB(120, 90, 30));
    }
    cv_blit((uint32_t)x, 186);
}

/* the well alone, ~30 times a second between the full redraws: the visualisers move smoothly */
static void draw_well_only(void)
{
    if (sv.open || mn.pick || mn.page == PG_RACK || mn.page == PG_MATRIX)
        return;
    if (mn.page == PG_HOME) {
        int32_t x, i;
        cv_begin(228, 42, K_BG);
        rrect(0, 0, 228, 42, K_WELL, 4);
        for (x = 12; x < 228; x += 12)
            for (i = 6; i < 42; i += 6)
                cv_pset(x, i, K_GRID);
        if (rc_heavy)
            cv_text(114 - text_w(&FONT_XS, "TOO HEAVY FOR FM-1: MUTED") / 2, 14, &FONT_XS, "TOO HEAVY FOR FM-1: MUTED", K_WARN);
        else
            vis_draw(-1, 0, 0, K_FG);
        home_chips();
        cv_blit(6, 29);
    } else {
        well(mn.page, page_mod(mn.page), is_fixed(mn.page) ? fixed_colour(mn.page) : slot_colour(page_slot(mn.page)));
    }
}

static void draw(void)
{
    static int shown = -1;                    /* the page on the screen: a new one starts from a clear screen */
    int now = mn.page | mn.pick << 5 | sv.open << 6 | sv.lib << 7;
    if (ui.clear) {                           /* a toast gone: what it covered is drawn again from clear */
        ui.clear = 0;
        shown = -1;
    }
    if (now != shown) {
        lcd_fill(0, 0, 240, 240, K_BG);
        shown = now;
    }
    if (sv.open) {
        draw_save();
        draw_footer();
    } else if (mn.page == PG_HOME)
        draw_home();
    else {
        draw_menu();
        draw_footer();
    }
    draw_toast();
}

/* the lights: the page's button lit, the others glowing (GLOBAL > LED BRIGHT: off, low, high), SEL and ARP half lit
 * while chords, QNT or the arp are on; on the SEL pages the keys in the scale glow, the zone keys half lit */
static void leds(uint32_t lit, uint32_t keys)
{
    float lb = set.glo[G_LED];
    int glow = lb < 0.2f ? 0 : lb < 0.6f ? 1 : 2;
    uint32_t dim = glow ? ((1u << NB) - 1u) & ~lit : 0u, mid = 0u, dimk = 0u, midk = 0u, play = 1u << B_PLAY, k;
    if (CI(C_TYPE) != CT_OFF || CI(C_QNT))
        mid |= 1u << B_SEL;
    if (AI(A_MODE) != AM_OFF)
        mid |= 1u << B_ARP;
    if (mn.page == PG_SEL)
        for (k = 0; k < NKEYS; k++) {
            if (k < 7u && zone_on())
                midk |= 1u << k;
            else if (in_scale(53 + (int)k))
                dimk |= 1u << k;
        }
    lit = (lit & ~play) | (lit & play ? LED_PLAY_GREEN : 0u);
    dim = (dim & ~play) | (dim & play ? LED_PLAY_GREEN : 0u);
    plat_lights(lit, keys, dim, dimk, glow ? glow : dimk ? 1 : 0);
    plat_lights_mid(mid & ~lit, midk & ~keys);
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), press = btn & ~ui.btn, k;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {             /* key 0 is F3 (MIDI 53); play.c makes the notes */
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            int note = 53 + (int)k + 12 * set.octave;
            uint32_t v = touch_vel(k);
            if (k < 7u && zone_on()) {        /* SEL > ZONE: the lowest keys change the chord */
                ui.zkeys |= bit;
                rc_push(PLE_ZONE, (int)k, 1.0f, 0.0f);
            } else {
                rc_push(PLE_KEY_ON, PL_RAW_KEY + (int)k, (float)note, (float)v / 127.0f);
            }
        } else if (!(keys & bit) && (ui.keys & bit)) {
            if (ui.zkeys & bit) {
                ui.zkeys &= ~bit;
                rc_push(PLE_ZONE, (int)k, 0.0f, 0.0f);
            } else {
                rc_push(PLE_KEY_OFF, PL_RAW_KEY + (int)k, 0.0f, 0.0f);
            }
        }
    }
    if (keys != ui.keys)
        ui.redraw = 1;
    ui.keys = keys;
    if (save_input(press)) {                 /* the SAVE page and the library take every control */
        ui.btn = btn;
        leds(btn | page_leds(), keys);
        return;
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {    /* PRESET: the presets, factory then user, on every page (as the page) */
        int i;
        for (i = 0; i < (e > 0 ? e : -e) && i < 8; i++)
            ui.shown = entry_step(ui.shown, e > 0 ? 1 : -1);
        ui.turned_at = plat_ms();
        ui.redraw = 1;
    }
    if (press >> B_OCTDN & 1u)
        octave_step(-1);
    if (press >> B_OCTUP & 1u)
        octave_step(1);
    if (press >> B_SAVE & 1u)                /* SAVE: the page to name and store what is playing */
        save_open();
    if (menu_input(press)) {                 /* a menu page: its own encoders */
        ui.btn = btn;
        leds(btn | page_leds(), keys);
        return;
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        ui.shown = cat_step(ui.shown < NPRESETS ? ui.shown : 0, e > 0 ? 1 : -1);
        ui.turned_at = plat_ms();
        ui.redraw = 1;
    }
    if ((e = plat_enc(EN_SELECT)) != 0 && rc_patch.mode == RVM_POLY) {
        int v = rc_patch.voices_now + (int)e;
        char t[4];
        v = v < 1 ? 1 : v > RC_MAXV ? RC_MAXV : v;
        set.voices = (uint8_t)v;
        set.glo[G_VOICES] = (float)v;
        settings_touch();
        rc_voices(v);
        int_text(v, t);
        app_say("VOICES", t);
    }
    for (k = 0; k < 4u; k++)
        if ((e = plat_enc(EN_K1 + (int)k)) != 0) {
            float m = rc_patch.macro[k] + (float)e * 0.02f;
            m = m < 0.0f ? 0.0f : m > 1.0f ? 1.0f : m;
            rc_macro((int)k, m);
            if (GI(G_MOUT) == 2)              /* MIDI OUT +CC: the macros as CC 21-24 (the page's) */
                plat_midi_send(0x0Bu | (0xB0u | ((uint32_t)GI(G_OUTCH) & 15u)) << 8 | (21u + k) << 16
                               | (uint32_t)(m * 127.0f + 0.5f) << 24, TO_USB);
            ui.redraw = 1;
        }
    ui.btn = btn;
    leds(btn | page_leds(), keys);
}

void app_input_only(void) { input(); }

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, K_BG);
    ui.shown = cur_entry;
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_draw, last_well;
    uint32_t now = plat_ms();
    input();
    menu_tick();
    /* PRESET or ALGORITHM stopped turning: load what is shown */
    if (ui.shown != cur_entry && now - ui.turned_at > 180u) {
        load_entry(ui.shown);
        if (ui.shown < NPRESETS)
            say_preset(ui.shown);
        ui.redraw = 1;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.clear = 1;
        ui.redraw = 1;
    }
    {                                         /* the notes played (keys, chords, arp, MIDI) for the visualisers */
        static uint32_t ring_r;
        uint32_t w = pv.ring_w;
        if (w - ring_r > 16u)
            ring_r = w - 16u;
        for (; ring_r != w; ring_r++)
            vis_note(pv.ring_n[ring_r & 15u], pv.ring_v[ring_r & 15u]);
    }
    if (pl_prog_req) {                        /* MIDI program change (GLOBAL > PROG CHG): factory, then user */
        int p = pl_prog_req - 1;
        pl_prog_req = 0;
        if (p < NPRESETS || (p < NPRESETS + NUSER && us.used[p - NPRESETS])) {
            ui.shown = p;
            ui.turned_at = now - 1000u;
        }
    }
    if (GI(G_CLOCK) == 1 && pv.bpm_in > 30.0f && now - pv.clock_at < 500u) {   /* MIDI clock: its tempo */
        float b = (float)(int32_t)(pv.bpm_in + 0.5f);
        b = b < 30.0f ? 30.0f : b > 300.0f ? 300.0f : b;
        if (b != set.glo[G_TEMPO]) {
            set.glo[G_TEMPO] = b;
            rk_set_param(RS_GLO, 0, b);
        }
    }
    settings_tick();
    vis_tick();
    if (now - last_draw > 100u)               /* the level and CPU, ten times a second */
        ui.redraw = 1;
    if (ui.redraw) {
        ui.redraw = 0;
        last_draw = now;
        draw();
    } else if (now - last_well > (GI(G_ANIM) == 0 ? 33u : GI(G_ANIM) == 1 ? 50u : 166u)) {   /* the well: 30, 20, 6 a second */
        last_well = now;
        draw_well_only();
    }
    ann_tick(now);
}

void app_init(void)
{
    int n, i;
    if (plat_cpu2_set(1) == 0)                /* the second core (now: the one time it may start) renders half the voices */
        rk_set_worker(plat_cpu2_run, plat_cpu2_wait);
    n = plat_store_load(OBJ_SETTINGS, &set, sizeof set);
    if (n != (int)sizeof set || set.magic != SET_MAGIC) {
        int ok1 = n >= 8 && set.magic == SET_MAGIC1;   /* stage 2's record: its preset, octave and voices stay */
        uint8_t p = set.preset, v = set.voices;
        int8_t o = set.octave;
        settings_defaults();
        set.magic = SET_MAGIC;
        set.preset = ok1 ? p : 0;
        set.octave = ok1 ? o : 0;
        set.voices = ok1 ? v : 0;
    }
    if (set.preset >= NPRESETS + NUSER || set.octave < -3 || set.octave > 3 || set.voices > RC_MAXV) {
        set.preset = 0;
        set.octave = 0;
        set.voices = 0;
    }
    for (i = 0; i < NG; i++)                  /* each setting inside its range (a NaN fails both tests) */
        if (!(set.glo[i] >= (float)UP_GLO[i].lo && set.glo[i] <= (float)UP_GLO[i].hi)) set.glo[i] = UP_GLO[i].def;
    for (i = 0; i < NC; i++)
        if (!(set.sel[i] >= (float)UP_SEL[i].lo && set.sel[i] <= (float)UP_SEL[i].hi)) set.sel[i] = UP_SEL[i].def;
    for (i = 0; i < NA; i++)
        if (!(set.arp[i] >= (float)UP_ARP[i].lo && set.arp[i] <= (float)UP_ARP[i].hi)) set.arp[i] = UP_ARP[i].def;
    set.glo[G_VOICES] = (float)set.voices;
    rc_ext = pl_event;
    users_load();
    if (set.preset >= NPRESETS && !us.used[set.preset - NPRESETS])
        set.preset = 0;
    load_entry(set.preset);
    vz.t0 = vz.at_ms = plat_ms();
}
