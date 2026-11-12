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

#define SET_MAGIC 0x524B4C36u                 /* "RKL6": the settings record; "RKL1" (stage 2) keeps its first three */
#define SET_MAGIC5 0x524B4C35u                /* "RKL5" (updates 38-39): GLOBAL 19 values (no CLICK) */
#define SET_MAGIC4 0x524B4C34u                /* "RKL4" (updates 36-37): the same; CHD FM-1 turned back on (update 38) */
#define SET_MAGIC3 0x524B4C33u                /* "RKL3" (updates 34-35): ARP 18 values, the layer switches in their old order */
#define SET_MAGIC1 0x524B4C31u
#define SET_MAGIC2 0x524B4C32u                /* "RKL2": ARP's 8 values only (before STYLE): all but the new ones kept */
typedef struct {
    uint32_t magic;
    uint8_t preset;
    int8_t octave;
    uint8_t voices;                           /* 0: the preset's own (GLOBAL > VOICES AUTO) */
    uint8_t rsv;
    float glo[23], sel[8], arp[20];           /* the GLOBAL, SEL and ARP pages (play.c G_*, C_*, A_*); GLOBAL 19-22: CLICK */
} settings_t;
static settings_t set;
/* a moment as a button's "went down at" (0 means "not down"): now, or 1 at the clock's very start. Until update 38 it
 * was plat_ms() | 1, a millisecond in the future half the time: a hold test in the same frame then saw ~49 days held,
 * took the tap for a hold, and the tap did nothing (EDIT, SEL, ARP, SEQ, HOME, FX: "needs several tries") */
static uint32_t ms_stamp(void) { uint32_t t = plat_ms(); return t ? t : 1u; }
static uint32_t in_btn;                       /* the buttons held this frame (the sequencer's SEL + K1 lasso) */
#include "ui_defs.h"
#include "play.c"
#include "seq.c"

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
    case 24: return "FUNC";
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
        if (st == 0xFAu) {
            pl_clock_start();
            if (GI(G_CLOCK) == 1) sq.req = 1;
        }
        if (st == 0xFCu && GI(G_CLOCK) == 1)
            sq.req = 2;
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

/* the buttons' presses, counted by the render (every 5.8 ms; update 38): the main loop reads the buttons once a
 * screen frame, and a quick tap could fall between two frames and be lost. input() takes each press from here, once */
static volatile uint8_t btn_downs[32];
static volatile uint32_t btn_isr_at;          /* when the render last looked (no audio: the frames' own edges) */
static volatile int btn_isr_on;               /* the render has looked at least once */
static uint32_t btn_rprev;
static void btn_count(void)
{
    uint32_t b = plat_buttons(), d = b & ~btn_rprev, i;
    btn_rprev = b;
    for (i = 0; d; i++, d >>= 1)
        if (d & 1u)
            btn_downs[i]++;
    btn_isr_at = plat_ms();
    btn_isr_on = 1;
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;
    uint32_t i;
    btn_count();
    if (!rc_audio.pause_req)
        midi_in();
    pl_block(n);                              /* the arp's steps and the strums falling due */
    sq_block(n);                              /* the sequencer's and MSEQ's (seq.c) */
    rc_render(lr, n, 8300000.0f);
    click_render(lr, n, 8300000.0f);          /* the click track and the count-in (seq.c, update 40) */
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
    rc_fade_pause();
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
    uint32_t pkeys;                           /* the keys sounding (not the layer's steps) */
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
    if (plat_ms() < ui.msg_until)
        ui.clear = 1;                         /* a toast replacing one still up: a narrower one would leave its edges */
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
/* the cells' place (after stage 4: the modulation strip gone, the cells taller) */
#define CELL_H 68
#define CELL_Y(i) ((i) < 2 ? 80 : 150)
#define HELP_Y 224
#define HOLD_MS 600u                          /* a button held this long is a hold: its second function (never a tap) */
#define EDIT_DOUBLE_MS 300u                   /* two LFO taps this close: the MATRIX */
#define LEARN_MS 500u                         /* SEL + EDIT held together this long: LEARN on or off */
#define K_BG RGB(0, 0, 0)
#define K_FG RGB(242, 239, 230)
#define K_DIM RGB(172, 168, 158)
#define K_FAINT RGB(62, 60, 56)
#define K_WELL RGB(5, 5, 5)
#define K_GRID RGB(34, 34, 31)
#define K_MID RGB(48, 48, 45)
#define K_BAR RGB(17, 17, 16)
#define K_CELL RGB(22, 22, 21)
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

/* a slider (after stage 4): a track, the value's fill and a thumb. With a modulation colour mc, a halo in that colour:
 * the range the value has swept lately (lo-hi) glows round the track, the live value fills it, the set value stays
 * as a tick, and the thumb glows; e (0-1, how fast the modulation moves) brightens the glow, so it pulses with it */
static void slider(int32_t x, int32_t y, int32_t w, float f, uint16_t c, uint16_t mc, float lo, float hi, float live, float e, uint16_t bg)
{
    int32_t tx;
    f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    live = live < 0.0f ? 0.0f : live > 1.0f ? 1.0f : live;
    if (mc) {
        uint32_t k = 100u + (uint32_t)((e < 0.0f ? 0.0f : e > 1.0f ? 1.0f : e) * 150.0f);   /* 100-250: bright on black */
        int32_t a = x + (int32_t)((lo < 0.0f ? 0.0f : lo) * (float)w), b = x + (int32_t)((hi > 1.0f ? 1.0f : hi) * (float)w);
        if (b > a) {
            rrect(a - 5, y - 6, b - a + 10, 16, mix565(mc, bg, k / 4u), 4);
            rrect(a - 2, y - 3, b - a + 4, 10, mix565(mc, bg, k / 2u), 3);
        }
        rrect(x, y, w, 4, K_FAINT, 2);
        tx = x + (int32_t)(live * (float)w);
        if (tx > x) rrect(x, y, tx - x, 4, mc, 2);
        cv_rect(x + (int32_t)(f * (float)w), y - 3, 1, 10, mix565(c, bg, 170));
        rrect(tx - 7, y - 8, 15, 20, mix565(mc, bg, k / 3u), 4);
        rrect(tx - 4, y - 6, 9, 16, mix565(mc, bg, (k * 3u) / 4u), 3);
        rrect(tx - 2, y - 4, 5, 12, K_FG, 2);
    } else {
        rrect(x, y, w, 4, K_FAINT, 2);
        tx = x + (int32_t)(f * (float)w);
        if (tx > x) rrect(x, y, tx - x, 4, c, 2);
        rrect(tx - 2, y - 4, 5, 12, K_FG, 2);
    }
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
    case 24: return IC_FUNC;
    case 31: return IC_DELAY;
    case 32: return IC_REVERB;
    case 33: return IC_CHORUS;
    default: return IC_EMPTY;
    }
}

/* the scope: the output, a rising zero crossing near the start so a steady note stands still, every second frame
 * (about 10 ms across), drawn twice as the page's glow(): a soft halo, then the line */
static int scope_keep;                        /* 1: draw the trace taken last time (the next band of the same well) */
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
    for (x = 0; x < w && x < 228 && !scope_keep; x++) {
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


#include "menus.c"
#include "save.c"

#include "frame.c"
#include "seqpages.c"

#define HOME_WY 198u                          /* HOME's bottom strip (the scope until update 38) */
/* HOME's bottom strip (update 39: the scope is gone, as Josh asked): the tabs of what plays the keys besides the
 * preset (scale and chord, arp, STYLE, a tuning, the red FM-1 OFF warning), redrawn only when one of them changes */
#define HOME_SH 16
static uint32_t home_strip_sig = 1u;
static void home_strip(int force)
{
    uint32_t sig = (uint32_t)GI(G_TUNE) * 7919u ^ (uint32_t)AI(A_STYLE) * 131u ^ (uint32_t)AI(A_MODE) * 17u
                   ^ (uint32_t)CI(C_TYPE) * 1031u ^ (uint32_t)CI(C_QNT) * 3u ^ (uint32_t)CI(C_KEY) * 65537u
                   ^ (uint32_t)lay_local(L_CHORD) << 28 ^ (uint32_t)lay_local(L_ARP) << 29 ^ (uint32_t)rc_heavy << 30;
    if (!force && sig == home_strip_sig)
        return;
    home_strip_sig = sig;
    cv_begin(228, HOME_SH, K_BG);
    if (rc_heavy)
        cv_text(114 - text_w(&FONT_XS, "TOO HEAVY FOR FM-1: MUTED") / 2, 0, &FONT_XS, "TOO HEAVY FOR FM-1: MUTED", K_WARN);
    else
        home_chips();
    cv_blit(6, HOME_WY);
}

/* home's chain (12 Nov 2026): six tiles, each its module's page picture moving small, so what each module is doing
 * to the sound shows at a glance (a filter's curve opening, Rings ringing, the LFOs swinging, echoes falling); its
 * icon and name under it. Red: asked for but unloaded (no room). After Teenage Engineering's mixer drawn as the
 * sound's path: the tiles sit on the path SRC > SHP > FX 1 > FX 2, joined by arrows whose dots run at the output's
 * level, and the modulators send their pulses up into it */
#define HT_W 64
#define HT_H 50
static void home_tile(int32_t x, int32_t y, int slot, uint16_t kind)
{
    int id = rc_patch.mod[slot];
    const char *n = module_name(id);
    int lost = rk_fm1_status(4) >> slot & 1, off = (slot == RS_FX1 || slot == RS_FX2) && rk_fx_bypass(slot);
    uint16_t c = lost ? K_WARN : off ? K_FAINT : n ? kind : K_FAINT;
    int32_t w;
    cv_begin(HT_W, HT_H, K_BG);
    if (n)
        rrect(0, 0, HT_W, HT_H, K_WELL, 4);
    else {
        rrect(0, 0, HT_W, HT_H, K_CELL, 4);
        rrect(1, 1, HT_W - 2, HT_H - 2, K_BG, 3);
    }
    if (n && !lost && !off)
        vis_mini(slot, id, 2, 2, HT_W - 4, HT_H - 15, kind);
    else if (n && off)                        /* switched off (hold FX, then tap): no picture, OFF */
        cv_text(HT_W / 2 - text_w(&FONT_T, "OFF") / 2, (HT_H - 15) / 2 - 3, &FONT_T, "OFF", K_DIM);
    if (n) {
        w = 14 + text_w(&FONT_T, n);
        icon(module_icon(id), HT_W / 2 - w / 2, HT_H - 13, c);
        cv_text(HT_W / 2 - w / 2 + 14, HT_H - 13, &FONT_T, n, lost ? K_WARN : off ? K_FAINT : K_DIM);
    } else {
        icon(IC_EMPTY, HT_W / 2 - 6, HT_H / 2 - 6, c);
    }
    cv_blit((uint32_t)x, (uint32_t)y);
}
static float home_level(void)                 /* the output's level lately, 0..1 (the scope's last 256 samples) */
{
    uint32_t end = rc_scope_w, k;
    float s = 0.0f;
    for (k = 0; k < 256u; k++) {
        float v = (float)rc_scope[(end - 1u - k) & (RC_SCOPE_N - 1u)] / 16384.0f;
        s += v * v;
    }
    s = fm_sqrtf(s / 256.0f) * 3.0f;
    return s > 1.0f ? 1.0f : s;
}
/* the path between two tiles: an arrow (across, or down), with two dots running along it, as bright as lev */
static void path_arrow(int32_t x, int32_t y, int32_t w, int32_t h, int down, float lev, float t, uint16_t c)
{
    uint16_t line = mix565(c, K_BG, 70u + (uint32_t)(lev * 60.0f));
    int j;
    cv_begin((uint32_t)w, (uint32_t)h, K_BG);
    if (!down) {
        int32_t yy = h / 2;
        cv_rect(1, yy, w - 4, 1, line);
        cv_line(w - 5, yy - 3, w - 2, yy, line);
        cv_line(w - 5, yy + 3, w - 2, yy, line);
        for (j = 0; j < 2 && lev > 0.02f; j++) {
            float u = t * 0.9f + (float)j * 0.5f;
            u -= fm_floorf(u);
            rrect(1 + (int32_t)(u * (float)(w - 7)), yy - 1, 3, 3, mix565(K_FG, K_BG, 60u + (uint32_t)(lev * 195.0f)), 1);
        }
    } else {
        int32_t xx = w / 2;
        cv_rect(xx, 0, 1, h - 2, line);
        cv_line(xx - 3, h - 4, xx, h - 1, line);
        cv_line(xx + 3, h - 4, xx, h - 1, line);
        for (j = 0; j < 1 && lev > 0.02f; j++) {
            float u = t * 0.9f;
            u -= fm_floorf(u);
            rrect(xx - 1, (int32_t)(u * (float)(h - 3)), 3, 3, mix565(K_FG, K_BG, 60u + (uint32_t)(lev * 195.0f)), 1);
        }
    }
    cv_blit((uint32_t)x, (uint32_t)y);
}
/* a modulator's feed: dotted upward ticks over its tile, pulsing with its output */
static void mod_feed(int32_t x, int32_t y, int slot, float out)
{
    float a = fm_fabsf(out);
    int32_t k;
    uint16_t c = rc_patch.mod[slot] ? mix565(K_MOD, K_BG, 50u + (uint32_t)((a > 1.0f ? 1.0f : a) * 205.0f)) : K_CELL;
    cv_begin(HT_W, 8, K_BG);
    for (k = 0; k < 3; k++) {
        int32_t xx = 16 + k * 16;
        cv_rect(xx, 3, 1, 4, c);
        cv_line(xx - 2, 4, xx, 1, c);
        cv_line(xx + 2, 4, xx, 1, c);
    }
    cv_blit((uint32_t)x, (uint32_t)y);
}
static void home_blank(int32_t x, int32_t y, int32_t w, int32_t h)   /* a place nothing uses now: black */
{
    cv_begin((uint32_t)w, (uint32_t)h, K_BG);
    cv_blit((uint32_t)x, (uint32_t)y);
}
/* only the slots in use are drawn (12 Nov 2026): the path's modules (SRC, SHP, FX 1, FX 2, as loaded) fill the
 * places along it in order, top row then down the right; the modulators loaded fill the bottom row's left */
static void home_tiles(void)
{
    static const int16_t PX[4] = {6, 88, 170, 170}, PY[4] = {34, 34, 34, 92}, MX[2] = {6, 88};
    static const uint8_t PATH[4] = {RS_SRC, RS_SHP, RS_FX1, RS_FX2}, MODS[2] = {RS_MODA, RS_MODB};
    static const uint16_t PC[4] = {K_SRC, K_SHP, K_FX, K_FX};
    const float *mon = rk_monitor(RS_AMP);
    float lev = home_level(), t = vis_t();
    int np = 0, nm = 0, i;
    for (i = 0; i < 4; i++)
        if (rc_patch.mod[PATH[i]]) {
            home_tile(PX[np], PY[np], PATH[i], PC[i]);
            np++;
        }
    for (i = np; i < 4; i++)
        home_blank(PX[i], PY[i], HT_W, HT_H);
    if (np > 1) path_arrow(70, 34, 18, HT_H, 0, lev, t, K_SRC); else home_blank(70, 34, 18, HT_H);
    if (np > 2) path_arrow(152, 34, 18, HT_H, 0, lev, t + 0.3f, K_SHP); else home_blank(152, 34, 18, HT_H);
    if (np > 3) path_arrow(170, 84, HT_W, 8, 1, lev, t + 0.6f, K_FX); else home_blank(170, 84, HT_W, 8);
    for (i = 0; i < 2; i++)
        if (rc_patch.mod[MODS[i]]) {
            home_tile(MX[nm], 92, MODS[i], K_MOD);
            mod_feed(MX[nm], 84, MODS[i], mon[i ? 42 : 40]);
            nm++;
        }
    for (i = nm; i < 2; i++) {
        home_blank(MX[i], 92, HT_W, HT_H);
        home_blank(MX[i], 84, HT_W, 8);
    }
}

static void draw_footer(void)                 /* (the status strip on top; the modulation strip is gone) */
{
    status_strip();
}

/* home's macros as sliders (12 Nov 2026: 48 rows, under the tiles): each with what it moves underneath; LEARN: the
 * macro being linked framed gold */
#define HM_H 48
static uint32_t home_macros_sig = 1u;
static void home_macros(int force)            /* (update 39: drawn again only when a value, LEARN or a route changes) */
{
    char t[24], n[8];
    int i;
    uint32_t now = plat_ms(), sig = (uint32_t)mn.learn * 7u + (uint32_t)mn.learn_m * 31u + (mn.learn ? (now / 250u) & 1u : 0u);
    for (i = 0; i < 4; i++)
        sig = sig * 131u + (uint32_t)(rc_patch.macro[i] * 1000.0f);
    for (i = 0; i < 18; i++) {
        int src, via;
        float amt;
        sig = sig * 31u + route_dst(i, &src, &via, &amt) + (uint32_t)src * 977u;
    }
    if (!force && sig == home_macros_sig)
        return;
    home_macros_sig = sig;
    for (i = 0; i < 4; i++) {                 /* (one canvas each: the canvas holds 9600 pixels) */
        float v = rc_patch.macro[i], amt;
        int r, nt = 0, src, via, sel = mn.learn && !mn.learn_src && mn.learn_m == i;
        uint16_t first = 0;
        for (r = 0; r < 18; r++) {
            uint16_t d = route_dst(r, &src, &via, &amt);
            if (d && src == 1 + i) {
                if (!nt) first = d;
                nt++;
            }
        }
        cv_begin(57, HM_H, K_BG);             /* flat (12 Nov 2026): an arc in its knob's colour, no box */
        if (sel && (now / 250u) & 1u)
            rrect(0, 0, 57, HM_H, RGB(40, 34, 18), 4);
        rrect(1, 2, 11, 11, sel ? K_GOLD : KNOB_C[i], 5);
        t[0] = (char)('1' + i);
        t[1] = 0;
        cv_text(7 - text_w(&FONT_T, t) / 2, 1, &FONT_T, t, K_BG);
        t[0] = 'M';
        t[1] = (char)('1' + i);
        t[2] = 0;
        cv_text(15, 1, &FONT_T, t, sel ? K_GOLD : K_DIM);
        arc(36, 21, 12.0f, 0.0f, 1.0f, K_FAINT, 2);
        if (v > 0.005f)
            arc(36, 21, 12.0f, 0.0f, v > 1.0f ? 1.0f : v, nt ? KNOB_C[i] : mix565(KNOB_C[i], K_BG, 120), 3);
        {
            float an = 2.3561945f + (v > 1.0f ? 1.0f : v < 0.0f ? 0.0f : v) * 4.712389f;
            rrect(36 + (int32_t)(11.0f * fm_cosf(an)) - 2, 21 + (int32_t)(11.0f * fm_sinf(an)) - 2, 5, 5, K_FG, 2);
        }
        int_text((int32_t)(v * 100.0f + 0.5f), n);
        cv_text(36 - text_w(&FONT_T, n) / 2, 15, &FONT_T, n, K_FG);
        if (nt) {
            dst_name(first, t);
            if (nt > 1) {
                cat(t, "+");
                int_text(nt - 1, n);
                cat(t, n);
            }
        } else {
            t[0] = 0;
            cat(t, "UNSET");
        }
        while (text_w(&FONT_T, t) > 54 && t[0] && t[1]) {
            int k = 0;
            while (t[k]) k++;
            t[k - 1] = 0;
        }
        cv_text(28 - text_w(&FONT_T, t) / 2, 35, &FONT_T, t, nt ? K_FG : K_DIM);
        cv_blit(2u + (uint32_t)i * 59u, 146);
    }
}

static void draw_home(void)
{
    char t[32], n[12];
    int32_t x;
    int pending = ui.shown != cur_entry, c = entry_cat(ui.shown);
    char name[24];
    const char *tag = ui.shown < NPRESETS ? PRESETS[ui.shown].cat : CAT_NAME[c];
    entry_name(ui.shown, name);
    status_strip();
    /* the header: the source's icon, the category in a tab (its colour), the preset; its place on the right */
    cv_begin(240, 22, K_BAR);
    icon(module_icon(rc_patch.mod[RS_SRC]), 6, 5, K_SRC);
    x = text_w(&FONT_T, tag) + 8;
    rrect(22, 4, x, 14, c ? CAT_COL[c] : K_SRC, 3);
    cv_text(26, 4, &FONT_T, tag, K_BG);
    cv_text(28 + x, 3, &FONT_B, name, pending ? K_DIM : K_FG);
    if (pending) {
        cv_text(234 - text_w(&FONT_T, "LOADING"), 5, &FONT_T, "LOADING", K_GOLD);
    } else if (ui.shown < NPRESETS) {
        int_text(ui.shown + 1, t);
        cat(t, "/");
        int_text(NPRESETS, n);
        cat(t, n);
        cv_text(234 - text_w(&FONT_T, t), 5, &FONT_T, t, K_DIM);
    } else {
        cv_text(234 - text_w(&FONT_T, "USER"), 5, &FONT_T, "USER", K_GOLD);
    }
    cv_blit(0, 10);
    home_tiles();                             /* the chain, moving; the macros under it; the tabs at the bottom */
    home_macros(1);
    home_strip(1);
}

/* the lit buttons: the page's button (as the page's syncLeds) */
static uint32_t page_leds(void)
{
    if (sv.open)
        return 1u << B_SAVE;
    switch (mn.page) {
    case PG_HOME: case PG_RACK: return 1u << B_HOME;
    case PG_SRC: case PG_OSC: return 1u << B_EDIT;
    case PG_SHP: case PG_AMP: return 1u << B_ENV;
    case PG_MODA: case PG_MODB: case PG_LFO: return 1u << B_LFO;
    case PG_FX1: case PG_FX2: return 1u << B_FX;
    case PG_ARP: return 1u << B_ARP;
    case PG_MATRIX: return 1u << B_LFO;
    case PG_SEL: return 1u << B_SEL;
    case PG_SEQ: return 1u << B_SEQ;
    case PG_MSEQ: return 1u << B_LFO;
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
    cv_blit((uint32_t)x, 212);
}

/* the well alone, ~30 times a second between the full redraws: the visualisers move smoothly */
static void draw_well_only(void)
{
    if (sv.open || mn.pick || sp.layer || mn.page == PG_RACK || mn.page == PG_MATRIX)
        return;
    if (mn.page == PG_HOME) {                 /* (update 39: only the tiles move; macros and tabs when they change) */
        home_tiles();
        home_macros(0);
        home_strip(0);
    } else if (is_seq_page(mn.page)) {
        seq_area();
    } else {
        page_well(mn.page);
        if (!is_fixed(mn.page) && !(mn.page == PG_LFO && mn.sub >= UI_LFO.npages) && !is_mod_sub(mn.page))
            param_cells(mn.page);             /* the modulated sliders' halos move with the well */
    }
    draw_toast();                             /* a toast stays on top (HOME's macros and scope drew over it, update 36) */
}

static void draw(void)
{
    static int shown = -1;                    /* the page on the screen: a new one starts from a clear screen */
    int now = mn.page | mn.pick << 5 | sv.open << 6 | sv.lib << 7 | sp.layer << 8 | picture_first(mn.page) << 9 | is_mod_sub(mn.page) << 10;
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
    } else if (sp.layer) {
        status_strip();
        draw_seq_layer();
    } else if (is_seq_page(mn.page)) {
        draw_seq_page();
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
    if (AI(A_MODE) != AM_OFF || AI(A_STYLE))
        mid |= 1u << B_ARP;
    if (mn.page == PG_SEL)
        for (k = 0; k < NKEYS; k++) {
            if (k < 7u && (zone_on() || zone_bass()))
                midk |= 1u << k;
            else if (in_scale(53 + (int)k))
                dimk |= 1u << k;
        }
    if (mn.learn && (plat_ms() / 250u) & 1u)  /* LEARN: HOME blinks */
        lit |= 1u << B_HOME;
    if (sq.run)                               /* the sequencer: PLAY lit while it plays, REC while it records (blinking ready) */
        lit |= 1u << B_PLAY;
    if ((sq.rec && sq.run) || (sq.armed && (plat_ms() / 300u) & 1u))
        lit |= 1u << B_REC;
    lit = (lit & ~play) | (lit & play ? LED_PLAY_GREEN : 0u);
    dim = (dim & ~play) | (dim & play ? LED_PLAY_GREEN : 0u);
    plat_lights(lit, keys, dim, dimk, glow ? glow : dimk ? 1 : 0);
    plat_lights_mid(mid & ~lit, midk & ~keys);
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), press = btn & ~ui.btn, k;
    int32_t e;
    {   /* each press once (update 40): the frame's own edge, or the render's count of one that fell between two
         * frames. A press the frame saw first is skipped when the count catches up with it (so a press can't arrive a
         * frame late and undo what was done while it was held, e.g. SEL + K4) */
        static uint8_t seen[32], skip[32];
        uint32_t i, edge = press;
        int isr = btn_isr_on && (int32_t)(plat_ms() - btn_isr_at) < 60;
        press = 0;
        for (i = 0; i < 32u; i++) {
            uint8_t c = btn_downs[i];
            int counted = c != seen[i];
            seen[i] = c;
            if (!isr) {
                skip[i] = 0;
                if (edge >> i & 1u) press |= 1u << i;
            } else if (edge >> i & 1u) {
                press |= 1u << i;
                if (!counted) skip[i] = 1;    /* the count will come: not a second press */
            } else if (counted) {
                if (skip[i]) skip[i] = 0;
                else press |= 1u << i;        /* a tap the frames missed */
            }
        }
    }
    in_btn = btn;
    touch_poll();
    keys = touch_keys();
    seq_layer_keys(keys, ui.keys);            /* holding SEQ: the white keys are steps (seqpages.c) */
    for (k = 0; k < NKEYS; k++) {             /* key 0 is F3 (MIDI 53); play.c makes the notes */
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit) && sp.layer) {
            continue;                         /* (a step, not a note) */
        } else if ((keys & bit) && !(ui.keys & bit)) {
            ui.pkeys |= bit;
            int note = 53 + (int)k + 12 * set.octave;
            uint32_t v = touch_vel(k);
            if (k < 7u && (zone_on() || zone_bass())) {        /* SEL > ZONE: the lowest keys change the chord */
                ui.zkeys |= bit;
                rc_push(PLE_ZONE, (int)k, 1.0f, 0.0f);
            } else {
                rc_push(PLE_KEY_ON, PL_RAW_KEY + (int)k, (float)note, (float)v / 127.0f);
            }
        } else if (!(keys & bit) && (ui.keys & bit) && (ui.pkeys & bit)) {
            ui.pkeys &= ~bit;
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
    seq_buttons(press, btn);                  /* SEQ (tap: the pages; hold: the layer), PLAY, REC */
    if (sp.layer) {                           /* the layer: K1-K4 the held step's, OCT its page of 16 */
        seq_layer_encoders();
        if (press >> B_OCTDN & 1u && sp.layer_page > 0) { sp.layer_page--; ui.redraw = 1; }
        if (press >> B_OCTUP & 1u && sp.layer_page < 3) { sp.layer_page++; ui.redraw = 1; }
        ui.btn = btn;
        leds(btn | page_leds() | 1u << B_SEQ, keys);
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
    if (is_seq_page(mn.page) || sp.save_at)  /* SAVE on a sequencer page: the sequencer (held on NOTES: copy / paste) */
        seq_save_btn(press, btn);
    else if (press >> B_SAVE & 1u)           /* SAVE: the page to name and store what is playing */
        save_open();
    if (menu_input(press, btn)) {                 /* a menu page: its own encoders */
        ui.btn = btn;
        leds(btn | page_leds(), keys);
        return;
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        ui.shown = cat_step(ui.shown < NPRESETS ? ui.shown : 0, e > 0 ? 1 : -1);
        ui.turned_at = plat_ms();
        ui.redraw = 1;
    }
    if (mn.learn && (e = plat_enc(EN_SELECT)) != 0) {   /* LEARN on home: SELECT picks the macro */
        mn.learn_m = (mn.learn_m + (e > 0 ? 1 : 3)) & 3;
        mn.learn_src = 0;
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
            mn.learn_m = (int)k;              /* the macro LEARN links: the last one turned */
            mn.learn_src = 0;
            if (mn.learn) {                   /* LEARN: the knob picks its macro, its value stays */
                ui.redraw = 1;
                continue;
            }
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
    seq_tick();
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
    if ((set.magic == SET_MAGIC2 || set.magic == SET_MAGIC3 || set.magic == SET_MAGIC4 || set.magic == SET_MAGIC5) &&
        n > (int)((char *)&set.glo[19] - (char *)&set) && n <= (int)(sizeof set - 4u * sizeof(float))) {
        /* RKL2-5 had GLOBAL's 19 values (update 40 added CLICK's 4): SEL and ARP move up, CLICK at its defaults */
        float *f = set.glo;
        int tail = (n - (int)((char *)&set.glo[19] - (char *)&set)) / (int)sizeof(float);
        for (i = tail - 1; i >= 0; i--)
            f[NG + i] = f[19 + i];
        for (i = 19; i < NG; i++)
            f[i] = UP_GLO[i].def;
        n += (int)(4u * sizeof(float));
    }
    if (n == (int)sizeof set && set.magic == SET_MAGIC4) {   /* RKL4: two taps on SEL may have silenced the keys */
        set.arp[A_LCH] = 1.0f;
        set.magic = SET_MAGIC5;
    }
    if (n == (int)(sizeof set - 2u * sizeof(float)) && set.magic == SET_MAGIC3) {   /* RKL3: ON FM-1 was CHORD, BASS, ARP */
        float lc = set.arp[15], lb = set.arp[16], la = set.arp[17];
        set.arp[A_LBS] = lb;
        set.arp[A_LCH] = lc;
        set.arp[A_SCH] = UP_ARP[A_SCH].def;
        set.arp[A_LAR] = la;
        set.arp[A_SAR] = UP_ARP[A_SAR].def;
        set.magic = SET_MAGIC5;
        n = (int)sizeof set;
    }
    if (n == (int)(sizeof set - 12u * sizeof(float)) && set.magic == SET_MAGIC2) {   /* RKL2: the same, shorter */
        for (i = A_STYLE; i < NA; i++)
            set.arp[i] = UP_ARP[i].def;
        set.magic = SET_MAGIC5;
        n = (int)sizeof set;
    }
    if (n == (int)sizeof set && set.magic == SET_MAGIC5)   /* (CLICK at its defaults, above) */
        set.magic = SET_MAGIC;
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
    sq_init();
    users_load();
    if (set.preset >= NPRESETS && !us.used[set.preset - NPRESETS])
        set.preset = 0;
    load_entry(set.preset);
    vz.t0 = vz.at_ms = plat_ms();
}
