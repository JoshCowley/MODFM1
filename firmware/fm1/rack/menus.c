/* SPDX-License-Identifier: MPL-2.0 */
/* FM1 Rack firmware: the sound-editing menus, ported from the page (web/src/4-pages.js, 5-controls.js), part of
 * app.c (included after the screen's helpers). The page's buttons, as printed on the FM-1:
 *
 *   EDIT   the source (SRC), then OSC     ENV   AMP, then the shaper (SHP)
 *   LFO    MOD A, MOD B, then the LFOs    FX    FX 1, then FX 2
 *   ARP    the matrix, then the arp       HOME  home; on home, the rack and back
 *   SEL    scale + chord, then the keys   GLO   GLOBAL: settings, playing, tuning, MIDI, master, system
 *
 * On a module's page: SELECT turns its pages, K1-K4 its four parameters, ALGORITHM opens the picker (press the
 * slot's button again to load the module shown; HOME closes it). On AMP and the LFOs the same; the LFOs' last three
 * pages are each LFO's two direct targets (K1 / K3 a destination, K2 / K4 its amount). On the matrix: SELECT the
 * row, K1 source, K2 destination, K3 amount, K4 via. Parameters and modules come from ui_defs.h (make_ui.py, from
 * the page's own definitions). SEL, GLO and ARP are settings, not the patch's: their values live in set (play.c plays
 * them) and are saved by themselves a few seconds after they change. SAVE and the library: save.c; the wells: vis.c,
 * vis3.c. Still to come from the page: the sequencer, MSEQ and TOOLS.
 *
 * OSC (the firmware's own, not on the page): a plain oscillator in each voice, mixed with the source before the
 * shaper: WAVE OCTAVE SEMI FINE, then PW, OSC LVL and MAIN LVL (the mixer). Its parameters are AMP's 24-30 in the
 * engine, so the matrix, the monitor and SAVE reach them as AMP's (shown as OSC.).
 *
 * LEARN (as the page): hold HOME half a second; the macro to link is the last one turned on home (SELECT or K1-K4 on
 * home pick it while learning). On a module, AMP, LFO or OSC page, turning a parameter's knob links that macro to it
 * in the matrix (the first free row, or the row it already has) and the turn sets the amount. Tap HOME to finish. */
#include "ui_defs.h"
static void entry_name(int e, char *t);

enum { PG_HOME, PG_RACK, PG_SRC, PG_SHP, PG_MODA, PG_MODB, PG_FX1, PG_FX2, PG_AMP, PG_LFO, PG_MATRIX, PG_SEL, PG_GLO,
       PG_ARP, PG_OSC, PG_SEQ, PG_MSEQ };
static int is_seq_page(int pg);               /* the sequencer's pages (seqpages.c) */
static void seq_encoders(void);
static void seq_delete(void);
static int sp_lasso_off(void);
static int32_t cv_text_acc(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c);
#define OSC_P0 24                             /* OSC's first parameter on AMP (engine/rack.cc OSC_WAVE) */

static struct {
    int page, sub, row;                       /* the page, its page within (SELECT), the matrix row */
    int pick, pick_ix;                        /* the picker: open (1), the choice shown */
    uint32_t edited_at;                       /* the last edit: the buffers are checked when the knobs rest */
    int edited;
    int learn, learn_m, learn_src;            /* learn_src: the source LEARN links (0: the macro learn_m) */
    int lfo_hold;                             /* LFO held: K1 picks LEARN's source, K2-K4 link it */
    uint32_t edit_at;                         /* EDIT went down (a hold is UNDO), 0 up */
    int edit_long;
    uint32_t sel_at;                          /* SEL went down, 0 up */
    int sel_long;
    uint32_t combo_at;                        /* SEL + EDIT both went down (held: LEARN on or off), 0 both up */
    int combo_done;
    uint32_t lfo_at;                          /* LFO tapped, waiting for a second tap (two taps: the MATRIX) */
    uint32_t sel_tap, arp_tap, arp_at;        /* SEL / ARP let go, waiting for another tap; ARP went down */
    int sel_n, arp_n, arp_long;               /* taps so far: 1 the page, 2 and 3 their toggles */
    int tap_pg, tap_sub;                      /* the page before a first tap opened SEL / ARP (two or three: back to it) */
    float chord_mem;                          /* the chord type two taps on SEL turned off (what they turn back on) */
    uint32_t fx_at, fx_arm, fx_tap;           /* FX down (held: FX on/off armed), armed since, first tap of two */
    int fx_long;
    uint32_t glo_at;                          /* GLO down (held: DAYLIGHT on or off, update 55) */
    int glo_long;
    int mtab, hrow[4];                        /* the matrix hub's table (ALGORITHM): ROUTES ENV EDIT OSC; each one's row */                       /* LEARN on; the macro it links (0-3: M1-M4) */
    uint32_t home_at;                         /* HOME went down (a hold starts LEARN), 0 up */
    int home_long;
} mn;

static const uint8_t SRC_CH[] = {1, 2};
static const uint8_t SHP_CH[] = {10, 12, 0};
static const uint8_t MOD_CH[] = {20, 21, 23, 24, 0};
static const uint8_t FX_CH[] = {31, 32, 33, 0};
static const char *const SRC_NAMES[] = {"-", "M1", "M2", "M3", "M4", "VELO", "KEY TRK", "RANDOM", "AMP ENV", "A OUT 1",
                                        "A OUT 2", "B OUT 1", "B OUT 2", "LFO 1", "LFO 2", "LFO 3", "MOD ENV", "SEQ 1",
                                        "SEQ 2", "SEQ 3", "SEQ 4"};
#define NSRC_UI 21

static const ui_mod_t *ui_mod(int id)
{
    int i;
    for (i = 0; i < UI_NMODS; i++)
        if (UI_MODS[i].id == id)
            return &UI_MODS[i];
    return 0;
}

static int page_slot(int pg) { return pg >= PG_SRC && pg <= PG_FX2 ? pg - PG_SRC : pg == PG_AMP || pg == PG_OSC ? RS_AMP : pg == PG_LFO ? RS_LFO : -1; }
static int pofs(int pg) { return pg == PG_OSC ? OSC_P0 : 0; }   /* the page's first parameter in its slot */
static int is_fixed(int pg) { return pg >= PG_SEL && pg <= PG_ARP; }
/* SEL's pages 3-5 (update 38: the MIDI controller pages, STYLE, LAYER MIDI CH, CHORD + ARP, moved from ARP) show
 * ARP's settings 3-5: the page whose values a fixed page shows */
#define SEL_OWN 2                             /* SEL's own pages; ARP keeps 2 */
static int data_pg(int pg) { return pg == PG_SEL && mn.sub >= SEL_OWN ? PG_ARP : pg; }
/* RHYTHM (update 48): SEL's pages 7-8 are ARP's 6-7; K1 the generator shown (GEN), K2-K4 its values (the CHORD's and
 * the ARP's sit on ARP pages 8-11, never shown): the parameter knob k sets there, or -1 */
#define RHY_PG 6
static void fixed_controls(int pg);
static int rhy_pi(int pg, int sub, int k)
{
    int g = (int)(set.arp[A_RGEN] + 0.5f);
    if (pg != PG_SEL || (sub != RHY_PG && sub != RHY_PG + 1))
        return -2;                            /* not a RHYTHM page */
    if (k == 0)
        return A_RGEN;
    if (k > 3)
        return -1;
    return A_R0 + g * 6 + (sub == RHY_PG ? 0 : 3) + k - 1;
}
static float *page_vals(int pg) { return pg == PG_GLO ? set.glo : pg == PG_SEL ? set.sel : pg == PG_ARP ? set.arp : 0; }
static uint16_t fixed_colour(int pg) { return pg == PG_GLO ? K_GLO : pg == PG_SEL ? K_CHD : K_ARP; }
static const ui_mod_t *page_mod(int pg)
{
    if (pg == PG_GLO)
        return &UI_GLO;
    if (pg == PG_SEL)
        return &UI_SEL;
    if (pg == PG_ARP)
        return &UI_ARP;
    if (pg == PG_OSC)
        return &UI_OSC;
    if (pg == PG_AMP)
        return &UI_AMP;
    if (pg == PG_LFO)
        return &UI_LFO;
    return pg >= PG_SRC && pg <= PG_FX2 ? ui_mod(rc_patch.mod[pg - PG_SRC]) : 0;
}
static uint16_t slot_colour(int slot)
{
    const uint16_t C[] = {K_SRC, K_SHP, K_MOD, K_MOD, K_FX, K_FX, K_AMP, K_FG, K_LFO};
    return slot >= 0 && slot < 9 ? C[slot] : K_FG;
}
static const char *const SLOT_TAG[] = {"SRC", "SHP", "MOD A", "MOD B", "FX 1", "FX 2", "AMP", "GLO", "LFO"};
static uint16_t page_colour(int pg) { return is_fixed(pg) ? fixed_colour(pg) : pg == PG_OSC ? K_OSC : pg == PG_SEQ ? K_SEQC : pg == PG_MSEQ ? K_LFO : slot_colour(page_slot(pg)); }
static void choices(int slot, const uint8_t **ch, int *n)
{
    if (slot == RS_SRC) { *ch = SRC_CH; *n = 2; }
    else if (slot == RS_SHP) { *ch = SHP_CH; *n = 3; }
    else if (slot == RS_MODA || slot == RS_MODB) { *ch = MOD_CH; *n = 5; }
    else { *ch = FX_CH; *n = 4; }
}
static int page_first(const ui_mod_t *m, int sub)
{
    int i, o = 0;
    for (i = 0; i < sub && i < m->npages; i++)
        o += m->pages[i];
    return o;
}
/* a module, AMP or OSC page ends in its MOD page (12 Nov 2026, after the Dirtywave M8's mod slots): the matrix rows
 * into that module, four at a time. SELECT steps through its four rows as it steps through pages (MOD_ROWS subs) */
#define MOD_ROWS 4
static int has_mod_page(int pg) { return pg == PG_AMP || pg == PG_OSC || (pg >= PG_SRC && pg <= PG_FX2 && page_mod(pg)); }
/* the knobs' colours (12 Nov 2026, after Teenage Engineering's colour-linked encoders): K1 blue, K2 green, K3 white,
 * K4 orange, on a page's gauges and on the part of its picture each one moves, so the knob and what it changes on the
 * screen read as one */
#define KNOB_C (fm1_pal + P_KN1)                  /* K1-K4's colours (theme.h; update 55: the palette in use) */
static int vk_on, vk_first, vk_n;             /* the picture being drawn: its parameters vk_first.. on K1.. */
static uint16_t kc(int pi, uint16_t c)        /* parameter pi's knob's colour when it is on the page, else c */
{
    int k = pi - vk_first;
    return vk_on && k >= 0 && k < vk_n ? KNOB_C[k] : c;
}
static int npages(int pg)
{
    const ui_mod_t *m = page_mod(pg);
    if (pg == PG_SEL) return SEL_OWN + 6;
    if (pg == PG_ARP) return 2;
    return m ? m->npages + (pg == PG_LFO ? 3 : 0) + (has_mod_page(pg) ? MOD_ROWS : 0) : 1;
}
static int is_mod_sub(int pg) { const ui_mod_t *m = page_mod(pg); return m && has_mod_page(pg) && mn.sub >= m->npages; }

/* ---------------------------------------------------------- values as the page shows them */
static void fmt(const ui_param_t *p, float v, char *t)
{
    int32_t i = (int32_t)(v < 0.0f ? v - 0.5f : v + 0.5f);
    t[0] = 0;
    if (p->t == UT_LIST) {
        if (i < p->lo) i = p->lo;
        if (i > p->hi) i = p->hi;
        cat(t, UI_OPT[p->opt + i]);
    } else if (p->t == UT_INT) {
        int_text(i, t);
    } else if (p->t == UT_BIP) {
        i = (int32_t)(v * 100.0f + (v < 0.0f ? -0.5f : 0.5f));
        if (i >= 0)
            cat(t, "+");
        int_text(i, t + (i >= 0 ? 1 : 0));
    } else {
        int_text((int32_t)(v * 100.0f + 0.5f), t);
    }
}
static float step(const ui_param_t *p, float v, int32_t k)
{
    if (p->t == UT_LIST || p->t == UT_INT) {
        int32_t i = (int32_t)(v < 0.0f ? v - 0.5f : v + 0.5f) + k;
        return (float)(i < p->lo ? p->lo : i > p->hi ? p->hi : i);
    }
    v += (float)k * (p->t == UT_BIP ? 0.02f : 0.01f) * (k > 1 || k < -1 ? 2.0f : 1.0f);
    return v < (float)p->lo ? (float)p->lo : v > (float)p->hi ? (float)p->hi : v;
}
static float frac(const ui_param_t *p, float v)
{
    if (p->t == UT_BIP)
        return (v + 1.0f) * 0.5f;
    if (p->hi == p->lo)
        return 0.0f;
    return (v - (float)p->lo) / (float)(p->hi - p->lo);
}

/* ---------------------------------------------------------- the matrix's destinations */
/* a destination: (slot + 1) << 8 | param; 0 none. PITCH and PAN (slot -1), AMP, the LFOs, then each loaded module */
static int dst_list(uint16_t *out)
{
    int n = 0, s, i;
    out[n++] = 0;
    out[n++] = 0 << 8 | 0;
    out[n++] = 0 << 8 | 1;
    for (s = 0; s < 8; s++) {
        int slot = s < 6 ? s : s == 6 ? RS_AMP : RS_LFO;
        const ui_mod_t *m = slot == RS_AMP ? &UI_AMP : slot == RS_LFO ? &UI_LFO : slot >= 0 ? ui_mod(rc_patch.mod[slot]) : 0;
        if (!m)
            continue;
        for (i = 0; i < m->nparams; i++)
            if (m->p[i].mod && n < 160)
                out[n++] = (uint16_t)((slot + 1) << 8 | i);
        if (slot == RS_AMP)                   /* OSC, after AMP */
            for (i = 0; i < UI_OSC.nparams; i++)
                if (UI_OSC.p[i].mod && n < 160)
                    out[n++] = (uint16_t)((slot + 1) << 8 | (OSC_P0 + i));
    }
    out[1] |= 0x8000u;                          /* PITCH, PAN: marked apart from "none" (both slot -1) */
    out[2] |= 0x8000u;
    return n;
}
static void dst_name(uint16_t d, char *t)
{
    int slot = (int)((d >> 8) & 0x7F) - 1, i = d & 0xFF;
    const ui_mod_t *m;
    t[0] = 0;
    if (!d) { cat(t, "-"); return; }
    if (slot < 0) { cat(t, i ? "PAN" : "PITCH"); return; }
    if (slot == RS_AMP && i >= OSC_P0) {
        cat(t, "OSC.");
        cat(t, i - OSC_P0 < UI_OSC.nparams ? UI_OSC.p[i - OSC_P0].l : "?");
        return;
    }
    m = slot == RS_AMP ? &UI_AMP : slot == RS_LFO ? &UI_LFO : ui_mod(rc_patch.mod[slot]);
    cat(t, SLOT_TAG[slot]);
    cat(t, ".");
    cat(t, m && i < m->nparams ? m->p[i].l : "?");
}

/* ---------------------------------------------------------- UNDO (12 Nov 2026): hold EDIT to undo the last change.
 * A journal of the values edits replaced: a parameter, a matrix row, a GLOBAL/SEL/ARP setting. Turning one knob
 * keeps adding to one entry until another is touched or a second passes; a preset load or a module swap lapses
 * everything before it */
#define UNDO_N 16
enum { UN_PARAM = 1, UN_ROUTE, UN_FIXED };
typedef struct { uint8_t kind; int8_t a; uint8_t b; uint8_t src, via; uint16_t dst; float v; uint32_t gen, at; } undo_t;
static undo_t undo_j[UNDO_N];
static int undo_n;
static int undo_replay;                       /* 1 while undoing: the journal isn't written */
static void undo_note(int kind, int a, int b, float v, int src, uint16_t dst, int via)
{
    undo_t *u = undo_n ? &undo_j[undo_n - 1] : 0;
    uint32_t now = plat_ms();
    if (undo_replay)
        return;
    if (u && u->kind == kind && u->a == a && u->b == b && u->gen == rc_gen && now - u->at < 1000u) {
        u->at = now;                          /* the same gesture: keep the first value */
        return;
    }
    if (undo_n == UNDO_N) {
        int k;
        for (k = 0; k < UNDO_N - 1; k++)      /* (the FM-1 has no memmove) */
            undo_j[k] = undo_j[k + 1];
        undo_n--;
    }
    u = &undo_j[undo_n++];
    u->kind = (uint8_t)kind; u->a = (int8_t)a; u->b = (uint8_t)b; u->v = v;
    u->src = (uint8_t)src; u->dst = dst; u->via = (uint8_t)via; u->gen = rc_gen; u->at = now;
}
static uint16_t route_dst(int i, int *src, int *via, float *amt)
{
    int r[4];
    *amt = rk_get_route(i, r);
    *src = r[0];
    *via = r[3];
    if (!r[0])
        return 0;
    return (uint16_t)(((r[1] + 1) << 8 | r[2]) | (r[1] < 0 ? 0x8000u : 0u));
}
static void route_set(int i, int src, uint16_t dst, float amt, int via)
{
    {
        int os, ov;
        float oa;
        uint16_t od = route_dst(i, &os, &ov, &oa);
        undo_note(UN_ROUTE, i, 0, oa, os, od, ov);
    }
    int slot = dst ? (int)((dst >> 8) & 0x7F) - 1 : 0, param = dst & 0xFF;
    rk_set_route(i, dst ? src : 0, slot, param, amt, via);
}
static int src_ok(int s)                      /* a source the patch has: a modulator's outputs only when loaded */
{
    const ui_mod_t *m;
    if (s < 9 || s > 12)
        return 1;
    m = ui_mod(rc_patch.mod[s < 11 ? RS_MODA : RS_MODB]);
    return m && (s - 9) % 2 < m->nouts;
}
static const char *src_name(int s)
{
    static char t[12];
    const ui_mod_t *m;
    if (s >= 9 && s <= 12 && (m = ui_mod(rc_patch.mod[s < 11 ? RS_MODA : RS_MODB])) && (s - 9) % 2 < m->nouts) {
        t[0] = s < 11 ? 'A' : 'B';
        t[1] = ' ';
        t[2] = 0;
        cat(t, m->outs[(s - 9) % 2]);
        return t;
    }
    return s >= 0 && s < NSRC_UI ? SRC_NAMES[s] : "?";
}
static uint16_t src_colour(int s)             /* the colour a modulation source is drawn in, everywhere */
{
    /* each kind of modulation its own colour, the same on every page (the sliders' halos, the matrix) */
    if (s >= 1 && s <= 4) return MC_MACRO;      /* the macros: gold */
    if (s == 5) return MC_VEL;                 /* velocity: green */
    if (s == 6) return MC_KEY;                 /* key tracking: sky blue */
    if (s == 7) return MC_RAND;                /* random: white */
    if (s == 8 || s == 16) return MC_ENV;     /* the envelopes: coral */
    if (s >= 9 && s <= 12) return MC_MOD;      /* MOD A and B's outputs: orange */
    if (s >= 13 && s <= 15) return MC_LFO;    /* the LFOs: violet */
    if (s >= 17 && s <= 20) return MC_MSEQ;    /* MSEQ's lanes: pink */
    return K_DIM;
}
/* the colour of the first route into (slot, param), 0 none: a dial shows its modulation in it */
static uint16_t mod_colour(int slot, int param)
{
    int r, src, via;
    float amt;
    uint16_t want = (uint16_t)((slot + 1) << 8 | param);
    for (r = 0; r < 18; r++)
        if (route_dst(r, &src, &via, &amt) == want && src && fm_fabsf(amt) > 0.001f)
            return src_colour(src);
    return 0;
}

static int src_step(int s, int k)
{
    int n;
    for (n = 0; n < NSRC_UI; n++) {
        s = (s + (k > 0 ? 1 : -1) + NSRC_UI) % NSRC_UI;
        if (src_ok(s))
            break;
    }
    return s;
}
static int learn_source(void) { return mn.learn_src ? mn.learn_src : 1 + mn.learn_m; }
static uint16_t dst_step(uint16_t d, int k)
{
    static uint16_t list[160];
    int n = dst_list(list), i, at = 0;
    for (i = 0; i < n; i++)
        if (list[i] == d)
            at = i;
    at = (at + (k > 0 ? 1 : -1) + n) % n;
    return list[at];
}

/* ---------------------------------------------------------- drawing */
static void header(const char *tag, uint16_t c, const char *title, int ic, int pages, int sub)
{
    int32_t w, i;
    cv_begin(240, 22, K_BAR);                 /* (stage 4: under the status strip, 22 rows) */
    icon(ic, 6, 5, c);
    w = text_w(&FONT_T, tag) + 8;
#if FM1_HEAD == 1
    rrect(22, 4, w, 14, mix565(c, K_BAR, 90), 3);   /* (update 51) the tag outlined, not a pill of colour */
    rrect(23, 5, w - 2, 12, K_BAR, 2);
    cv_text(26, 4, &FONT_T, tag, c);
#elif FM1_HEAD == 3
    cv_text(24, 4, &FONT_T, tag, c);          /* the tag as coloured words */
    w -= 2;
#else
    w = -4;                                   /* no tag: the icon, then the name */
#endif
    cv_text_acc(28 + w, 3, &FONT_B, title, K_FG);
    if (pal_day) cv_text_acc(29 + w, 3, &FONT_B, title, K_FG);
    for (i = 0; i < pages && pages > 1; i++) {
        int32_t x = 232 - (pages - 1 - i) * 9, r = i == sub ? 3 : 2;
        rrect(x - r, 11 - r, 2 * r + 1, 2 * r + 1, i == sub ? K_FG : K_DIM, r);
    }
    cv_blit(0, 10);
}

/* the AMP envelope's shape, from ATTACK DECAY SUSTAIN RELEASE */
static void adsr(int32_t w, int32_t h, uint16_t c)
{
    float a = rk_get_param(RS_AMP, 0), d = rk_get_param(RS_AMP, 1), s = rk_get_param(RS_AMP, 2), r = rk_get_param(RS_AMP, 3);
    int32_t x0 = 6, top = 5, bot = h - 5, ax = x0 + (int32_t)(a * 50.0f) + 1, dx = ax + (int32_t)(d * 50.0f) + 1;
    int32_t sy = bot - (int32_t)(s * (float)(bot - top)), sx = dx + 40, rx = sx + (int32_t)(r * 60.0f) + 1;
    uint16_t ca = kc(0, c), cd = kc(1, c), cs = kc(2, c), cr = kc(3, c);
    int i;
    if (w > 228) w = 228;
    if (h > 60) {                             /* the big picture: the envelope across the width */
        float k = (float)(w - 12) / 150.0f;
        ax = x0 + (int32_t)(a * 50.0f * k) + 1; dx = ax + (int32_t)(d * 50.0f * k) + 1;
        sx = dx + (int32_t)(40.0f * k); rx = sx + (int32_t)(r * 60.0f * k) + 1;
        if (vk_on && vk_n) {                  /* the segments' floor, shaded in their knobs' colours */
            cv_rect(x0, bot + 3, ax - x0, 2, ca); cv_rect(ax, bot + 3, dx - ax, 2, cd);
            cv_rect(dx, bot + 3, sx - dx, 2, cs); cv_rect(sx, bot + 3, rx - sx, 2, cr);
        }
    }
    for (i = -1; i <= 1; i += 2) {
        cv_line(x0, bot + i, ax, top + i, mix565(ca, K_WELL, 46));
        cv_line(ax, top + i, dx, sy + i, mix565(cd, K_WELL, 46));
        cv_line(dx, sy + i, sx, sy + i, mix565(cs, K_WELL, 46));
        cv_line(sx, sy + i, rx, bot + i, mix565(cr, K_WELL, 46));
    }
    cv_line(x0, bot, ax, top, ca);
    cv_line(ax, top, dx, sy, cd);
    cv_line(dx, sy, sx, sy, cs);
    cv_line(sx, sy, rx, bot, cr);
}

#include "vis.c"
#include "vis3.c"
#undef cv_text                                /* (vis.c and vis3.c draw through vm_*: scalable) */
#undef cv_line
#undef cv_pset
#undef cv_rect
#undef rrect

static void well(int pg, const ui_mod_t *m, uint16_t c)
{
    int32_t x, i, slot = page_slot(pg);
    cv_begin(228, 42, K_BG);
    rrect(0, 0, 228, 42, K_WELL, 4);
    for (x = 12; x < 228; x += 12)
        for (i = 6; i < 42; i += 6)
            cv_pset(x, i, K_GRID);
    if (is_fixed(pg))
        vis_fixed(pg, mn.sub, c);
    else if (pg == PG_OSC)
        vis_osc(c);
    else
        vis_draw(slot, slot >= 0 && slot < 6 ? rc_patch.mod[slot] : 0, mn.sub, c);
    if (m && !is_fixed(pg) && pg != PG_OSC)
        cv_text(220 - text_w(&FONT_T, m->blurb), 2, &FONT_T, m->blurb, K_DIM);
    cv_blit(6, 34);
}

/* a page's cells (after stage 4): four parameters, each with its number, label, value and a slider; a parameter
 * the matrix moves glows in its source's colour (the halo: the range it has swept lately round the track, the live
 * value, a glow round the thumb and the cell's edge that brightens as the modulation moves; mc: the colour, 0 none) */
static float cl_lo[4], cl_hi[4], cl_prev[4], cl_e[4];
static int cl_page = -1;
static void cell(int i, const ui_param_t *p, float v, float live, uint16_t mc, uint16_t c)
{
    char t[24];
    int32_t x = i & 1 ? 122 : 6, y = CELL_Y(i);
    float f, lf;
    int key = mn.page << 4 | mn.sub;
    if (key != cl_page) {                     /* a new page: the swept ranges start again */
        int k;
        cl_page = key;
        for (k = 0; k < 4; k++)
            cl_lo[k] = 2.0f, cl_hi[k] = -2.0f, cl_prev[k] = -1.0f, cl_e[k] = 0.0f;
    }
    cv_begin(112, CELL_H, K_BG);
    rrect(0, 0, 112, CELL_H, p ? K_CELL : GREY(9), 4);
    t[0] = (char)('1' + i);
    t[1] = 0;
    cv_text(6, 2, &FONT_T, t, p ? KNOB_C[i] : K_FAINT);
    if (!p) {
        rrect(8, 52, 96, 4, GREY(19), 2);
        cv_blit((uint32_t)x, (uint32_t)y);
        return;
    }
    cv_text(16, 2, &FONT_T, p->l, K_DIM);
    f = frac(p, v);
    lf = frac(p, live);
    if (mc) {
        float d;
        if (cl_lo[i] > 1.5f) cl_lo[i] = cl_hi[i] = lf;
        if (lf < cl_lo[i]) cl_lo[i] = lf; else cl_lo[i] += (lf - cl_lo[i]) * 0.01f;
        if (lf > cl_hi[i]) cl_hi[i] = lf; else cl_hi[i] += (lf - cl_hi[i]) * 0.01f;
        d = cl_prev[i] < 0.0f ? 0.0f : fm_fabsf(lf - cl_prev[i]) * 25.0f;
        cl_prev[i] = lf;
        cl_e[i] += ((d > 1.0f ? 1.0f : d) - cl_e[i]) * 0.3f;
        {                                     /* the cell's edge glows */
            uint16_t g = mix565(mc, K_CELL, 110u + (uint32_t)(cl_e[i] * 146.0f));
            cv_rect(2, 0, 108, 1, g);
            cv_rect(2, CELL_H - 1, 108, 1, g);
            cv_rect(0, 2, 1, CELL_H - 4, g);
            cv_rect(111, 2, 1, CELL_H - 4, g);
        }
    }
    fmt(p, v, t);
    cv_text(6, 16, text_w(&FONT_B, t) > 100 ? (text_w(&FONT_XS, t) > 100 ? &FONT_T : &FONT_XS) : &FONT_B, t, K_FG);
    if (mc) {
        char u[24] = "> ";
        fmt(p, live, u + 2);
        cv_text(106 - text_w(&FONT_T, u), 34, &FONT_T, u, mc);
    }
    slider(8, 52, 96, f, c, mc, cl_lo[i], cl_hi[i], lf, cl_e[i], K_CELL);
    cv_blit((uint32_t)x, (uint32_t)y);
}


/* ---------------------------------------------------------- picture-first pages (12 Nov 2026, after Teenage
 * Engineering's screens: the moving picture explains the module, four colour-linked gauges set it) */
#define BW_Y 34                               /* the big picture: 136 rows from here, drawn in bands of 30 */
#define BW_H 136                              /* (update 50: the sliders give the picture 48 rows more) */
#define G4_Y 176                              /* the four controls: a column each, a slider in each */
#define G4_H 46
static int picture_first(int pg)              /* (13 Nov 2026: every page with a picture, but GLOBAL's SYSTEM) */
{
    if (is_mod_sub(pg))
        return 0;
    if (pg == PG_LFO)
        return mn.sub < UI_LFO.npages;
    if (is_fixed(pg))
        return !(pg == PG_GLO && mn.sub == 5);
    return pg == PG_AMP || pg == PG_OSC || (pg >= PG_SRC && pg <= PG_FX2 && page_mod(pg));
}
static void big_well_h(int pg, const ui_mod_t *m, uint16_t c, int32_t wh)
{
    int slot = page_slot(pg), bnd, id = slot >= 0 && slot < 6 ? rc_patch.mod[slot] : 0, nb = (wh + 29) / 30;
    int32_t x, i;
    vk_on = !is_fixed(pg) && m && mn.sub < m->npages;
    vk_first = vk_on ? page_first(m, mn.sub) : 0;
    vk_n = vk_on ? m->pages[mn.sub] : 0;
    for (bnd = 0; bnd < nb; bnd++) {
        int32_t y0 = bnd * 30, hb = wh - y0 < 30 ? wh - y0 : 30;
        cv_begin(228, (uint32_t)hb, K_BG);
        cv_band(-y0, hb);
        cv_y0 = 0;
        cv_y1 = hb;
#if FM1_WELL == 1                             /* (theme.h FM1_WELL) a dark well and its dot grid */
        rrect(0, 0, 228, wh, K_WELL, 4);
        for (x = 12; x < 228; x += 12)
            for (i = 8; i < wh; i += 8)
                cv_pset(x, i, K_GRID);
#elif FM1_WELL == 3                           /* a hairline edge, no grid */
        rrect(0, 0, 228, wh, K_GRID, 4);
        rrect(1, 1, 226, wh - 2, K_WELL, 3);
        (void)x; (void)i;
#elif FM1_WELL >= 4                           /* a card or an inset: its edge, a quiet grid inside */
        rrect(0, 0, 228, wh, K_EDGE, 4);
        rrect(1, 1, 226, wh - 2, K_WELL, 3);
#if FM1_WELL == 6                             /* (update 53: Josh picked the bevelled card, and no panel grid) */
        cv_rect(3, 0, 222, 1, K_EDGE_HI);
        cv_rect(3, wh - 1, 222, 1, K_EDGE_LO);
        (void)x; (void)i;
#else
        for (x = 16; x < 228; x += 16)
            for (i = 12; i < wh - 4; i += 12)
                cv_pset(x, i, mix565(K_FG, K_WELL, 22));
#endif
#else                                         /* the picture straight on the page */
        (void)x; (void)i;
#endif
        if (slot == RS_AMP && pg != PG_OSC) {
            adsr(228, wh, c);
        } else {
            vm_on = 1;
            vm_lab = 1;
            vm_ox = 0;
            vm_oy = 0;
            vm_sx = 1.0f;
            vm_sy = (float)wh / (float)VH;
            if (is_fixed(pg)) vis_fixed(pg, mn.sub, c);
            else if (pg == PG_OSC) vis_osc(c);
            else vis_draw(slot, id, mn.sub, c);
            vm_on = 0;
            vm_lab = 0;
        }
        if (m && pg != PG_AMP && pg != PG_LFO && pg != PG_OSC && !is_fixed(pg) && m->id != 24)   /* (FUNC labels its own) */
            cv_text(220 - text_w(&FONT_T, m->blurb), 2, &FONT_T, m->blurb, K_DIM);
        cv_blit(6, (uint32_t)(BW_Y + y0));
    }
    vk_on = 0;
}
static void big_well(int pg, const ui_mod_t *m, uint16_t c) { big_well_h(pg, m, c, BW_H); }
/* the page's picture, as big as the page leaves room for: 88 rows on picture-first pages, 60 on a MOD page, else
 * the band of 42 */
static void page_well(int pg)
{
    const ui_mod_t *m = page_mod(pg);
    uint16_t c = page_colour(pg);
    if (picture_first(pg))
        big_well(pg, m, c);
    else if (is_mod_sub(pg))
        big_well_h(pg, m, c, 60);
    else
        well(pg, pg == PG_AMP || pg == PG_LFO || pg == PG_OSC ? 0 : m, c);
}
/* one of the four controls (update 50: sliders, not dials, after Josh: a dial spends a square on one number). A
 * column each, in its knob's colour: the number and name, the value large, and under it the slider. An amount
 * fills the slider; a choice is not an amount (after Polyend's and Elektron's users): a list of two is a switch,
 * a short list or whole-number range shows its stops, a long one its place in the count. A modulated parameter
 * shows the range it has swept lately round the slider in its source's colour, its live value as the thumb */
static void gauge(int i, const ui_param_t *p, float v, float live, uint16_t mc)
{
    char t[24];
    int32_t w, sx = 3, sy = 38, sw = 50;
    float f, lf;
    int key = mn.page << 4 | mn.sub, nst;
    uint16_t kcol = KNOB_C[i];
    if (key != cl_page) {
        int k;
        cl_page = key;
        for (k = 0; k < 4; k++)
            cl_lo[k] = 2.0f, cl_hi[k] = -2.0f, cl_prev[k] = -1.0f, cl_e[k] = 0.0f;
    }
    cv_begin(57, G4_H, K_BG);
    t[0] = (char)('1' + i);                   /* the knob's number in its colour (update 51: no disc), then its name */
    t[1] = 0;
    cv_text(2, 0, &FONT_T, t, p ? kcol : K_FAINT);
    if (!p) {
        rrect(sx, sy + 1, sw, 2, GREY(23), 1);
        cv_blit(3u + (uint32_t)i * 59u, G4_Y);
        return;
    }
    {
        char l[16];
        int k = 0;
        while (p->l[k] && k < 15) { l[k] = p->l[k]; k++; }
        l[k] = 0;
        while (text_w(&FONT_T, l) > 45 && k > 1) l[--k] = 0;
        cv_text(10, 0, &FONT_T, l, K_DIM);
    }
    f = fm_clampf(frac(p, v), 0.0f, 1.0f);
    lf = fm_clampf(frac(p, live), 0.0f, 1.0f);
    nst = p->t == UT_LIST || p->t == UT_INT ? p->hi - p->lo + 1 : 0;
    fmt(p, v, t);                             /* the value, large; too wide: the small face, then cut */
    if ((w = text_w(&FONT_B, t)) <= 54) {
        cv_text(2, 14, &FONT_B, t, K_FG);
        if (pal_day) cv_text(3, 14, &FONT_B, t, K_FG);   /* (DAYLIGHT: bold) */
    } else {
        while (text_w(&FONT_T, t) > 54 && t[0] && t[1]) { int k = 0; while (t[k]) k++; t[k - 1] = 0; }
        w = text_w(&FONT_T, t);
        cv_text(2, 17, &FONT_T, t, K_FG);
    }
    w += 2;                                   /* (where the value ends: a note beside it must start after) */
    if (p->t == UT_LIST && nst == 2) {        /* the switch */
        int on = f > 0.5f;
        rrect(sx, sy - 3, 20, 10, on ? kcol : K_FAINT, 5);
        if (!on) rrect(sx + 1, sy - 2, 18, 8, K_BG, 4);
        rrect(on ? sx + 11 : sx + 2, sy - 1, 7, 6, on ? K_FG : K_DIM, 3);
        cv_blit(3u + (uint32_t)i * 59u, G4_Y);
        return;
    }
    if (mc) {                                 /* modulated: the range it sweeps, the live value as the thumb */
        float d;
        if (cl_lo[i] > 1.5f) cl_lo[i] = cl_hi[i] = lf;
        if (lf < cl_lo[i]) cl_lo[i] = lf; else cl_lo[i] += (lf - cl_lo[i]) * 0.01f;
        if (lf > cl_hi[i]) cl_hi[i] = lf; else cl_hi[i] += (lf - cl_hi[i]) * 0.01f;
        d = cl_prev[i] < 0.0f ? 0.0f : fm_fabsf(lf - cl_prev[i]) * 25.0f;
        cl_prev[i] = lf;
        cl_e[i] += ((d > 1.0f ? 1.0f : d) - cl_e[i]) * 0.3f;
        wv_idx = i;
        slider(sx, sy, sw, f, kcol, mc, cl_lo[i], cl_hi[i], lf, cl_e[i], K_BG);
        {
            char u[24] = "> ";
            fmt(p, live, u + 2);
            if (text_w(&FONT_T, u) + w + 3 <= 55)   /* the live value beside it, in the source's colour */
                cv_text(55 - text_w(&FONT_T, u), 17, &FONT_T, u, mc);
        }
        cv_blit(3u + (uint32_t)i * 59u, G4_Y);
        return;
    }
    if (FM1_SLIDER != 1 && p->t != UT_LIST && p->t != UT_BIP && !(nst >= 3 && nst <= 17)) {
        wv_idx = i;                           /* (theme.h FM1_SLIDER: an amount drawn in the style chosen) */
        sl_line(sx, sy, sw, f, kcol);
        cv_blit(3u + (uint32_t)i * 59u, G4_Y);
        return;
    }
#if FM1_SLIDER == 4
    cv_rect(sx, sy + (pal_day ? 0 : 1), sw, pal_day ? 3 : 1, K_FAINT);   /* the hairline style (update 53): a hairline, its stops, a dot (DAYLIGHT: 3 px) */
#else
    rrect(sx, sy + 1, sw, 2, K_FAINT, 1);     /* the track (update 51: 2 px, not 4) */
#endif
    if (nst >= 3 && nst <= 17) {              /* its stops */
        int k2;
        for (k2 = 0; k2 < nst; k2++)
            cv_rect(sx + (int32_t)((float)k2 / (float)(nst - 1) * (float)(sw - 1)), sy + 6, 1, 2, K_DIM);
    }
#if FM1_SLIDER == 4
    {
        int32_t tx = sx + (int32_t)(f * (float)sw);
        wv_idx = i;
        if (p->t == UT_BIP) {                 /* bipolar: a hairline from the middle, its centre marked */
            int32_t m0 = sx + sw / 2;
            uint16_t hc = mix565(kcol, K_BG, 150);
            if (pal_day) hc = kcol;
            if (tx > m0) cv_rect(m0, sy + (pal_day ? 0 : 1), tx - m0, pal_day ? 3 : 1, hc);
            else if (tx < m0) cv_rect(tx, sy + (pal_day ? 0 : 1), m0 - tx, pal_day ? 3 : 1, hc);
            cv_rect(m0, sy - 1, 1, 5, K_DIM);
        } else if (p->t != UT_LIST && tx > sx) {
            cv_rect(sx, sy + (pal_day ? 0 : 1), tx - sx, pal_day ? 3 : 1, pal_day ? kcol : mix565(kcol, K_BG, 150));
        }
        sl_dot(tx, sy, kcol);
    }
    if (0)                                    /* (the other styles' track, fill and thumb) */
#endif
    {
        int32_t tx = sx + (int32_t)(f * (float)sw);
        if (p->t != UT_LIST) {                /* an amount fills; a bipolar one from the middle */
            if (p->t == UT_BIP) {
                int32_t m0 = sx + sw / 2;
                wv_idx = i;
                if (tx > m0) wv_fill(m0, sy, tx - m0, kcol);
                else if (tx < m0) wv_fill(tx, sy, m0 - tx, kcol);
                cv_rect(m0, sy - 1, 1, 6, K_DIM);
            } else if (tx > sx) {
                wv_idx = i;
                wv_fill(sx, sy, tx - sx, kcol);
            }
        } else {
            rrect(tx - 3, sy - 1, 7, 6, kcol, 2);   /* the choice: a block at its place */
        }
        rrect(tx - 1, sy - 3, 3, 10, K_FG, 1);
    }
    if (p->t == UT_LIST && nst > 2) {         /* which of how many */
        char u[12], u2[6];
        int_text((int32_t)(v + 0.5f) - p->lo + 1, u);
        cat(u, "/");
        int_text(nst, u2);
        cat(u, u2);
        if (text_w(&FONT_T, u) + w + 3 <= 55)
            cv_text(55 - text_w(&FONT_T, u), 17, &FONT_T, u, K_DIM);
    }
    cv_blit(3u + (uint32_t)i * 59u, G4_Y);
}

static void help(const char *s)
{
    cv_begin(240, 16, K_BG);
    if (mn.learn) {                           /* LEARN: a gold band in its place */
        char t[48] = "LEARN ";
        cat(t, src_name(learn_source()));
        cat(t, ": ALGO SRC  SEL+EDIT: DONE");
        rrect(2, 0, 236, 15, K_GOLD, 4);
        icon(IC_KNOB, 6, 2, K_BG);
        cv_text(22, 1, &FONT_T, t, K_BG);
    } else {
        cv_text(6, 1, &FONT_T, s, K_DIM);
    }
    cv_blit(0, HELP_Y);
}

/* the LFO's TARGET pages: two direct targets (routes 12-17) */
static void draw_lfo_targets(int l)
{
    int k, src, via;
    float amt;
    char t[32];
    for (k = 0; k < 4; k++) {
        int32_t x = k & 1 ? 122 : 6, y = CELL_Y(k);
        uint16_t d = route_dst(12 + l * 2 + (k >> 1), &src, &via, &amt);
        cv_begin(112, CELL_H, K_BG);
        rrect(0, 0, 112, CELL_H, K_CELL, 4);
        t[0] = (char)('1' + k);
        t[1] = 0;
        cv_text(6, 3, &FONT_T, t, K_DIM);
        cv_text(16, 3, &FONT_T, k & 1 ? "AMT" : k ? "DEST B" : "DEST A", K_DIM);
        if (k & 1) {
            int32_t a = (int32_t)(amt * 100.0f + (amt < 0 ? -0.5f : 0.5f));
            t[0] = 0;
            if (a >= 0)
                cat(t, "+");
            int_text(a, t + (a >= 0));
            cv_text(6, 17, &FONT_B, t, amt != 0.0f ? K_LFO : K_DIM);
            {
                int32_t e = 56 + (int32_t)(amt * 48.0f);
                rrect(8, 52, 96, 4, K_FAINT, 2);
                cv_rect(e < 56 ? e : 56, 52, e < 56 ? 56 - e : e - 56, 4, src_colour(13 + l));
                rrect(e - 2, 48, 5, 12, K_FG, 2);
            }
        } else {
            dst_name(d, t);
            cv_text(6, 17, text_w(&FONT_B, t) > 100 ? &FONT_XS : &FONT_B, t, d ? K_FG : K_DIM);
        }
        cv_blit((uint32_t)x, (uint32_t)y);
    }
}

/* a module, AMP, LFO or OSC page's four dials (also ~30 times a second with the well: modulation moves them) */
static void param_cells(int pg)
{
    const ui_mod_t *m = page_mod(pg);
    int slot = page_slot(pg), i, off;
    uint16_t c = page_colour(pg);
    float *live = 0;
    if (!m)
        return;
    if (slot >= 0 && slot != RS_LFO)
        live = rk_monitor(slot);
    off = page_first(m, mn.sub);
    for (i = 0; i < 4; i++) {
        int pi = off + i, ei = pofs(pg) + pi;   /* (OSC: AMP's 24-30) */
        const ui_param_t *p = i < m->pages[mn.sub] ? &m->p[pi] : 0;
        float v = p ? rk_get_param(slot, ei) : 0.0f, lv = v;
        uint16_t mc = 0;
        if (p && live && p->mod) {
            lv = ei < 16 ? live[ei] : live[168 + ei - 16];
            mc = mod_colour(slot, ei);
        }
        if (picture_first(pg))
            gauge(i, p, v, lv, mc);
        else
            cell(i, p, v, lv, mc, c);
    }
}


static void edited(void);
/* ---------------------------------------------------------- a module's MOD page (12 Nov 2026) */
/* the matrix rows (of the 12) that move this page's module, in row order: AMP's page takes AMP's own parameters,
 * OSC's the oscillator's (both live on AMP's slot) */
static int mod_rows(int pg, int *rows)
{
    int r, n = 0, src, via, slot = page_slot(pg);
    float amt;
    for (r = 0; r < 12; r++) {
        uint16_t d = route_dst(r, &src, &via, &amt);
        int ds = d ? (int)((d >> 8) & 0x7F) - 1 : -2, pi = d & 0xFF;
        if (!d || (d & 0x8000u) || ds != slot)
            continue;
        if (pg == PG_OSC ? pi < OSC_P0 : pg == PG_AMP ? pi >= OSC_P0 : 0)
            continue;
        rows[n++] = r;
    }
    return n;
}
/* the next of this page's parameters the matrix can move (K2 on the MOD page: only its own module's) */
static uint16_t mod_dst_step(int pg, uint16_t d, int k)
{
    const ui_mod_t *m = page_mod(pg);
    int slot = page_slot(pg), i, n = 0, at = -1;
    uint16_t list[32];
    for (i = 0; m && i < m->nparams && n < 32; i++)
        if (m->p[i].mod) {
            list[n] = (uint16_t)((slot + 1) << 8 | (pofs(pg) + i));
            if (list[n] == d) at = n;
            n++;
        }
    if (!n)
        return d;
    if (at < 0)
        return list[k > 0 ? 0 : n - 1];
    return list[(at + (k > 0 ? 1 : -1) + n) % n];
}
/* K1 source, K2 target, K3 amount, K4 via, on the row SELECT is on; a free row takes the matrix's first free one */
static void mod_knob(int pg, int k, int32_t e)
{
    const ui_mod_t *m = page_mod(pg);
    int rows[12], n = mod_rows(pg, rows), sel = mn.sub - m->npages, r, src, via, i;
    float amt;
    uint16_t d;
    if (sel < n) {
        r = rows[sel];
        d = route_dst(r, &src, &via, &amt);
    } else {
        for (r = 0; r < 12; r++)
            if (!route_dst(r, &src, &via, &amt))
                break;
        if (r == 12) {
            app_say("MATRIX FULL", "FREE A ROW");
            return;
        }
        src = learn_source();
        via = 0;
        amt = 0.0f;
        d = mod_dst_step(pg, 0, 1);
        if (!d) {
            app_say(m->name, "NOTHING TO MODULATE");
            return;
        }
    }
    if (k == 0) { src = src_step(src, (int)e); if (!src) src = src_step(src, (int)e); }
    if (k == 1) d = mod_dst_step(pg, d, (int)e);
    if (k == 2) amt = fm_clampf(amt + (float)e * 0.01f, -1.0f, 1.0f);
    if (k == 3) via = src_step(via, (int)e);
    route_set(r, src, d, amt, via);
    n = mod_rows(pg, rows);                   /* a new row may sort above the others: SELECT follows it */
    for (i = 0; i < n; i++)
        if (rows[i] == r && i < MOD_ROWS)
            mn.sub = m->npages + i;
    edited();
}
static void draw_mod_page(int pg)
{
    const ui_mod_t *m = page_mod(pg);
    int rows[12], n = mod_rows(pg, rows), sel = mn.sub - m->npages, k;
    uint16_t c = page_colour(pg);
    char t[32];
    cv_begin(240, 16, K_BG);
    cv_text(8, 2, &FONT_T, "SOURCE", K_DIM);
    cv_text(76, 2, &FONT_T, "TARGET", K_DIM);
    cv_text(150, 2, &FONT_T, "AMOUNT", K_DIM);
    cv_text(204, 2, &FONT_T, "VIA", K_DIM);
    cv_blit(0, 96);
    for (k = 0; k < MOD_ROWS; k++) {
        int on = k < n, src = 0, via = 0, isel = k == sel;
        float amt = 0.0f;
        uint16_t d = on ? route_dst(rows[k], &src, &via, &amt) : 0;
        cv_begin(232, 25, K_BG);
        rrect(0, 0, 232, 25, isel ? K_SEL : K_CELL, 4);
        if (isel)
            cv_rect(0, 0, 2, 25, c);
        if (on) {
            int32_t a = (int32_t)(amt * 100.0f + (amt < 0 ? -0.5f : 0.5f)), z = 172, ex = z + (int32_t)(amt * 24.0f);
            rrect(5, 11, 6, 6, src_colour(src), 3);
            cv_text(14, 7, &FONT_T, src_name(src), src_colour(src));
            dst_name(d, t);
            {                                 /* the parameter alone: the slot is this page's */
                const char *p = t;
                while (*p && *p != '.') p++;
                cv_text(72, 7, &FONT_T, *p ? p + 1 : t, K_FG);
            }
            rrect(148, 13, 48, 3, K_FAINT, 1);
            if (ex != z) cv_rect(ex < z ? ex : z, 13, ex < z ? z - ex : ex - z, 3, src_colour(src));
            t[0] = 0;
            if (a >= 0) cat(t, "+");
            int_text(a, t + (a >= 0));
            cv_text(172 - text_w(&FONT_T, t) / 2, 0, &FONT_T, t, K_FG);
            cv_text(204, 7, &FONT_T, via ? src_name(via) : "-", K_DIM);
        } else {
            cv_text(14, 7, &FONT_T, isel ? "FREE: TURN K1-K4 TO ADD" : "-", isel ? K_DIM : K_FAINT);
        }
        cv_blit(4, 112 + (uint32_t)k * 27);
    }
    if (n > MOD_ROWS) {
        int_text(n - MOD_ROWS, t);
        cat(t, " MORE IN THE MATRIX");
        help(t);
    } else
        help("SELECT: ROW  K1 SRC K2 TARGET K3 AMT K4 VIA");
}

static void draw_param_page(int pg)
{
    const ui_mod_t *m = page_mod(pg);
    int slot = page_slot(pg), i;
    uint16_t c = page_colour(pg);
    char title[24];
    if (!m) {                                 /* an empty slot */
        header(SLOT_TAG[slot], c, "EMPTY", IC_EMPTY, 1, 0);
        well(pg, 0, c);
        for (i = 0; i < 4; i++)
            cell(i, 0, 0.0f, 0.0f, 0, c);
        help("ALGORITHM: PICK A MODULE");
        return;
    }
    if (mn.sub >= npages(pg))
        mn.sub = npages(pg) - 1;
    title[0] = 0;
    if (is_fixed(pg)) {
        static const char *const GT[] = {"SETTINGS", "PLAYING", "TUNING", "MIDI", "MASTER", "SYSTEM", "CLICK", "DISPLAY"};
        static const char *const ST[] = {"SCALE + CHORD", "KEYS + STRUM", "STYLE", "LAYER MIDI CH", "CHORD + ARP", "FEEL",
                                         "RHYTHM", "RHYTHM CHANCE"},
                          *const AT[] = {"ARPEGGIATOR", "HOLD + TURING"};
        int ic = pg == PG_GLO ? (mn.sub == 3 ? IC_MIDI : mn.sub == 4 ? IC_OUT : IC_GLOBAL) : pg == PG_SEL ? (mn.sub ? IC_SCALE : IC_CHORD) : IC_ARP;
        header(pg == PG_GLO ? "GLO" : pg == PG_SEL ? "SEL" : "ARP", c, pg == PG_GLO ? GT[mn.sub % 8] : pg == PG_SEL ? ST[mn.sub % 8] : AT[mn.sub & 1],
               ic, npages(pg), mn.sub);
        page_well(pg);
        if (pg == PG_GLO && mn.sub == 5)
            system_cells(c);
        else
            fixed_controls(pg);
        help(pg == PG_GLO && mn.sub == 5 ? "SELECT: PAGE   (MEASURED LIVE)" : rhy_pi(pg, mn.sub, 0) >= 0 ? "K1: GENERATOR   K2-K4: ITS VALUES" : pg == PG_ARP ? "SELECT: PAGE   K1-K4: VALUES"
             : "SELECT: PAGE   K1-K4: VALUES");
        return;
    }
    if (pg == PG_LFO && mn.sub >= m->npages) {
        cat(title, "LFO ");
        title[4] = (char)('1' + mn.sub - m->npages);
        title[5] = 0;
        cat(title, " TARGET");
    } else {
        cat(title, m->name);
    }
    if (is_mod_sub(pg)) {
        title[0] = 0;
        cat(title, pg == PG_OSC ? "OSC" : m->name);
        cat(title, " MOD");
    }
    {
        int np = npages(pg), sb = mn.sub;
        if (has_mod_page(pg)) { np -= MOD_ROWS - 1; if (sb > m->npages) sb = m->npages; }   /* the MOD page: one dot */
        header(pg == PG_OSC ? "OSC" : SLOT_TAG[slot], c, is_mod_sub(pg) ? title : pg == PG_OSC ? (mn.sub ? "MIXER" : "OSCILLATOR") : title,
               pg == PG_AMP ? IC_AMP : pg == PG_LFO ? IC_LFO : pg == PG_OSC ? IC_KNOB : module_icon(m->id), np, sb);
    }
    page_well(pg);
    if (pg == PG_LFO && mn.sub >= m->npages) {
        draw_lfo_targets(mn.sub - m->npages);
        help("K1 K3: DESTINATION   K2 K4: AMOUNT");
        return;
    }
    if (is_mod_sub(pg)) {
        draw_mod_page(pg);
        return;
    }
    param_cells(pg);
    help(pg == PG_OSC ? "SELECT: PAGE   K1-K4: VALUES   EDIT: SOURCE" : pg == PG_AMP || pg == PG_LFO ? "SELECT: PAGE   K1-K4: VALUES"
         : "SELECT: PAGE   K1-K4: VALUES   ALGO: MODULE");
}

/* a fixed page's four controls (GLOBAL, SEL, ARP: their values in set.*), also each frame while the sound moves */
static void fixed_controls(int pg)
{
    const ui_mod_t *m = page_mod(data_pg(pg));
    float *fv = page_vals(data_pg(pg));
    int i, off;
    uint16_t c = page_colour(pg);
    if (!m || mn.sub >= npages(pg) || (pg == PG_GLO && mn.sub == 5))
        return;
    off = page_first(m, mn.sub);
    for (i = 0; i < 4; i++) {
        int ri = rhy_pi(pg, mn.sub, i), pi = ri == -2 ? off + i : ri;
        const ui_param_t *p = ri == -2 ? (i < m->pages[mn.sub] ? &m->p[off + i] : 0) : ri >= 0 ? &m->p[ri] : 0;
        if (picture_first(pg))
            gauge(i, p, p ? fv[pi] : 0.0f, p ? fv[pi] : 0.0f, 0);
        else
            cell(i, p, p ? fv[pi] : 0.0f, 0.0f, 0, c);
    }
}

static void draw_picker(void)
{
    int slot = page_slot(mn.page), n, i;
    const uint8_t *ch;
    uint16_t c = slot_colour(slot);
    char t[40];
    choices(slot, &ch, &n);
    header(SLOT_TAG[slot], c, "LOAD MODULE", module_icon(ch[mn.pick_ix]), 1, 0);
    for (i = 0; i < 4; i++) {
        cv_begin(228, 30, K_BG);
        if (i < n) {
            const ui_mod_t *m = ui_mod(ch[i]);
            int sel = i == mn.pick_ix, here = ch[i] == rc_patch.mod[slot];
            if (sel) {
                rrect(0, 0, 228, 28, K_SEL, 4);
                cv_rect(0, 0, 2, 28, c);
            }
            icon(m ? module_icon(m->id) : IC_EMPTY, 8, 8, sel ? c : K_DIM);
            cv_text(26, 6, &FONT_B, m ? m->name : "EMPTY", K_FG);
            if (here)
                cv_text(222 - text_w(&FONT_T, "LOADED"), 9, &FONT_T, "LOADED", K_OK);
        }
        cv_blit(6, 36 + (uint32_t)i * 31);
    }
    cv_begin(240, 52, K_BG);
    {
        const ui_mod_t *m = ui_mod(ch[mn.pick_ix]);
        const char *b = m ? m->blurb : "SLOT LEFT EMPTY";
        cv_text(120 - text_w(&FONT_XS, b) / 2, 4, &FONT_XS, b, K_FG);
        t[0] = 0;
        cat(t, "PRESS ");
        cat(t, slot == RS_SRC ? "EDIT" : slot == RS_SHP ? "ENV" : slot <= RS_MODB ? "LFO" : "FX");
        cat(t, " TO LOAD   HOME: BACK");
        cv_text(120 - text_w(&FONT_T, t) / 2, 26, &FONT_T, t, K_DIM);
    }
    cv_blit(0, 162);
}

/* the matrix hub: ALGORITHM turns between ROUTES (the 12 rows) and TARGETS (12 Nov 2026: the same rows sorted by
 * what they move, so "what moves the cutoff?" reads off one place; two routes into one parameter sit together).
 * The parameters' own tables (ENV, EDIT, OSC) gave way to each module's MOD page */
static void edited(void);
#define NHUB 2
static const char *const HUB_NAME[NHUB] = {"ROUTES", "TARGETS"};
static int hub_sorted(int *rows)              /* the rows in use, by destination then row */
{
    int r, n = 0, i, j, src, via;
    float amt;
    uint16_t key[12];
    for (r = 0; r < 12; r++) {
        uint16_t d = route_dst(r, &src, &via, &amt);
        if (!d || !src)
            continue;
        key[n] = (uint16_t)(d & 0x7FFFu);
        rows[n++] = r;
    }
    for (i = 1; i < n; i++)                   /* insertion sort: stable, so rows into one target keep their order */
        for (j = i; j > 0 && key[j - 1] > key[j]; j--) {
            uint16_t tk = key[j]; int tr = rows[j];
            key[j] = key[j - 1]; rows[j] = rows[j - 1];
            key[j - 1] = tk; rows[j - 1] = tr;
        }
    return n;
}
static void draw_hub(void)
{
    int rows[12], n = hub_sorted(rows), k, top, sel = mn.hrow[1], lastslot = -99;
    char t[32];
    if (sel >= n) sel = mn.hrow[1] = n ? n - 1 : 0;
    header("MATRIX", K_MX, "BY TARGET", IC_MATRIX, NHUB, 1);
    cv_begin(240, 16, K_BG);
    cv_text(8, 2, &FONT_T, "TARGET", K_DIM);
    cv_text(120, 2, &FONT_T, "FROM", K_DIM);
    cv_text(196, 2, &FONT_T, "AMT", K_DIM);
    cv_blit(0, 34);
    top = sel - 3;
    if (top > n - 7) top = n - 7;
    if (top < 0) top = 0;
    if (top > 0) {                            /* the slot heading of the first row shown */
        int src, via; float amt;
        uint16_t d = route_dst(rows[top - 1], &src, &via, &amt);
        lastslot = (int)((d >> 8) & 0x7F) - 1;
    }
    for (k = 0; k < 7; k++) {
        int i = top + k, src, via;
        float amt;
        cv_begin(232, 22, K_BG);
        if (i < n) {
            uint16_t d = route_dst(rows[i], &src, &via, &amt);
            int slot = (int)((d >> 8) & 0x7F) - 1, isel = i == sel, same = slot == lastslot && !(d & 0x8000u);
            int32_t a = (int32_t)(amt * 100.0f + (amt < 0 ? -0.5f : 0.5f));
            if (isel) {
                rrect(0, 0, 232, 22, K_SEL, 4);
                cv_rect(0, 0, 2, 22, K_MX);
            }
            dst_name(d, t);
            if (same) {                       /* under its slot's first row: the parameter alone, indented */
                const char *p = t;
                while (*p && *p != '.') p++;
                cv_text(20, 4, &FONT_T, *p ? p + 1 : t, K_FG);
            } else
                cv_text(8, 4, &FONT_T, t, K_FG);
            rrect(110, 9, 5, 5, src_colour(src), 2);
            cv_text(118, 4, &FONT_T, src_name(src), src_colour(src));
            t[0] = 0;
            if (a >= 0) cat(t, "+");
            int_text(a, t + (a >= 0));
            cv_text(222 - text_w(&FONT_T, t), 4, &FONT_T, t, K_FG);
            lastslot = (d & 0x8000u) ? -99 : slot;
        } else if (i == 0) {
            cv_text(8, 4, &FONT_T, "NO ROUTES YET", K_DIM);
        }
        cv_blit(4, 50 + (uint32_t)k * 23);
    }
    help("ALGO: TABLE  SELECT: ROW  K1 SRC K2 DEST K3 AMT");
}
/* the row SELECT is on in the matrix: ROUTES' row, or TARGETS' sorted row (-1 none) */
static int hub_row(void)
{
    int rows[12], n;
    if (!mn.mtab)
        return mn.row;
    n = hub_sorted(rows);
    return mn.hrow[1] < n ? rows[mn.hrow[1]] : -1;
}

static void draw_matrix(void)
{
    int k, top = mn.row - 2;
    if (mn.mtab) {
        draw_hub();
        return;
    }
    char t[32];
    if (top < 0) top = 0;
    if (top > 6) top = 6;
    t[0] = 0;
    cat(t, "ROW ");
    int_text(mn.row + 1, t + 4);
    cat(t, "/12");
    header("MATRIX", K_MX, t, IC_MATRIX, NHUB, 0);
    cv_begin(240, 16, K_BG);
    cv_text(8, 2, &FONT_T, "SOURCE", K_DIM);
    cv_text(72, 2, &FONT_T, "DESTINATION", K_DIM);
    cv_text(158, 2, &FONT_T, "AMT", K_DIM);
    cv_text(196, 2, &FONT_T, "VIA", K_DIM);
    cv_blit(0, 34);
    for (k = 0; k < 6; k++) {
        int i = top + k, src, via, sel = i == mn.row;
        float amt;
        uint16_t d = route_dst(i, &src, &via, &amt);
        int on = src && d;
        uint16_t c = on ? K_FG : K_DIM;
        int32_t a = (int32_t)(amt * 100.0f + (amt < 0 ? -0.5f : 0.5f));
        cv_begin(232, 22, K_BG);
        if (sel) {
            rrect(0, 0, 232, 22, K_SEL, 4);
            cv_rect(0, 0, 2, 22, K_MX);
        }
        if (src) rrect(5, 8, 5, 5, on ? src_colour(src) : mix565(src_colour(src), K_BG, 100), 2);   /* its kind's colour */
        cv_text(13, 4, &FONT_T, src_name(src), on ? src_colour(src) : c);
        dst_name(d, t);
        cv_text(68, 4, &FONT_T, t, c);
        t[0] = 0;
        if (a >= 0)
            cat(t, "+");
        int_text(a, t + (a >= 0));
        cv_text(176 - text_w(&FONT_T, t), 4, &FONT_T, t, c);
        cv_text(192, 4, &FONT_T, via ? src_name(via) : "-", K_DIM);
        cv_blit(4, 50 + (uint32_t)k * 24);
    }
    help("ALGO: TABLE  K1 SRC K2 DEST K3 AMT K4 VIA");
}

static void rack_tile(int32_t x, const char *tag, int slot, uint16_t c, const char *fixed, int ic)
{
    const ui_mod_t *m = slot >= 0 ? ui_mod(rc_patch.mod[slot]) : 0;
    const char *name = slot >= 0 ? (m ? m->name : 0) : fixed;
    int lost = slot >= 0 && (rk_fm1_status(4) >> slot & 1);
    rrect(x, 0, 72, 36, name ? GREY(18) : K_WELL, 4);
    icon(slot >= 0 ? (m ? module_icon(m->id) : IC_EMPTY) : ic, x + 5, 5, lost ? K_WARN : name ? c : K_FAINT);
    cv_text(x + 21, 4, &FONT_T, tag, c);
    cv_text(x + 5, 20, &FONT_T, name ? name : "--", lost ? K_WARN : name ? K_FG : K_DIM);
}

static void draw_rack(void)
{
    char t[24];
    int i, routes = 0, src, via;
    float amt;
    entry_name(cur_entry, t);
    header("RACK", K_FG, t, IC_RACK, 1, 0);
    for (i = 0; i < 12; i++)
        if (route_dst(i, &src, &via, &amt))
            routes++;
    cv_begin(240, 14, K_BG);
    cv_text(8, 0, &FONT_T, "PER VOICE", K_DIM);
    cv_blit(0, 34);
    t[0] = 0;
    cat(t, "AMP ");
    int_text(rc_patch.voices_now, t + 4);
    cat(t, "V");
    cv_begin(240, 40, K_BG);
    rack_tile(6, "SRC", RS_SRC, K_SRC, 0, 0);
    rack_tile(84, "SHP", RS_SHP, K_SHP, 0, 0);
    rack_tile(162, t, -1, K_AMP, "ADSR", IC_AMP);
    cv_blit(0, 48);
    cv_begin(240, 14, K_BG);
    cv_text(8, 0, &FONT_T, "SHARED", K_DIM);
    cv_blit(0, 90);
    cv_begin(240, 40, K_BG);
    rack_tile(6, "FX 1", RS_FX1, K_FX, 0, 0);
    rack_tile(84, "FX 2", RS_FX2, K_FX, 0, 0);
    rack_tile(162, "OUT", -1, K_OK, "MASTER", IC_OUT);
    cv_blit(0, 104);
    cv_begin(240, 14, K_BG);
    cv_text(8, 0, &FONT_T, "MODULATION", K_DIM);
    cv_blit(0, 146);
    cv_begin(240, 40, K_BG);
    t[0] = 0;
    int_text(routes, t);
    cat(t, " ROUTES");
    rack_tile(6, "MOD A", RS_MODA, K_MOD, 0, 0);
    rack_tile(84, "MOD B", RS_MODB, K_MOD, 0, 0);
    rack_tile(162, "MATRIX", -1, K_MX, t, IC_MATRIX);
    cv_blit(0, 160);
    help("EDIT ENV LFO FX ARP: PAGES   HOME: BACK");
}

/* ---------------------------------------------------------- the controls */
static int is_slot_page(int pg) { return pg >= PG_SRC && pg <= PG_FX2; }

static void go(int pg)
{
    if (mn.page != pg)
        mn.sub = 0;
    mn.page = pg;
    mn.pick = 0;
    ui.redraw = 1;
}

/* load the picker's choice into the page's slot (the audio stands aside meanwhile) */
static void load_module(void)
{
    int slot = page_slot(mn.page), n, i, other;
    const uint8_t *ch;
    const ui_mod_t *m;
    float vals[32];
    choices(slot, &ch, &n);
    other = slot == RS_FX1 ? RS_FX2 : slot == RS_FX2 ? RS_FX1 : -1;
    if (other >= 0 && ch[mn.pick_ix] && rc_patch.mod[other] == ch[mn.pick_ix]) {
        app_say(ui_mod(ch[mn.pick_ix])->name, other == RS_FX1 ? "IS IN FX 1" : "IS IN FX 2");
        return;
    }
    m = ui_mod(ch[mn.pick_ix]);
    for (i = 0; m && i < m->nparams && i < 32; i++)
        vals[i] = m->p[i].def;
    rc_fade_pause();
    rc_swap(slot, ch[mn.pick_ix], vals, m ? m->nparams : 0);
    rc_resume();
    mn.pick = 0;
    mn.sub = 0;
    if (m && (rk_fm1_status(4) >> slot & 1))
        app_say(m->name, "NO ROOM");
    else {
        char t[8];
        int_text(rk_fm1_status(2), t);
        cat(t, " V");
        app_say(m ? m->name : "EMPTIED", m ? t : SLOT_TAG[slot]);
    }
}

/* a page button: the pages it cycles through, as the page's resolve(); a second press on the picker's slot loads */
static void page_button(const uint8_t *cyc, int n)
{
    int i, at = -1;
    if (mn.pick)
        for (i = 0; i < n; i++)
            if (cyc[i] == mn.page) {
                load_module();
                ui.redraw = 1;
                return;
            }
    for (i = 0; i < n; i++)
        if (cyc[i] == mn.page)
            at = i;
    if (at >= 0) {                            /* (update 51, after Josh: a menu button pressed again turns its pages)
                                                 on one of its pages: the next page dot, then the next of its pages */
        const ui_mod_t *m = page_mod(mn.page);
        int last = npages(mn.page) - 1;
        if (has_mod_page(mn.page) && m)
            last = m->npages;                 /* (the MOD page: one stop, not its four rows) */
        if (mn.sub < last) {
            mn.sub = is_mod_sub(mn.page) ? last : mn.sub + 1;
            ui.redraw = 1;
            return;
        }
        if (n == 1) {                         /* its one page: round to the first dot */
            mn.sub = 0;
            ui.redraw = 1;
            return;
        }
    }
    go(cyc[at < 0 ? 0 : (at + 1) % n]);
}

static void edited(void)
{
    mn.edited = 1;
    mn.edited_at = plat_ms();
    ui.redraw = 1;
}

/* a setting on SEL, GLO or ARP changed: what follows from it, then saved when the knobs rest */
/* DAYLIGHT on or off (update 55): the palette, then the whole screen drawn again */
static void display_set(int day)
{
    set.glo[G_DISP] = day ? 1.0f : 0.0f;
    pal_set(day);
    ui.clear = 1;
    ui.redraw = 1;
    settings_touch();
}
static void fixed_changed(int pg, int pi, float old, float v)
{
    if (pg == PG_GLO) {
        if (pi == G_DISP) {
            display_set(v > 0.5f);
        } else if (pi == G_TEMPO) {
            rk_set_param(RS_GLO, 0, v);
            edited();                         /* a delay synced to the tempo may need a longer buffer */
        } else if (pi == G_VOICES) {
            set.voices = (uint8_t)GI(G_VOICES);
            if (rc_patch.mode == RVM_POLY)
                rc_voices(set.voices ? set.voices : rc_patch.voices > START_VOICES ? START_VOICES : rc_patch.voices);
        } else if (pi == G_TUNE || pi == G_TROOT || pi == G_A4) {
            tune_apply();
        } else if (pi >= G_LOW) {
            master_apply();
        }
    } else if (pg == PG_ARP && (pi == A_HOLD || (pi >= A_STYLE && pi <= A_CLEN) || (pi == A_MODE && ((old < 0.5f) != (v < 0.5f))))) {
        rc_push(PLE_ARP_CLEAR, 0, 0.0f, 0.0f);   /* the arp on or off, HOLD or a STYLE setting changed: start again
                                                  * from the keys held now (the layers' notes end first) */
    } else if (pg == PG_ARP && pi >= A_CHCH && pi <= A_SAR) {   /* a layer's channel changed, or a switch turned off: its notes end */
        if (pi <= A_ARCH)
            rc_push(PLE_LAYER, pi - A_CHCH, 2.0f, 0.0f);
        else if (v < 0.5f)
            rc_push(PLE_LAYER, pi == A_LBS ? L_BASS : pi <= A_SCH ? L_CHORD : L_ARP,
                    pi == A_SCH || pi == A_SAR ? 2.0f : 1.0f, 0.0f);
    }
    settings_touch();
}

/* ---------------------------------------------------------- LEARN */
static void learn_set(int on)
{
    mn.learn = on;
    app_say(on ? "LEARN" : "LEARN DONE", on ? src_name(learn_source()) : "");
    ui.redraw = 1;
}
static int learnable(int pg)
{
    if (pg == PG_LFO)
        return mn.sub < UI_LFO.npages;        /* not the LFO target pages */
    return (pg == PG_AMP || pg == PG_OSC || (is_slot_page(pg) && page_mod(pg))) && !is_mod_sub(pg);
}
/* a knob turned while learning: the macro linked to its parameter, the turn its amount */
static void learn_turn(int k, int32_t e)
{
    const ui_mod_t *m = page_mod(mn.page);
    int slot = page_slot(mn.page), pi, r, row = -1, free_row = -1, src, via, msrc = learn_source();
    float amt;
    uint16_t d;
    char t[40], u[24];
    if (!m || mn.sub >= m->npages || k >= m->pages[mn.sub])
        return;
    pi = page_first(m, mn.sub) + k;
    if (!m->p[pi].mod) {
        app_say(m->p[pi].l, "CAN'T BE MODULATED");
        return;
    }
    d = (uint16_t)((slot + 1) << 8 | (pofs(mn.page) + pi));
    for (r = 0; r < 12; r++) {
        uint16_t rd = route_dst(r, &src, &via, &amt);
        if (rd == d && src == msrc && row < 0)
            row = r;
        if (!rd && free_row < 0)
            free_row = r;
    }
    if (row < 0) {
        if (free_row < 0) {
            app_say("MATRIX FULL", "FREE A ROW");
            return;
        }
        row = free_row;
        route_set(row, msrc, d, 0.0f, 0);
    }
    route_dst(row, &src, &via, &amt);
    amt = fm_clampf(amt + (float)e * 0.02f, -1.0f, 1.0f);
    route_set(row, msrc, d, amt, via);
    t[0] = 0;
    cat(t, src_name(msrc));
    cat(t, ">");
    dst_name(d, u);
    cat(t, u);
    u[0] = 0;
    {
        int32_t a = (int32_t)(amt * 100.0f + (amt < 0.0f ? -0.5f : 0.5f));
        if (a >= 0) cat(u, "+");
        int_text(a, u + (a >= 0));
    }
    app_say(t, u);
    edited();
}

/* K1-K4 on a page */
static void knob(int k, int32_t e)
{
    const ui_mod_t *m = page_mod(mn.page);
    int slot = page_slot(mn.page);
    if (mn.page == PG_MATRIX) {               /* ROUTES or TARGETS: the row SELECT is on */
        int src, via, row = hub_row();
        float amt;
        uint16_t d;
        if (row < 0)
            return;
        d = route_dst(row, &src, &via, &amt);
        if (k == 0) src = src_step(src, (int)e);
        if (k == 1) d = dst_step(d, (int)e);
        if (k == 2) { amt += (float)e * 0.01f; amt = amt < -1.0f ? -1.0f : amt > 1.0f ? 1.0f : amt; }
        if (k == 3) via = src_step(via, (int)e);
        if (!src && k != 0 && d)
            src = 1;
        route_set(row, src, d, amt, via);
        edited();
        return;
    }
    if (!m)
        return;
    if (is_fixed(mn.page)) {
        int dp = data_pg(mn.page), pi;
        float *fv = page_vals(dp), old;
        m = page_mod(dp);
        pi = rhy_pi(mn.page, mn.sub, k);
        if (pi == -1)
            return;
        if (pi == -2) {
            pi = page_first(m, mn.sub) + k;
            if (k >= m->pages[mn.sub])
                return;
        }
        old = fv[pi];
        undo_note(UN_FIXED, dp, pi, old, 0, 0, 0);
        fv[pi] = step(&m->p[pi], old, e);
        if (pi >= A_R0 && (pi - A_R0) % 6 == RF_MODE) {   /* BASS: STYLE or RING; CHORD, ARP: no STYLE */
            int g = (pi - A_R0) / 6, v = (int)(fv[pi] + 0.5f);
            if (g == 0) v = v < RM_STYLE ? RM_STYLE : v > RM_RING ? RM_RING : v;
            else if (v == RM_STYLE) v = old < 0.5f ? RM_RING : RM_OFF;
            fv[pi] = (float)v;
        }
        if (fv[pi] != old)
            fixed_changed(dp, pi, old, fv[pi]);
        ui.redraw = 1;
        return;
    }
    if (is_mod_sub(mn.page)) {
        mod_knob(mn.page, k, e);
        return;
    }
    if (mn.page == PG_LFO && mn.sub >= m->npages) {
        int l = mn.sub - m->npages, r = 12 + l * 2 + (k >> 1), src, via;
        float amt;
        uint16_t d = route_dst(r, &src, &via, &amt);
        if (k & 1) { amt += (float)e * 0.01f; amt = amt < -1.0f ? -1.0f : amt > 1.0f ? 1.0f : amt; }
        else d = dst_step(d, (int)e);
        route_set(r, 13 + l, d, amt, 0);
        edited();
        return;
    }
    {
        int pi = page_first(m, mn.sub) + k;
        const ui_param_t *p;
        float v;
        if (k >= m->pages[mn.sub])
            return;
        p = &m->p[pi];
        v = step(p, rk_get_param(slot, pofs(mn.page) + pi), e);
        if (slot == RS_SHP && rc_patch.mod[RS_SHP] == 10 && pi == 0) {   /* Rings: only STRING, FM VOICE, STR+VERB */
            static const uint8_t RM[3] = {2, 3, 5};
            int cur = (int)(rk_get_param(slot, 0) + 0.5f), j = cur == 5 ? 2 : cur == 3 ? 1 : 0;
            j += e > 0 ? 1 : -1;
            v = (float)RM[j < 0 ? 0 : j > 2 ? 2 : j];
        }
        undo_note(UN_PARAM, slot, pofs(mn.page) + pi, rk_get_param(slot, pofs(mn.page) + pi), 0, 0, 0);
        if (p->t == UT_NUM || p->t == UT_BIP)   /* continuous: glides; a list or a number of steps: at once */
            rc_set_glide(slot, pofs(mn.page) + pi, v);
        else
            rc_set(slot, pofs(mn.page) + pi, v);
        if (mn.page == PG_AMP && pi == 11) {  /* MODE: the voices follow */
            int md = (int)(v + 0.5f);
            rc_patch.mode = md;
            rk_set_legato(md == RVM_MONO ? 0 : 1);
            rc_voices(md == RVM_MONO || md == RVM_LEGATO ? 1 : START_VOICES);
        }
        edited();
    }
}

/* EDIT held: the last change put back */
static void undo_last(void)
{
    undo_t *u;
    char t[32];
    while (undo_n && undo_j[undo_n - 1].gen != rc_gen)
        undo_n--;
    if (!undo_n) {
        app_say("UNDO", "NOTHING TO UNDO");
        return;
    }
    u = &undo_j[--undo_n];
    undo_replay = 1;
    if (u->kind == UN_PARAM) {
        uint16_t d = (uint16_t)((u->a + 1) << 8 | u->b);
        rc_set(u->a, u->b, u->v);
        if (u->a == RS_AMP && u->b == 11) {   /* AMP MODE: the voices follow */
            int md = (int)(u->v + 0.5f);
            rc_patch.mode = md;
            rk_set_legato(md == RVM_MONO ? 0 : 1);
            rc_voices(md == RVM_MONO || md == RVM_LEGATO ? 1 : START_VOICES);
        }
        dst_name(d, t);
    } else if (u->kind == UN_ROUTE) {
        route_set(u->a, u->src, u->dst, u->v, u->via);
        t[0] = 0;
        cat(t, "MATRIX ROW ");
        int_text(u->a + 1, t + 11);
    } else {
        float *fv = page_vals(u->a), old = fv[u->b];
        fv[u->b] = u->v;
        fixed_changed(u->a, u->b, old, u->v);
        t[0] = 0;
        cat(t, page_mod(u->a)->p[u->b].l);
    }
    undo_replay = 0;
    app_say("UNDO", t);
    edited();
    ui.redraw = 1;
}

/* Two taps on SEL (update 38, as before update 36): chords off, the keys playing single notes, and two taps again back
 * to the chord type they had; as turning SEL's CHORD knob does (UNDO, saved with the settings) */
static void fixed_toggle(int pg, int pi, float *mem, const char *what)
{
    const ui_mod_t *m = page_mod(pg);
    float *fv = page_vals(pg), old = fv[pi];
    char t[32];
    undo_note(UN_FIXED, pg, pi, old, 0, 0, 0);
    if (old > 0.5f) {
        *mem = old;
        fv[pi] = 0.0f;
    } else {
        fv[pi] = *mem > 0.5f ? *mem : 1.0f;   /* never turned off here: the first choice after OFF */
    }
    fixed_changed(pg, pi, old, fv[pi]);
    t[0] = 0;
    cat(t, fv[pi] > 0.5f ? "ON: " : "OFF");
    if (fv[pi] > 0.5f)
        cat(t, UI_OPT[m->p[pi].opt + (int)(fv[pi] + 0.5f)]);
    app_say(what, t);
    ui.redraw = 1;
}
/* Two taps on ARP: the arp on the FM-1's sound on or off; three taps on SEL / ARP: the chords' / the arp's SEND to
 * their MIDI channel on or off (update 36). As turning the switch on ARP > CHORD + ARP does (UNDO, saved with the settings) */
static void layer_toggle(int pi, const char *title)
{
    float *fv = page_vals(PG_ARP), old = fv[pi];
    int send = pi == A_SCH || pi == A_SAR, ch = AI(pi == A_SCH ? A_CHCH : A_ARCH);
    char t[32], m[8];
    undo_note(UN_FIXED, PG_ARP, pi, old, 0, 0, 0);
    fv[pi] = old > 0.5f ? 0.0f : 1.0f;
    fixed_changed(PG_ARP, pi, old, fv[pi]);
    t[0] = 0;
    if (fv[pi] < 0.5f) cat(t, "OFF");
    else if (!send) cat(t, pi == A_LCH && GI(G_LOCAL) ? "ON (LOCAL IS OFF)" : "ON");
    else if (!GI(G_MOUT)) cat(t, "ON (MIDI OUT IS OFF)");
    else if (!ch) cat(t, "ON (ITS CH IS OFF)");
    else { cat(t, "ON: CH "); int_text(ch, m); cat(t, m); }
    app_say(title, t);
    ui.redraw = 1;
}
/* Hold FX, then one tap: FX 1 off or on, two taps: FX 2 (14 Nov 2026). The engine skips it (no CPU) and keeps its
 * settings; a preset load turns both on again. Not saved with a preset. */
#define FX_ARM_MS 2000u                       /* armed this long after FX is let go, waiting for the taps */
static void fx_toggle(int slot)
{
    char t[32];
    int off = !rk_fx_bypass(slot);
    t[0] = 0;
    cat(t, slot == RS_FX1 ? "FX 1 " : "FX 2 ");
    if (!rc_patch.mod[slot]) {
        cat(t, "IS EMPTY");
        app_say("FX ON/OFF", t);
        return;
    }
    rk_set_fx_bypass(slot, off);
    cat(t, module_name(rc_patch.mod[slot]));
    app_say(t, off ? "OFF" : "ON");
    ui.redraw = 1;
}

/* the buttons and encoders of the menus; 1 when they were the menus' (input() does the rest) */
static int menu_input(uint32_t press, uint32_t btn)
{
    static const uint8_t ENV_CYC[] = {PG_AMP, PG_SHP}, LFO_CYC[] = {PG_MODA, PG_MODB, PG_LFO, PG_MSEQ, PG_MATRIX}, FX_CYC[] = {PG_FX1, PG_FX2};
    static const uint8_t SRC_CYC[] = {PG_SRC, PG_OSC}, ARP_CYC[] = {PG_ARP}, SEL_CYC[] = {PG_SEL}, GLO_CYC[] = {PG_GLO};
    int32_t e;
    int k, both = (btn >> B_EDIT & 1u) && (btn >> B_SEL & 1u);
    /* SEL + EDIT held together (LEARN_MS): LEARN on or off. Neither key then taps, and EDIT's hold is no UNDO */
    if (both) {
        if (!mn.combo_at)
            mn.combo_at = ms_stamp();
        mn.edit_long = mn.sel_long = 1;
        mn.sel_tap = 0;
        mn.sel_n = 0;
        if (!mn.combo_done && plat_ms() - mn.combo_at >= LEARN_MS) {
            mn.combo_done = 1;
            learn_set(!mn.learn);
        }
    } else if (!(btn >> B_EDIT & 1u) && !(btn >> B_SEL & 1u)) {
        mn.combo_at = 0;
        mn.combo_done = 0;
    }
    if (press >> B_EDIT & 1u) {               /* EDIT (10 Nov 2026): a tap turns the page when let go; held, UNDO */
        mn.edit_at = ms_stamp();
        mn.edit_long = both;
    }
    if (mn.edit_at && (btn >> B_EDIT & 1u) && !mn.edit_long && plat_ms() - mn.edit_at >= HOLD_MS) {
        mn.edit_long = 1;
        undo_last();
    }
    if (mn.edit_at && !(btn >> B_EDIT & 1u)) {
        if (!mn.edit_long) {
            if (mn.page == PG_SEQ && mn.sub == 0)   /* (NOTES) */
                seq_delete();                 /* the sequencer's NOTES: EDIT deletes the note picked, or the lasso */
            else
                page_button(SRC_CYC, 2);
        }
        mn.edit_at = 0;
    }
    if (press >> B_ENV & 1u) page_button(ENV_CYC, 2);
    /* LFO (update 38): a tap turns the page at once; a second tap within 0.3 s: the MATRIX */
    if (press >> B_LFO & 1u) {
        if (mn.lfo_at && plat_ms() - mn.lfo_at < EDIT_DOUBLE_MS) {
            mn.lfo_at = 0;
            go(PG_MATRIX);
        } else {
            mn.lfo_at = ms_stamp();
            page_button(LFO_CYC, 5);
        }
    }
    /* FX (14 Nov 2026): a tap turns the page when let go; held, FX on/off is armed: then one tap FX 1, two taps FX 2 */
    if (press >> B_FX & 1u) {
        if (mn.fx_arm) {
            if (mn.fx_tap && plat_ms() - mn.fx_tap < EDIT_DOUBLE_MS) {
                mn.fx_tap = mn.fx_arm = 0;
                fx_toggle(RS_FX2);
            } else {
                mn.fx_tap = ms_stamp();
            }
        } else {
            mn.fx_at = ms_stamp();
            mn.fx_long = 0;
        }
    }
    if (mn.fx_at && (btn >> B_FX & 1u) && !mn.fx_long && plat_ms() - mn.fx_at >= HOLD_MS) {
        mn.fx_long = 1;
        mn.fx_arm = ms_stamp();
        app_say("FX ON/OFF", "TAP FX 1, TWO TAPS FX 2");
    }
    if (mn.fx_at && !(btn >> B_FX & 1u)) {
        if (!mn.fx_long)
            page_button(FX_CYC, 2);
        else
            mn.fx_arm = ms_stamp();       /* the wait for the taps starts when it's let go */
        mn.fx_at = 0;
    }
    if (mn.fx_tap && !(btn >> B_FX & 1u) && plat_ms() - mn.fx_tap >= EDIT_DOUBLE_MS) {
        mn.fx_tap = mn.fx_arm = 0;            /* one tap and no second: FX 1 */
        fx_toggle(RS_FX1);
    }
    if (mn.fx_arm && !mn.fx_at && !mn.fx_tap && plat_ms() - mn.fx_arm >= FX_ARM_MS) {
        mn.fx_arm = 0;
        app_say("FX ON/OFF", "NOTHING CHANGED");
    }
    /* ARP and SEL (update 38): a tap opens the page as soon as it's let go; two taps within 0.3 s of each other:
     * back to the page you were on, and SEL: chords off / on, ARP: the arp on the FM-1 off / on; three taps: their
     * SEND off / on. Held: never a tap (SEL held with EDIT: LEARN, above; with K1 on the sequencer: the lasso) */
    if (press >> B_ARP & 1u) {
        if (!(mn.arp_tap && plat_ms() - mn.arp_tap < EDIT_DOUBLE_MS))
            mn.arp_n = 0;
        mn.arp_tap = 0;
        mn.arp_at = ms_stamp();
        mn.arp_long = 0;
    }
    if (mn.arp_at && (btn >> B_ARP & 1u) && !mn.arp_long && plat_ms() - mn.arp_at >= HOLD_MS) {
        mn.arp_long = 1;
        mn.arp_n = 0;
    }
    if (mn.arp_at && !(btn >> B_ARP & 1u)) {
        if (!mn.arp_long) {
            if (++mn.arp_n == 1) {
                mn.tap_pg = mn.page;
                mn.tap_sub = mn.sub;
                page_button(ARP_CYC, 1);
            }
            mn.arp_tap = ms_stamp();
        }
        mn.arp_at = 0;
    }
    if (mn.arp_tap && !(btn >> B_ARP & 1u) && plat_ms() - mn.arp_tap >= EDIT_DOUBLE_MS) {
        int n = mn.arp_n;
        mn.arp_tap = 0;
        mn.arp_n = 0;
        if (n >= 2) {
            go(mn.tap_pg);
            mn.sub = mn.tap_sub;
            ui.clear = 1;
            if (n >= 3) layer_toggle(A_SAR, "ARP: SEND");
            else layer_toggle(A_LAR, "ARP: ON FM-1");
        }
    }
    if (press >> B_SEL & 1u) {
        if (both || !(mn.sel_tap && plat_ms() - mn.sel_tap < EDIT_DOUBLE_MS))
            mn.sel_n = 0;
        mn.sel_tap = 0;
        mn.sel_at = ms_stamp();
        mn.sel_long = both;
    }
    if (mn.sel_at && (btn >> B_SEL & 1u) && !mn.sel_long && plat_ms() - mn.sel_at >= HOLD_MS)
        mn.sel_long = 1;                      /* held: never a tap */
    if (mn.sel_at && !(btn >> B_SEL & 1u)) {
        if (!mn.sel_long) {
            if (mn.page == PG_SEQ && mn.sub == 0 && sp_lasso_off()) {
                mn.sel_n = 0;                 /* a tap of SEL on a lasso: the lasso goes (update 41), no page */
            } else {
                if (++mn.sel_n == 1) {
                    mn.tap_pg = mn.page;
                    mn.tap_sub = mn.sub;
                    page_button(SEL_CYC, 1);
                }
                mn.sel_tap = ms_stamp();      /* let go: another tap may still come */
            }
        } else {
            mn.sel_n = 0;
        }
        mn.sel_at = 0;
    }
    if (mn.sel_tap && !(btn >> B_SEL & 1u) && plat_ms() - mn.sel_tap >= EDIT_DOUBLE_MS) {
        int n = mn.sel_n;
        mn.sel_tap = 0;
        mn.sel_n = 0;
        if (n >= 2) {
            go(mn.tap_pg);
            mn.sub = mn.tap_sub;
            ui.clear = 1;
            if (n >= 3) layer_toggle(A_SCH, "CHORDS: SEND");
            else fixed_toggle(PG_SEL, C_TYPE, &mn.chord_mem, "CHORDS");
        }
    }
    /* GLO (update 55): a tap turns the page when let go; held, DAYLIGHT on or off */
    if (press >> B_GLO & 1u) {
        mn.glo_at = ms_stamp();
        mn.glo_long = 0;
    }
    if (mn.glo_at && (btn >> B_GLO & 1u) && !mn.glo_long && plat_ms() - mn.glo_at >= HOLD_MS) {
        mn.glo_long = 1;
        display_set(!pal_day);
        app_say("DISPLAY", pal_day ? "DAYLIGHT" : "NORMAL");
    }
    if (mn.glo_at && !(btn >> B_GLO & 1u)) {
        if (!mn.glo_long)
            page_button(GLO_CYC, 1);
        mn.glo_at = 0;
    }
    if (press >> B_HOME & 1u) {               /* HOME: a tap on letting go (LEARN is EDIT's double tap now) */
        mn.home_at = ms_stamp();
        mn.home_long = 0;
    }
    if (mn.home_at && (btn >> B_HOME & 1u) && !mn.home_long && plat_ms() - mn.home_at >= HOLD_MS)
        mn.home_long = 1;                     /* held: nothing (a long press is never a tap) */
    if (mn.home_at && !(btn >> B_HOME & 1u)) {
        if (!mn.home_long) {
            if (mn.learn) learn_set(0);
            else if (mn.pick) { mn.pick = 0; ui.redraw = 1; }
            else if (mn.page == PG_HOME) go(PG_RACK);
            else { if (mn.page == PG_RACK) rc_all_off(); go(PG_HOME); }
        }
        mn.home_at = 0;
    }
    if (mn.learn && (e = plat_enc(EN_ALGO)) != 0) {   /* LEARN on: ALGORITHM picks its source, on any page */
        int s2 = src_step(learn_source(), (int)e);
        if (!s2) s2 = src_step(s2, (int)e);   /* never "-" */
        if (s2 >= 1 && s2 <= 4) { mn.learn_m = s2 - 1; mn.learn_src = 0; }
        else mn.learn_src = s2;
        app_say("LEARN", src_name(s2));
        ui.redraw = 1;
    }
    if (mn.page == PG_HOME)
        return 0;                   /* holding LFO on home: the knobs are LEARN's, not the macros' */
    if (is_seq_page(mn.page)) {               /* the sequencer's pages: their own encoders */
        seq_encoders();
        return 1;
    }
    if ((e = plat_enc(EN_SELECT)) != 0) {
        if (mn.page == PG_MATRIX && mn.mtab) {
            int rows[12], n = hub_sorted(rows);
            if (n < 1) n = 1;
            mn.hrow[1] = (mn.hrow[1] + (e > 0 ? 1 : -1) + n) % n;
        } else if (mn.page == PG_MATRIX) {
            mn.row = (mn.row + (e > 0 ? 1 : -1) + 12) % 12;   /* round (all pages loop) */
        } else if (!mn.pick) {
            int np = npages(mn.page);
            mn.sub = (mn.sub + (e > 0 ? 1 : -1) + np) % np;
        }
        ui.redraw = 1;
    }
    if (mn.page == PG_MATRIX && (e = plat_enc(EN_ALGO)) != 0) {   /* the hub's tables, round */
        mn.mtab = (mn.mtab + 1) % NHUB;
        ui.redraw = 1;
    }
    if ((e = plat_enc(EN_ALGO)) != 0 && is_slot_page(mn.page)) {
        int slot = page_slot(mn.page), n, i;
        const uint8_t *ch;
        choices(slot, &ch, &n);
        if (!mn.pick) {                       /* the picker opens on the module loaded, and the turn moves it (the page's) */
            mn.pick = 1;
            mn.pick_ix = 0;
            for (i = 0; i < n; i++)
                if (ch[i] == rc_patch.mod[slot])
                    mn.pick_ix = i;
        }
        mn.pick_ix = (mn.pick_ix + (e > 0 ? 1 : -1) + n) % n;   /* round: from EMPTY, one more is the first */
        ui.redraw = 1;
    }
    for (k = mn.lfo_hold ? 1 : 0; k < 4; k++)
        if ((e = plat_enc(EN_K1 + k)) != 0 && mn.page != PG_RACK && !mn.pick) {
            if (mn.learn && learnable(mn.page))
                learn_turn(k, e);
            else
                knob(k, e);
        }
    return 1;
}

/* the main loop, after input: when the knobs have rested, the buffers fitted again if the patch now reaches past them
 * (a longer delay, STR+VERB, ...): a moment of silence while it lays the arena out */
static void menu_tick(void)
{
    if (mn.edited && plat_ms() - mn.edited_at > 400u) {
        mn.edited = 0;
        if (rk_fm1_fit_needed()) {
            char t[8];
            rc_fade_pause();
            rc_refit();
            rc_resume();
            int_text(rk_fm1_status(2), t);
            cat(t, " V");
            app_say("MEMORY FITTED", t);
        }
    }
}

static void draw_menu(void)
{
    if (mn.page == PG_RACK)
        draw_rack();
    else if (mn.page == PG_MATRIX)
        draw_matrix();
    else if (mn.pick)
        draw_picker();
    else
        draw_param_page(mn.page);
}
