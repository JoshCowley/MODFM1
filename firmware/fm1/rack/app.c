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
static void load_preset(int k)
{
    rc_pause();
    rc_load(k);
    if (set.voices && rc_patch.mode == RVM_POLY) {
        rk_set_voices(set.voices);
        rc_patch.voices_now = set.voices;
    }
    rc_resume();
}

void app_init(void)
{
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
static void bar(int32_t x, int32_t y, int32_t w, float v, uint16_t c)
{
    cv_rect(x, y, w, 4, C_LINE);
    cv_rect(x, y, (int32_t)((float)w * v + 0.5f), 4, c);
}

static int32_t mod_text(int32_t x, int32_t y, const char *label, int slot)
{
    const char *n = module_name(rc_patch.mod[slot]);
    int lost = rk_fm1_status(4) >> slot & 1;  /* asked for but unloaded: no room in the arena */
    x = cv_text(x, y, &FONT_XS, label, C_DIM) + 4;
    return cv_text(x, y, &FONT_XS, n ? n : "-", lost ? RGB(255, 80, 60) : n ? C_HI : C_DIM) + 10;
}

static void draw(void)
{
    char t[32], n[12];
    uint32_t now = plat_ms();
    int32_t x;
    int i, pending = ui.shown != rc_patch.index;
    const rk_preset_t *p = &PRESETS[ui.shown];
    /* the preset: its name, its category and place */
    cv_begin(240, 40, C_BLACK);
    if (now < ui.msg_until) {
        x = cv_text(6, 4, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 4, &FONT_B, ui.msg[1], C_HI);
    } else {
        cv_text(6, 4, &FONT_B, p->name, pending ? C_GRAY : C_WHITE);
    }
    t[0] = 0;
    cat(t, p->cat);
    cat(t, "  ");
    int_text(ui.shown + 1, n);
    cat(t, n);
    cat(t, "/");
    int_text(NPRESETS, n);
    cat(t, n);
    cv_text(6, 24, &FONT_XS, t, C_GRAY);
    if (pending)
        cv_text(234 - text_w(&FONT_XS, "LOADING"), 24, &FONT_XS, "LOADING", C_AMB);
    cv_blit(0, 0);
    /* the chain: source, shaper, modulators, effects (red: unloaded, no room) */
    cv_begin(240, 40, C_BLACK);
    x = mod_text(6, 2, "SRC", RS_SRC);
    mod_text(x, 2, "SHP", RS_SHP);
    x = mod_text(6, 16, "MOD", RS_MODA);
    if (rc_patch.mod[RS_MODB])
        x = mod_text(x - 6, 16, "", RS_MODB);
    mod_text(x, 16, "FX", RS_FX1);
    if (rc_patch.mod[RS_FX2])
        mod_text(x + 60, 16, "", RS_FX2);
    cv_rect(6, 36, 228, 1, C_LINE);
    cv_blit(0, 40);
    /* the macros on K1-K4 */
    cv_begin(240, 40, C_BLACK);
    for (i = 0; i < 4; i++) {
        int32_t bx = 6 + i * 58;
        t[0] = 'M';
        t[1] = (char)('1' + i);
        t[2] = 0;
        cv_text(bx, 4, &FONT_XS, t, C_GRAY);
        int_text((int32_t)(rc_patch.macro[i] * 100.0f + 0.5f), n);
        cv_text(bx + 50 - text_w(&FONT_XS, n), 4, &FONT_XS, n, C_HI);
        bar(bx, 22, 50, rc_patch.macro[i], C_HI);
    }
    cv_blit(0, 84);
    /* voices, mode, octave, CPU */
    cv_begin(240, 40, C_BLACK);
    t[0] = 0;
    cat(t, "VOICES ");
    int_text(rc_patch.voices_now, n);
    cat(t, n);
    cat(t, "/");
    int_text(rk_fm1_status(2), n);
    cat(t, n);
    cat(t, "  ");
    cat(t, MODE_NAME[rc_patch.mode < 5 ? rc_patch.mode : 0]);
    cv_text(6, 4, &FONT_XS, t, C_GRAY);
    t[0] = 0;
    cat(t, "OCT ");
    n[0] = set.octave < 0 ? '-' : '+';
    n[1] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave));
    n[2] = 0;
    cat(t, n);
    cv_text(170, 4, &FONT_XS, t, C_GRAY);
    t[0] = 0;
    cat(t, "CPU ");
    int_text((int32_t)plat_cpu_pct(), n);
    cat(t, n);
    cat(t, "%   XRUNS ");
    int_text((int32_t)plat_xruns(), n);
    cat(t, n);
    if (rc_shed_count) {
        cat(t, "   SHED ");
        int_text((int32_t)rc_shed_count, n);
        cat(t, n);
    }
    cv_text(6, 20, &FONT_XS, t, plat_cpu_pct() > 85u ? RGB(255, 80, 60) : C_GRAY);
    cv_blit(0, 124);
    /* the level, and the keys held */
    cv_begin(240, 40, C_BLACK);
    {
        float pk = rc_audio.peak;
        rc_audio.peak = 0.0f;
        bar(6, 4, 228, pk > 1.0f ? 1.0f : pk, pk >= 1.0f ? RGB(255, 80, 60) : C_HI);
    }
    for (i = 0; i < NKEYS; i++)
        cv_rect(6 + i * 8, 16, 7, 14, ui.keys >> i & 1u ? C_HI : C_LINE);
    cv_blit(0, 164);
    /* the footer */
    cv_begin(240, 36, C_BLACK);
    cv_text(6, 4, &FONT_XS, "PRESET: SOUND  ALGO: TYPE  K1-4: MACROS", C_DIM);
    cv_text(6, 18, &FONT_XS, "SELECT: VOICES  HOME: NOTES OFF  SAVE", C_DIM);
    cv_blit(0, 204);
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BLACK);
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
