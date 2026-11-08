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
#define OSC_P0 24                             /* OSC's first parameter on AMP (engine/rack.cc OSC_WAVE) */
#define K_OSC RGB(255, 214, 150)
#define K_AMP RGB(255, 122, 107)
#define K_LFO RGB(201, 167, 255)
#define K_MX RGB(255, 210, 122)
#define K_SEL RGB(36, 35, 30)
#define K_GLO RGB(216, 215, 211)
#define K_CHD RGB(155, 227, 122)
#define K_ARP RGB(127, 214, 255)
#define K_SEQC RGB(255, 143, 200)

static struct {
    int page, sub, row;                       /* the page, its page within (SELECT), the matrix row */
    int pick, pick_ix;                        /* the picker: open (1), the choice shown */
    uint32_t edited_at;                       /* the last edit: the buffers are checked when the knobs rest */
    int edited;
    int learn, learn_m;
    uint32_t edit_at;                         /* EDIT tapped, waiting for a second tap */                       /* LEARN on; the macro it links (0-3: M1-M4) */
    uint32_t home_at;                         /* HOME went down (a hold starts LEARN), 0 up */
    int home_long;
} mn;

static const uint8_t SRC_CH[] = {1, 2};
static const uint8_t SHP_CH[] = {10, 12, 0};
static const uint8_t MOD_CH[] = {20, 21, 23, 0};
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
    static const uint16_t C[] = {K_SRC, K_SHP, K_MOD, K_MOD, K_FX, K_FX, K_AMP, K_FG, K_LFO};
    return slot >= 0 && slot < 9 ? C[slot] : K_FG;
}
static const char *const SLOT_TAG[] = {"SRC", "SHP", "MOD A", "MOD B", "FX 1", "FX 2", "AMP", "GLO", "LFO"};
static uint16_t page_colour(int pg) { return is_fixed(pg) ? fixed_colour(pg) : pg == PG_OSC ? K_OSC : pg == PG_SEQ ? K_SEQC : pg == PG_MSEQ ? K_LFO : slot_colour(page_slot(pg)); }
static void choices(int slot, const uint8_t **ch, int *n)
{
    if (slot == RS_SRC) { *ch = SRC_CH; *n = 2; }
    else if (slot == RS_SHP) { *ch = SHP_CH; *n = 3; }
    else if (slot == RS_MODA || slot == RS_MODB) { *ch = MOD_CH; *n = 4; }
    else { *ch = FX_CH; *n = 4; }
}
static int page_first(const ui_mod_t *m, int sub)
{
    int i, o = 0;
    for (i = 0; i < sub && i < m->npages; i++)
        o += m->pages[i];
    return o;
}
static int npages(int pg)
{
    const ui_mod_t *m = page_mod(pg);
    return m ? m->npages + (pg == PG_LFO ? 3 : 0) : 1;
}

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
    if (s >= 1 && s <= 4) return RGB(255, 210, 122);      /* the macros: gold */
    if (s == 5) return RGB(95, 224, 138);                 /* velocity: green */
    if (s == 6) return RGB(80, 205, 255);                 /* key tracking: sky blue */
    if (s == 7) return RGB(242, 239, 230);                /* random: white */
    if (s == 8 || s == 16) return RGB(255, 122, 107);     /* the envelopes: coral */
    if (s >= 9 && s <= 12) return RGB(255, 160, 40);      /* MOD A and B's outputs: orange */
    if (s >= 13 && s <= 15) return RGB(170, 120, 255);    /* the LFOs: violet */
    if (s >= 17 && s <= 20) return RGB(255, 110, 190);    /* MSEQ's lanes: pink */
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
    rrect(22, 4, w, 14, c, 3);
    cv_text(26, 4, &FONT_T, tag, K_BG);
    cv_text(28 + w, 3, &FONT_B, title, K_FG);
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
    uint16_t halo = mix565(c, K_WELL, 46);
    int i;
    for (i = -1; i <= 1; i += 2) {
        cv_line(x0, bot + i, ax, top + i, halo);
        cv_line(ax, top + i, dx, sy + i, halo);
        cv_line(dx, sy + i, sx, sy + i, halo);
        cv_line(sx, sy + i, rx, bot + i, halo);
    }
    cv_line(x0, bot, ax, top, c);
    cv_line(ax, top, dx, sy, c);
    cv_line(dx, sy, sx, sy, c);
    cv_line(sx, sy, rx, bot, c);
    (void)w;
}

#include "vis.c"
#include "vis3.c"

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
    rrect(0, 0, 112, CELL_H, p ? K_CELL : RGB(10, 10, 9), 4);
    t[0] = (char)('1' + i);
    t[1] = 0;
    cv_text(6, 2, &FONT_T, t, K_FAINT);
    if (!p) {
        rrect(8, 52, 96, 4, RGB(20, 20, 19), 2);
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

static void help(const char *s)
{
    cv_begin(240, 16, K_BG);
    if (mn.learn) {                           /* LEARN: a gold band in its place */
        char t[40] = "LEARN M";
        t[7] = (char)('1' + mn.learn_m);
        t[8] = 0;
        cat(t, ": TURN A KNOB   EDIT x2: DONE");
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
        cell(i, p, v, lv, mc, c);
    }
}

static void draw_param_page(int pg)
{
    const ui_mod_t *m = page_mod(pg);
    int slot = page_slot(pg), i, off;
    uint16_t c = page_colour(pg);
    char title[24];
    float *fv = page_vals(pg);
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
        static const char *const GT[] = {"SETTINGS", "PLAYING", "TUNING", "MIDI", "MASTER", "SYSTEM"};
        static const char *const ST[] = {"SCALE + CHORD", "KEYS + STRUM"}, *const AT[] = {"ARPEGGIATOR", "HOLD + TURING"};
        int ic = pg == PG_GLO ? (mn.sub == 3 ? IC_MIDI : mn.sub == 4 ? IC_OUT : IC_GLOBAL) : pg == PG_SEL ? (mn.sub ? IC_SCALE : IC_CHORD) : IC_ARP;
        header(pg == PG_GLO ? "GLO" : pg == PG_SEL ? "SEL" : "ARP", c, pg == PG_GLO ? GT[mn.sub % 6] : pg == PG_SEL ? ST[mn.sub & 1] : AT[mn.sub & 1],
               ic, npages(pg), mn.sub);
        well(pg, m, c);
        off = page_first(m, mn.sub);
        if (pg == PG_GLO && mn.sub == 5)
            system_cells(c);
        else
            for (i = 0; i < 4; i++) {
                const ui_param_t *p = i < m->pages[mn.sub] ? &m->p[off + i] : 0;
                cell(i, p, p ? fv[off + i] : 0.0f, 0.0f, 0, c);
            }
        help(pg == PG_GLO && mn.sub == 5 ? "SELECT: PAGE   (MEASURED LIVE)" : pg == PG_ARP ? "SELECT: PAGE   K1-K4: VALUES"
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
    header(pg == PG_OSC ? "OSC" : SLOT_TAG[slot], c, pg == PG_OSC ? (mn.sub ? "MIXER" : "OSCILLATOR") : title,
           pg == PG_AMP ? IC_AMP : pg == PG_LFO ? IC_LFO : pg == PG_OSC ? IC_KNOB : module_icon(m->id), npages(pg), mn.sub);
    well(pg, pg == PG_AMP || pg == PG_LFO || pg == PG_OSC ? 0 : m, c);
    if (pg == PG_LFO && mn.sub >= m->npages) {
        draw_lfo_targets(mn.sub - m->npages);
        help("K1 K3: DESTINATION   K2 K4: AMOUNT");
        return;
    }
    param_cells(pg);
    help(pg == PG_OSC ? "SELECT: PAGE   K1-K4: VALUES   EDIT: SOURCE" : pg == PG_AMP || pg == PG_LFO ? "SELECT: PAGE   K1-K4: VALUES"
         : "SELECT: PAGE   K1-K4: VALUES   ALGO: MODULE");
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
            cv_text(26, 6, &FONT_S, m ? m->name : "EMPTY", K_FG);
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

static void draw_matrix(void)
{
    int k, top = mn.row - 2;
    char t[32];
    if (top < 0) top = 0;
    if (top > 6) top = 6;
    t[0] = 0;
    cat(t, "ROW ");
    int_text(mn.row + 1, t + 4);
    cat(t, "/12");
    header("MATRIX", K_MX, t, IC_MATRIX, 1, 0);
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
    help("SELECT: ROW  K1 SRC  K2 DEST  K3 AMT  K4 VIA");
}

static void rack_tile(int32_t x, const char *tag, int slot, uint16_t c, const char *fixed, int ic)
{
    const ui_mod_t *m = slot >= 0 ? ui_mod(rc_patch.mod[slot]) : 0;
    const char *name = slot >= 0 ? (m ? m->name : 0) : fixed;
    int lost = slot >= 0 && (rk_fm1_status(4) >> slot & 1);
    rrect(x, 0, 72, 36, name ? RGB(19, 19, 18) : K_WELL, 4);
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
    rc_pause();
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
    go(cyc[at < 0 ? 0 : (at + 1) % n]);
}

static void edited(void)
{
    mn.edited = 1;
    mn.edited_at = plat_ms();
    ui.redraw = 1;
}

/* a setting on SEL, GLO or ARP changed: what follows from it, then saved when the knobs rest */
static void fixed_changed(int pg, int pi, float old, float v)
{
    if (pg == PG_GLO) {
        if (pi == G_TEMPO) {
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
    } else if (pg == PG_ARP && (pi == A_HOLD || (pi == A_MODE && ((old < 0.5f) != (v < 0.5f))))) {
        rc_push(PLE_ARP_CLEAR, 0, 0.0f, 0.0f);   /* the arp on or off, HOLD changed: start from the keys held now */
    }
    settings_touch();
}

/* ---------------------------------------------------------- LEARN */
static void learn_set(int on)
{
    char t[4] = "M1";
    mn.learn = on;
    t[1] = (char)('1' + mn.learn_m);
    app_say(on ? "LEARN" : "LEARN DONE", on ? t : "");
    ui.redraw = 1;
}
static int learnable(int pg)
{
    if (pg == PG_LFO)
        return mn.sub < UI_LFO.npages;        /* not the LFO target pages */
    return pg == PG_AMP || pg == PG_OSC || (is_slot_page(pg) && page_mod(pg));
}
/* a knob turned while learning: the macro linked to its parameter, the turn its amount */
static void learn_turn(int k, int32_t e)
{
    const ui_mod_t *m = page_mod(mn.page);
    int slot = page_slot(mn.page), pi, r, row = -1, free_row = -1, src, via, msrc = 1 + mn.learn_m;
    float amt;
    uint16_t d;
    char t[40], u[24];
    if (!m || k >= m->pages[mn.sub])
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
    t[0] = 'M';
    t[1] = (char)('1' + mn.learn_m);
    t[2] = '>';
    t[3] = 0;
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
    if (mn.page == PG_MATRIX) {
        int src, via;
        float amt;
        uint16_t d = route_dst(mn.row, &src, &via, &amt);
        if (k == 0) src = src_step(src, (int)e);
        if (k == 1) d = dst_step(d, (int)e);
        if (k == 2) { amt += (float)e * 0.01f; amt = amt < -1.0f ? -1.0f : amt > 1.0f ? 1.0f : amt; }
        if (k == 3) via = src_step(via, (int)e);
        if (!src && k != 0 && d)
            src = 1;
        route_set(mn.row, src, d, amt, via);
        edited();
        return;
    }
    if (!m)
        return;
    if (is_fixed(mn.page)) {
        int pi = page_first(m, mn.sub) + k;
        float *fv = page_vals(mn.page), old;
        if (k >= m->pages[mn.sub])
            return;
        old = fv[pi];
        fv[pi] = step(&m->p[pi], old, e);
        if (fv[pi] != old)
            fixed_changed(mn.page, pi, old, fv[pi]);
        ui.redraw = 1;
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

/* the buttons and encoders of the menus; 1 when they were the menus' (input() does the rest) */
static int menu_input(uint32_t press, uint32_t btn)
{
    static const uint8_t ENV_CYC[] = {PG_AMP, PG_SHP}, LFO_CYC[] = {PG_MODA, PG_MODB, PG_LFO, PG_MSEQ, PG_MATRIX}, FX_CYC[] = {PG_FX1, PG_FX2};
    static const uint8_t SRC_CYC[] = {PG_SRC, PG_OSC}, ARP_CYC[] = {PG_ARP}, SEL_CYC[] = {PG_SEL}, GLO_CYC[] = {PG_GLO};
    int32_t e;
    int k;
    if (press >> B_EDIT & 1u) {               /* EDIT: a tap turns the page once the double tap's time has passed; */
        if (mn.edit_at && plat_ms() - mn.edit_at < EDIT_DOUBLE_MS) {   /* two taps: LEARN on or off */
            mn.edit_at = 0;
            learn_set(!mn.learn);
        } else {
            mn.edit_at = plat_ms() | 1u;
        }
    }
    if (mn.edit_at && plat_ms() - mn.edit_at >= EDIT_DOUBLE_MS) {
        mn.edit_at = 0;
        page_button(SRC_CYC, 2);
    }
    if (press >> B_ENV & 1u) page_button(ENV_CYC, 2);
    if (press >> B_LFO & 1u) page_button(LFO_CYC, 5);
    if (press >> B_FX & 1u) page_button(FX_CYC, 2);
    if (press >> B_ARP & 1u) page_button(ARP_CYC, 1);
    if (press >> B_SEL & 1u) page_button(SEL_CYC, 1);
    if (press >> B_GLO & 1u) page_button(GLO_CYC, 1);
    if (press >> B_HOME & 1u) {               /* HOME: a tap on letting go (LEARN is EDIT's double tap now) */
        mn.home_at = plat_ms() | 1u;
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
    if (mn.page == PG_HOME)
        return 0;
    if (is_seq_page(mn.page)) {               /* the sequencer's pages: their own encoders */
        seq_encoders();
        return 1;
    }
    if ((e = plat_enc(EN_SELECT)) != 0) {
        if (mn.page == PG_MATRIX) {
            mn.row = (mn.row + (e > 0 ? 1 : -1) + 12) % 12;   /* round (all pages loop) */
        } else if (!mn.pick) {
            int np = npages(mn.page);
            mn.sub = (mn.sub + (e > 0 ? 1 : -1) + np) % np;
        }
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
    for (k = 0; k < 4; k++)
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
            rc_pause();
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
