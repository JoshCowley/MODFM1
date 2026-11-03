/* SPDX-License-Identifier: MPL-2.0
 * Modelled on fm1-base's template/app.c (Copyright (C) 2026 Charles Vestal, MPL-2.0), whose helpers it adapts. */
/* FM1 Rack (Lite) firmware: the screen and the controls (part of fm1-base's unity build, after the platform:
 * gfx.c and lib/touch.h are in scope). The sound is rack_core.c's.
 *
 * This is the first stage of the port: the factory presets played from the keys and USB/TRS MIDI, with the four
 * macros on K1-K4. Editing modules and parameters, the sequencer, the arpeggiator, chords, tunings and user
 * presets are still to come from the page (web/src).
 *
 *   PRESET        the next / previous preset (loads when you stop turning)
 *   ALGORITHM     the next / previous category
 *   SELECT        voices, 1-6 (poly presets)
 *   K1-K4         macros 1-4
 *   OCT- / OCT+   octave          HOME   all notes off          SAVE   remember the preset and octave */
#include "fastmath.h"
#define RC_IN_UNITY
#include "rack_core.c"

#define SET_MAGIC 0x524B4C31u                 /* "RKL1": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    uint8_t preset;
    int8_t octave;
    uint8_t voices;                           /* 0: the preset's own */
    uint8_t rsv;
} settings_t;
static settings_t set;

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
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24;
        if (st == 0xF8u || st == 0xFEu)
            continue;
        midi_count++;
        if ((st & 0xF0u) == 0x90u && d2)
            rc_do(RCE_ON, 0, (float)d1, (float)d2 / 127.0f);
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
            rc_do(RCE_OFF, 0, (float)d1, 0.0f);
        else if ((st & 0xF0u) == 0xE0u)       /* pitch bend: +-2 semitones */
            rc_do(RCE_BEND, 0, ((float)((int32_t)(d2 << 7 | d1) - 8192) / 8192.0f) * 2.0f, 0.0f);
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
    rc_resume();
}

void app_init(void)
{
    if (plat_cpu2_set(1) == 0)                /* the second core (now: the one time it may start) renders half the voices */
        rk_set_worker(plat_cpu2_run, plat_cpu2_wait);
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.preset >= NPRESETS || set.octave < -3 || set.octave > 3 || set.voices > RC_MAXV) {
        set.magic = SET_MAGIC;
        set.preset = 0;
        set.octave = 0;
        set.voices = 0;
    }
    load_preset(set.preset);
}

/* ------------------------------------------------------------------ UI --- */
static struct {
    int shown;                                /* the preset on the screen (turning PRESET: not loaded yet) */
    uint32_t turned_at;                       /* when PRESET or ALGORITHM last turned */
    uint32_t keys, btn;
    char msg[2][24];
    uint32_t msg_until;
    int redraw;
    uint8_t held[NKEYS];                      /* the note each held key plays (the octave may change meanwhile) */
} ui;

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

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), press = btn & ~ui.btn, k;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {             /* key 0 is F3 (MIDI 53) */
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            int note = 53 + (int)k + 12 * set.octave;
            uint32_t v = touch_vel(k);
            ui.held[k] = (uint8_t)note;
            rc_note_on(note, (float)v / 127.0f);
            plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)note << 16 | v << 24, TO_USB);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            rc_note_off(ui.held[k]);
            plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)ui.held[k] << 16, TO_USB);
        }
    }
    if (keys != ui.keys)
        ui.redraw = 1;
    ui.keys = keys;
    if ((e = plat_enc(EN_PRESET)) != 0) {
        ui.shown = (ui.shown + (int)e % NPRESETS + NPRESETS) % NPRESETS;
        ui.turned_at = plat_ms();
        ui.redraw = 1;
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        ui.shown = cat_step(ui.shown, e > 0 ? 1 : -1);
        ui.turned_at = plat_ms();
        ui.redraw = 1;
    }
    if ((e = plat_enc(EN_SELECT)) != 0 && rc_patch.mode == RVM_POLY) {
        int v = rc_patch.voices_now + (int)e;
        char t[4];
        v = v < 1 ? 1 : v > RC_MAXV ? RC_MAXV : v;
        set.voices = (uint8_t)v;
        rc_voices(v);
        int_text(v, t);
        app_say("VOICES", t);
    }
    for (k = 0; k < 4u; k++)
        if ((e = plat_enc(EN_K1 + (int)k)) != 0) {
            float m = rc_patch.macro[k] + (float)e * 0.02f;
            m = m < 0.0f ? 0.0f : m > 1.0f ? 1.0f : m;
            rc_macro((int)k, m);
            ui.redraw = 1;
        }
    if (press >> B_OCTDN & 1u)
        octave_step(-1);
    if (press >> B_OCTUP & 1u)
        octave_step(1);
    if (press >> B_HOME & 1u) {
        rc_all_off();
        app_say("ALL NOTES", "OFF");
    }
    if (press >> B_SAVE & 1u) {
        set.preset = (uint8_t)rc_patch.index;
        app_say(plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0 ? "SAVED" : "SAVE FAILED", "");
    }
    ui.btn = btn;
    plat_glow(1);
    plat_leds(btn, keys);
}

void app_input_only(void) { input(); }

/* ------------------------------------------------------------------ the screen --- */
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

static void draw(void)
{
    char t[32], n[12];
    uint32_t now = plat_ms();
    int32_t x;
    int i, pending = ui.shown != rc_patch.index;
    const rk_preset_t *p = &PRESETS[ui.shown];
    /* the header: the source's icon, the category in a tab, the preset; its place on the right */
    cv_begin(240, 26, K_BAR);
    icon(module_icon(rc_patch.mod[RS_SRC]), 6, 7, K_SRC);
    x = text_w(&FONT_T, p->cat) + 8;
    rrect(22, 6, x, 15, K_SRC, 3);
    cv_text(26, 7, &FONT_T, p->cat, K_BG);
    if (now < ui.msg_until) {
        int32_t e = cv_text(28 + x, 5, &FONT_B, ui.msg[0], K_FG);
        cv_text(e + 6, 5, &FONT_B, ui.msg[1], K_GOLD);
    } else {
        cv_text(28 + x, 5, &FONT_B, p->name, pending ? K_DIM : K_FG);
        if (pending) {
            cv_text(234 - text_w(&FONT_T, "LOADING"), 8, &FONT_T, "LOADING", K_GOLD);
        } else {
            int_text(ui.shown + 1, t);
            cat(t, "/");
            int_text(NPRESETS, n);
            cat(t, n);
            cv_text(234 - text_w(&FONT_T, t), 8, &FONT_T, t, K_DIM);
        }
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

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, K_BG);
    ui.shown = rc_patch.index;
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_draw;
    uint32_t now = plat_ms();
    input();
    /* PRESET or ALGORITHM stopped turning: load what is shown */
    if (ui.shown != rc_patch.index && now - ui.turned_at > 180u) {
        uint32_t k;
        for (k = 0; k < NKEYS; k++)           /* the keys held let go first: their notes belong to the old preset */
            if (ui.keys >> k & 1u)
                plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)ui.held[k] << 16, TO_USB);
        load_preset(ui.shown);
        say_preset(ui.shown);
        ui.redraw = 1;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (now - last_draw > 100u)               /* the level and CPU, ten times a second */
        ui.redraw = 1;
    if (ui.redraw) {
        ui.redraw = 0;
        last_draw = now;
        draw();
    }
    ann_tick(now);
}
