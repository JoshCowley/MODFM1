/* SPDX-License-Identifier: MPL-2.0 */
/* FM1 Rack firmware: the 64 user presets, the SAVE page and the library (after the page: web/src/1-data.js
 * openSave / commitSave / libDo, 4-pages.js drawSave / drawLibrary), part of app.c.
 *
 * A user preset keeps what the menus edit: the factory preset it started from (which brings what the menus don't
 * reach: master EQ, tuning, slews), the six slots' modules and parameters, AMP, the LFOs, the 18 routes (the matrix's
 * 12 and the LFOs' direct targets), the macros and the tempo; its name (10 characters) and category; OSC (its
 * difference from the defaults, so stage 2's presets, read 422 bytes each, load with it off). 438 bytes, eight to a
 * store object (OBJ_USER0..7, torn-write safe A/B sectors in the data area).
 *
 *   SAVE page 1, NAME:  SELECT the letter, ALGORITHM or K1 change it, K2 the slot, K3 the category, K4 the page of 16;
 *                       SAVE stores it; OCT+ the library; HOME closes.
 *   SAVE page 2, LIBRARY: every factory and user preset in category tabs; PRESET, ALGORITHM or K1 the row, K2 the tab,
 *                       K3 the action (LOAD, COPY, RENAME, DELETE), OCT+ does it (DELETE asks twice); OCT- back.
 * PRESET on every other page steps through the factory presets, then the user presets saved. */
#define NUSER 64
#define UP_PER_OBJ 8
#define NCAT 6
static const char *const CAT_NAME[NCAT] = {"NONE", "BASS", "PADS", "LEAD", "CHORD", "FX"};
static const uint16_t CAT_COL[NCAT] = {RGB(125, 122, 114), RGB(255, 179, 71), RGB(106, 169, 255), RGB(255, 143, 200),
                                       RGB(155, 227, 122), RGB(180, 140, 255)};
static const char CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-#.";
#define NCHARS ((int)sizeof CHARS - 1)

typedef struct {                              /* one user preset: 438 bytes, no padding (int16 first) */
    int16_t p[6][16];                         /* the slots' parameters (lists, ints as they are; the rest x 10000) */
    int16_t amp[24], lfo[24];
    int16_t ramt[18];                         /* the routes: amount x 10000, source, slot, parameter, via */
    int8_t rsrc[18], rslot[18];
    uint8_t rpar[18];
    int8_t rvia[18];
    char name[10];
    uint8_t used, cat, base, mode;
    uint8_t mod[6];
    uint8_t tempo;
    uint8_t macro[4];                         /* x 250 */
    uint8_t pad;
    int16_t osc[8];                           /* OSC's 7 parameters less their defaults (lists, ints; the rest x 10000) */
} upatch_t;
#define UPATCH_V1 422                         /* stage 2's size: no OSC */
typedef char upatch_size_check[sizeof(upatch_t) == 438 ? 1 : -1];

static struct {
    char name[NUSER][10];
    uint8_t used[NUSER], cat[NUSER];
    int loaded;                               /* the names read from the store once */
} us;
/* one store object's presets, read and written whole. It shares its RAM with the sequencer's undo copy (seqpages.c
 * sp_undo), as the two are never needed at once: reading a group drops the undo (share_undo 0) */
static union { upatch_t g[UP_PER_OBJ]; sq_bank_t undo; } share;
#define ugroup share.g
static int share_undo;                        /* 1: share holds the sequencer's undo */


static int factory_cat(int k)                 /* the factory categories, as the library's tabs */
{
    const char *c = PRESETS[k].cat;
    if (same(c, "BASS")) return 1;
    if (same(c, "PAD")) return 2;
    if (same(c, "LEAD")) return 3;
    if (same(c, "KEYS")) return 4;
    if (same(c, "FX")) return 5;
    return 0;
}
static int entry_cat(int e) { return e < NPRESETS ? factory_cat(e) : us.cat[e - NPRESETS]; }
static void slot_name(int u, char *t)         /* "U01" .. "U64" */
{
    t[0] = 'U';
    t[1] = (char)('0' + (u + 1) / 10);
    t[2] = (char)('0' + (u + 1) % 10);
    t[3] = 0;
}
static void user_name(int u, char *t)         /* the name, trailing spaces off */
{
    int i, n = 0;
    for (i = 0; i < 10; i++)
        if (us.name[u][i] != ' ' && us.name[u][i])
            n = i + 1;
    for (i = 0; i < n; i++)
        t[i] = us.name[u][i];
    t[n] = 0;
}
static void entry_name(int e, char *t)        /* "PD SHIMMER" or "U03 MY PAD" */
{
    if (e < NPRESETS) {
        t[0] = 0;
        cat(t, PRESETS[e].name);
        return;
    }
    slot_name(e - NPRESETS, t);
    cat(t, " ");
    user_name(e - NPRESETS, t + 4);
}

/* ---------------------------------------------------------- the store */
static int group_read(int g)                  /* -> ugroup; an object never saved reads as empty */
{
    int n, i;
    share_undo = 0;
    n = plat_store_load(OBJ_USER0 + (uint32_t)g, ugroup, sizeof ugroup);
    if (n == UPATCH_V1 * UP_PER_OBJ) {        /* stage 2's: spread out to the new size, OSC at its defaults */
        uint8_t *b = (uint8_t *)ugroup;
        for (i = UP_PER_OBJ - 1; i >= 0; i--) {
            int j;
            for (j = UPATCH_V1 - 1; j >= 0; j--)   /* (back to front: the copy lands past where it came from) */
                b[(uint32_t)i * sizeof(upatch_t) + (uint32_t)j] = b[(uint32_t)i * UPATCH_V1 + (uint32_t)j];
            memset(b + (uint32_t)i * sizeof(upatch_t) + UPATCH_V1, 0, sizeof(upatch_t) - UPATCH_V1);
        }
        return 1;
    }
    if (n != (int)sizeof ugroup) {
        memset(ugroup, 0, sizeof ugroup);
        return 0;
    }
    return 1;
}
static int group_write(int g) { return plat_store_save(OBJ_USER0 + (uint32_t)g, ugroup, sizeof ugroup); }
static void users_load(void)                  /* the 64 names and categories, at power-on */
{
    int g, i;
    for (g = 0; g < NUSER / UP_PER_OBJ; g++) {
        group_read(g);
        for (i = 0; i < UP_PER_OBJ; i++) {
            int u = g * UP_PER_OBJ + i, k;
            us.used[u] = ugroup[i].used == 1;
            us.cat[u] = ugroup[i].cat < NCAT ? ugroup[i].cat : 0;
            for (k = 0; k < 10; k++)
                us.name[u][k] = ugroup[i].name[k];
        }
    }
    us.loaded = 1;
}
static int first_free(void)
{
    int u;
    for (u = 0; u < NUSER; u++)
        if (!us.used[u])
            return u;
    return -1;
}

/* a parameter as kept: lists and ints as they are, the rest x 10000 */
static int16_t q_param(const ui_param_t *p, float v)
{
    if (p && (p->t == UT_LIST || p->t == UT_INT))
        return (int16_t)(v < 0.0f ? v - 0.5f : v + 0.5f);
    v *= 10000.0f;
    return (int16_t)(v < 0.0f ? v - 0.5f : v + 0.5f);
}
static float dq_param(const ui_param_t *p, int16_t q)
{
    return p && (p->t == UT_LIST || p->t == UT_INT) ? (float)q : (float)q * 0.0001f;
}

/* what is playing, as a user preset */
static void snapshot(upatch_t *u, const char *name, int cat)
{
    int s, i, r[4];
    memset(u, 0, sizeof *u);
    for (i = 0; i < 10; i++)
        u->name[i] = name[i] ? name[i] : ' ';
    u->used = 1;
    u->cat = (uint8_t)cat;
    u->base = (uint8_t)(rc_patch.index < 0 ? 0 : rc_patch.index);
    u->mode = (uint8_t)rc_patch.mode;
    for (s = 0; s < 6; s++) {
        const ui_mod_t *m = ui_mod(rc_patch.mod[s]);
        u->mod[s] = (uint8_t)rc_patch.mod[s];
        for (i = 0; m && i < m->nparams && i < 16; i++)
            u->p[s][i] = q_param(&m->p[i], rk_get_param(s, i));
    }
    for (i = 0; i < UI_AMP.nparams && i < 24; i++)
        u->amp[i] = q_param(&UI_AMP.p[i], rk_get_param(RS_AMP, i));
    for (i = 0; i < UI_LFO.nparams && i < 24; i++)
        u->lfo[i] = q_param(&UI_LFO.p[i], rk_get_param(RS_LFO, i));
    for (i = 0; i < 18; i++) {
        float a = rk_get_route(i, r);
        u->ramt[i] = q_param(0, a);
        u->rsrc[i] = (int8_t)r[0];
        u->rslot[i] = (int8_t)r[1];
        u->rpar[i] = (uint8_t)r[2];
        u->rvia[i] = (int8_t)r[3];
    }
    for (i = 0; i < 4; i++)
        u->macro[i] = (uint8_t)(rc_patch.macro[i] * 250.0f + 0.5f);
    u->tempo = (uint8_t)(rk_get_param(RS_GLO, 0) + 0.5f);
    for (i = 0; i < UI_OSC.nparams && i < 8; i++)
        u->osc[i] = q_param(&UI_OSC.p[i], rk_get_param(RS_AMP, OSC_P0 + i) - UI_OSC.p[i].def);
}

/* a user preset into the engine (the audio standing aside): its factory preset first, then what it changed */
static void apply(const upatch_t *u)
{
    int s, i;
    rc_load(u->base < NPRESETS ? u->base : 0);
    for (s = 0; s < 6; s++) {
        const ui_mod_t *m = ui_mod(u->mod[s]);
        if (rc_patch.mod[s] != (int8_t)u->mod[s]) {
            rk_set_module(s, u->mod[s]);
            rc_patch.mod[s] = (int8_t)u->mod[s];
            rc_meta(s, u->mod[s]);
        }
        for (i = 0; m && i < m->nparams && i < 16; i++)
            rk_set_param(s, i, dq_param(&m->p[i], u->p[s][i]));
    }
    for (i = 0; i < UI_AMP.nparams && i < 24; i++)
        rc_set(RS_AMP, i, dq_param(&UI_AMP.p[i], u->amp[i]));
    for (i = 0; i < UI_LFO.nparams && i < 24; i++)
        rk_set_param(RS_LFO, i, dq_param(&UI_LFO.p[i], u->lfo[i]));
    rc_rings_fix();
    for (i = 0; i < 18; i++)
        rk_set_route(i, u->rsrc[i], u->rslot[i], u->rpar[i], dq_param(0, u->ramt[i]), u->rvia[i]);
    for (i = 0; i < 4; i++) {
        rc_patch.macro[i] = (float)u->macro[i] / 250.0f;
        rk_set_macro(i, rc_patch.macro[i]);
    }
    if (u->tempo >= 40)
        rk_set_param(RS_GLO, 0, (float)u->tempo);
    for (i = 0; i < UI_OSC.nparams && i < 8; i++)
        rk_set_param(RS_AMP, OSC_P0 + i, UI_OSC.p[i].def + dq_param(&UI_OSC.p[i], u->osc[i]));
    rc_patch.mode = u->mode < 5 ? u->mode : 0;
    rk_set_legato(rc_patch.mode == RVM_MONO ? 0 : 1);
    rc_patch.voices = rc_patch.mode == RVM_MONO || rc_patch.mode == RVM_LEGATO ? 1 : RC_MAXV;
    rk_set_voices(rc_patch.voices);
    rk_fm1_fit();
    rc_patch.voices_now = rk_fm1_status(3);
}

/* ---------------------------------------------------------- the presets playing */
static void load_entry(int e)
{
    if (e < NPRESETS) {
        load_preset(e);
    } else {
        int u = e - NPRESETS;
        group_read(u / UP_PER_OBJ);
        rc_pause();
        apply(&ugroup[u % UP_PER_OBJ]);
        if (rc_patch.mode == RVM_POLY && rc_patch.voices_now > START_VOICES) {
            rk_set_voices(START_VOICES);
            rc_patch.voices_now = START_VOICES;
        }
        glo_apply();                          /* GLOBAL's tempo, EQ and tuning, as for a factory preset */
        rc_resume();
        rc_push(PLE_RESET, 0, 0.0f, 0.0f);
    }
    cur_entry = e;
}
static int entry_step(int e, int d)          /* the next preset that exists, factory then user, round */
{
    int n;
    for (n = 0; n < NPRESETS + NUSER; n++) {
        e = (e + d + NPRESETS + NUSER) % (NPRESETS + NUSER);
        if (e < NPRESETS || us.used[e - NPRESETS])
            return e;
    }
    return e;
}

/* ---------------------------------------------------------- the SAVE page */
enum { LT_ALL, LT_USER, LT_BASS, LT_PADS, LT_LEAD, LT_CHORD, LT_FX, LT_OTHER, NTABS };
static const char *const TAB_SHORT[NTABS] = {"ALL", "USR", "BS", "PD", "LD", "CH", "FX", "OTH"};
static const char *const ACT[4] = {"LOAD", "COPY", "RENAME", "DELETE"};
static struct {
    int open, lib;                            /* the page is open; on the library */
    char name[10];
    int cursor, slot, cat, rename;            /* rename: the user slot being renamed, -1 none */
    int tab, ix, act, arm;                    /* the library: tab, row, action, the slot DELETE is armed for */
} sv;

static int lib_has(int e)
{
    int c = entry_cat(e);
    if (e >= NPRESETS && !us.used[e - NPRESETS]) return 0;
    if (sv.tab == LT_ALL) return 1;
    if (sv.tab == LT_USER) return e >= NPRESETS;
    if (sv.tab == LT_OTHER) return c == 0;
    return c == sv.tab - 1;
}
static int lib_count(void)
{
    int e, n = 0;
    for (e = 0; e < NPRESETS + NUSER; e++)
        n += lib_has(e);
    return n;
}
static int lib_entry(int ix)                 /* the ix'th entry in the tab, -1 none */
{
    int e;
    for (e = 0; e < NPRESETS + NUSER; e++)
        if (lib_has(e) && ix-- == 0)
            return e;
    return -1;
}

static void save_open(void)
{
    char t[24];
    int i, f = first_free();
    entry_name(cur_entry, t);
    for (i = 0; i < 10; i++)
        sv.name[i] = ' ';
    for (i = 0; i < 10 && t[i + (cur_entry >= NPRESETS ? 4 : 0)]; i++)
        sv.name[i] = t[i + (cur_entry >= NPRESETS ? 4 : 0)];
    sv.cursor = 0;
    sv.slot = cur_entry >= NPRESETS ? cur_entry - NPRESETS : f < 0 ? 0 : f;
    sv.cat = entry_cat(cur_entry);
    sv.rename = -1;
    sv.lib = 0;
    sv.arm = -1;
    sv.open = 1;
    ui.redraw = 1;
}
static int save_put(int u, const upatch_t *p)  /* one preset into its object; 0 = stored */
{
    int g = u / UP_PER_OBJ, k, rc;
    group_read(g);
    ugroup[u % UP_PER_OBJ] = *p;
    rc = group_write(g);
    us.used[u] = p->used == 1;
    us.cat[u] = p->cat;
    for (k = 0; k < 10; k++)
        us.name[u][k] = p->name[k];
    return rc;
}
static void save_commit(void)
{
    static upatch_t p;
    char t[24];
    if (sv.rename >= 0) {                    /* renaming keeps the sound */
        int g = sv.rename / UP_PER_OBJ, k, rc;
        group_read(g);
        for (k = 0; k < 10; k++)
            ugroup[sv.rename % UP_PER_OBJ].name[k] = sv.name[k];
        ugroup[sv.rename % UP_PER_OBJ].cat = (uint8_t)sv.cat;
        rc = group_write(g);
        for (k = 0; k < 10; k++)
            us.name[sv.rename][k] = sv.name[k];
        us.cat[sv.rename] = (uint8_t)sv.cat;
        slot_name(sv.rename, t);
        app_say(rc ? "STORE FULL" : "RENAMED", t);
        sv.rename = -1;
        sv.lib = 1;
        return;
    }
    snapshot(&p, sv.name, sv.cat);
    slot_name(sv.slot, t);
    if (save_put(sv.slot, &p)) {
        app_say("STORE FULL", "NOT SAVED");
        return;
    }
    cur_entry = NPRESETS + sv.slot;
    ui.shown = cur_entry;
    set.preset = (uint8_t)cur_entry;          /* and it comes back at the next power-on */
    plat_store_save(OBJ_SETTINGS, &set, sizeof set);
    app_say("SAVED TO", t);
    sv.open = 0;
}
static void lib_do(void)
{
    static upatch_t p;
    int e = lib_entry(sv.ix), u;
    char t[24];
    if (e < 0) { app_say("NOTHING", "HERE"); return; }
    if (sv.act != 3)
        sv.arm = -1;
    if (sv.act == 0) {                       /* LOAD */
        rc_all_off();
        load_entry(e);
        ui.shown = e;
        entry_name(e, t);
        app_say(t, "");
        return;
    }
    if (sv.act == 1) {                       /* COPY: into the first free slot; a factory one is loaded first */
        int f = first_free();
        if (f < 0) { app_say("ALL 64 SLOTS", "FULL"); return; }
        if (e >= NPRESETS) {
            group_read((e - NPRESETS) / UP_PER_OBJ);
            p = ugroup[(e - NPRESETS) % UP_PER_OBJ];
        } else {
            char n[10];
            int i, end = 0;
            rc_all_off();
            load_entry(e);
            ui.shown = e;
            for (i = 0; i < 10; i++) {
                if (!PRESETS[e].name[i]) end = 1;
                n[i] = end ? 0 : PRESETS[e].name[i];
            }
            snapshot(&p, n, factory_cat(e));
        }
        slot_name(f, t);
        app_say(save_put(f, &p) ? "STORE FULL" : "COPIED TO", t);
        return;
    }
    if (e < NPRESETS) { app_say("FACTORY:", "COPY IT FIRST"); return; }
    u = e - NPRESETS;
    if (sv.act == 2) {                       /* RENAME: the name page, SAVE keeps it */
        int i;
        for (i = 0; i < 10; i++)
            sv.name[i] = us.name[u][i] ? us.name[u][i] : ' ';
        sv.cursor = 0;
        sv.slot = u;
        sv.cat = us.cat[u];
        sv.rename = u;
        sv.lib = 0;
        slot_name(u, t);
        app_say("RENAME", t);
        return;
    }
    slot_name(u, t);                          /* DELETE, twice */
    if (sv.arm != u) {
        sv.arm = u;
        app_say("OCT+ AGAIN:", "DELETE");
        return;
    }
    memset(&p, 0, sizeof p);
    app_say(save_put(u, &p) ? "STORE FULL" : "DELETED", t);
    sv.arm = -1;
    if (cur_entry == e)
        cur_entry = rc_patch.index;
}

static int save_input(uint32_t press)        /* 1: the SAVE page took the controls */
{
    int32_t e;
    int k, d;
    if (!sv.open)
        return 0;
    if (press >> B_HOME & 1u) {
        if (sv.rename >= 0) { sv.rename = -1; sv.lib = 1; app_say("RENAME", "CANCELLED"); }
        else sv.open = 0;
        ui.redraw = 1;
        return 1;
    }
    if (press >> B_SAVE & 1u) {
        if (sv.lib) sv.lib = 0;
        else save_commit();
        ui.redraw = 1;
        return 1;
    }
    if (press >> B_OCTUP & 1u) {
        if (sv.lib) lib_do();
        else if (sv.rename < 0) { sv.lib = 1; sv.arm = -1; }
        ui.redraw = 1;
    }
    if (press >> B_OCTDN & 1u && sv.lib) {
        sv.lib = 0;
        sv.arm = -1;
        ui.redraw = 1;
    }
    for (k = -3; k < 4; k++) {               /* PRESET (-3), SELECT (-2), ALGORITHM (-1), K1-K4 */
        int role = k == -3 ? EN_PRESET : k == -2 ? EN_SELECT : k == -1 ? EN_ALGO : EN_K1 + k;
        if ((e = plat_enc(role)) == 0)
            continue;
        d = e > 0 ? 1 : -1;
        ui.redraw = 1;
        if (sv.lib) {
            int n = lib_count();
            if (k == -3 || k == -1 || k == 0) { sv.ix += d; sv.arm = -1; }
            else if (k == 1) { sv.tab += d; sv.tab = sv.tab < 0 ? 0 : sv.tab >= NTABS ? NTABS - 1 : sv.tab; sv.ix = 0; sv.arm = -1; n = lib_count(); }
            else if (k == 2) { sv.act += d; sv.act = sv.act < 0 ? 0 : sv.act > 3 ? 3 : sv.act; sv.arm = -1; }
            else if (k == -2 && d < 0) sv.lib = 0;
            sv.ix = sv.ix < 0 ? 0 : sv.ix >= n ? (n ? n - 1 : 0) : sv.ix;
            continue;
        }
        if (k == -2) {
            if (sv.cursor == 9 && d > 0 && sv.rename < 0) sv.lib = 1;
            else { sv.cursor += d; sv.cursor = sv.cursor < 0 ? 0 : sv.cursor > 9 ? 9 : sv.cursor; }
        } else if (k == -1 || k == 0) {
            int c = 0, i;
            for (i = 0; i < NCHARS; i++)
                if (CHARS[i] == sv.name[sv.cursor])
                    c = i;
            sv.name[sv.cursor] = CHARS[(c + d + NCHARS) % NCHARS];
        } else if (k == 1 && sv.rename < 0) {
            sv.slot += d;
            sv.slot = sv.slot < 0 ? 0 : sv.slot >= NUSER ? NUSER - 1 : sv.slot;
        } else if (k == 2) {
            sv.cat += d;
            sv.cat = sv.cat < 0 ? 0 : sv.cat >= NCAT ? NCAT - 1 : sv.cat;
        } else if (k == 3 && sv.rename < 0) {
            int pg = sv.slot / 16 + d;
            pg = pg < 0 ? 0 : pg > 3 ? 3 : pg;
            sv.slot = pg * 16 + sv.slot % 16;
        }
    }
    return 1;
}

static void draw_save_name(void)
{
    char t[32], n[12];
    int i, k, pg = sv.slot / 16;
    int blink = (plat_ms() / 333u) & 1u;
    header("SAVE", K_FG, sv.rename >= 0 ? "RENAME" : "NAME + SLOT", IC_SAVE, 2, 0);
    cv_begin(240, 40, K_BG);
    icon(IC_SAVE, 10, 6, K_GOLD);
    slot_name(sv.slot, n);
    t[0] = 0;
    if (sv.rename < 0)
        cat(t, "TO ");
    cat(t, n);
    cv_text(30, 2, &FONT_B, t, K_GOLD);
    t[0] = 0;
    cat(t, "PAGE ");
    t[5] = (char)('A' + pg);
    t[6] = 0;
    cv_text(100, 5, &FONT_T, t, K_DIM);
    rrect(150, 3, 84, 15, CAT_COL[sv.cat], 3);
    cv_text(192 - text_w(&FONT_T, CAT_NAME[sv.cat]) / 2, 4, &FONT_T, CAT_NAME[sv.cat], K_BG);
    t[0] = 0;
    if (sv.rename >= 0)
        cat(t, "THE SOUND IS KEPT");
    else if (us.used[sv.slot]) {
        cat(t, "REPLACES ");
        user_name(sv.slot, t + 9);
    } else
        cat(t, "EMPTY SLOT");
    cv_text(30, 22, &FONT_T, t, us.used[sv.slot] && sv.rename < 0 ? K_WARN : K_DIM);
    cv_blit(0, 32);
    cv_begin(240, 36, K_BG);
    for (i = 0; i < 10; i++) {
        int32_t x = 11 + i * 22, on = i == sv.cursor;
        char ch[2];
        rrect(x, 0, 19, 30, on ? RGB(42, 41, 36) : RGB(20, 20, 18), 3);
        ch[0] = sv.name[i] == ' ' ? '.' : sv.name[i];
        ch[1] = 0;
        cv_text(x + 10 - text_w(&FONT_B, ch) / 2, 4, &FONT_B, ch, sv.name[i] == ' ' ? K_FAINT : on ? K_GOLD : K_FG);
        if (on)
            cv_rect(x, 32, 19, 2, blink ? K_GOLD : RGB(122, 106, 58));
    }
    cv_blit(0, 74);
    for (k = 0; k < 4; k++) {                 /* this page's 16 slots, with names and categories */
        cv_begin(240, 21, K_BG);
        for (i = 0; i < 4; i++) {
            int u = pg * 16 + k * 4 + i, sel = u == sv.slot;
            int32_t x = 4 + i * 58;
            rrect(x, 1, 56, 19, us.used[u] ? RGB(34, 33, 29) : RGB(17, 17, 16), 3);
            if (us.used[u])
                cv_rect(x, 4, 2, 14, CAT_COL[us.cat[u]]);
            if (sel) {
                cv_rect(x - 1, 0, 58, 1, K_GOLD);
                cv_rect(x - 1, 20, 58, 1, K_GOLD);
                cv_rect(x - 1, 0, 1, 21, K_GOLD);
                cv_rect(x + 56, 0, 1, 21, K_GOLD);
            }
            slot_name(u, n);
            cv_text(x + 5, 1, &FONT_T, n + 1, K_DIM);
            if (us.used[u]) {
                user_name(u, t);
                t[8] = 0;
                cv_text(x + 18, 5, &FONT_T, t, K_FG);
            } else
                cv_text(x + 18, 5, &FONT_T, "-", K_FAINT);
        }
        cv_blit(0, 110 + (uint32_t)k * 21);
    }
    help(sv.rename >= 0 ? "K1 LETTER  K3 CAT  SAVE KEEPS  HOME BACK" : "K1 LETTER K2 SLOT K3 CAT K4 PAGE  OCT+ LIB");
}

static void draw_library(void)
{
    char t[32];
    int i, n = lib_count(), top, k, e0 = lib_entry(sv.ix), can;
    header("SAVE", K_FG, "LIBRARY", IC_SAVE, 2, 1);
    cv_begin(240, 16, K_BG);
    for (i = 0; i < NTABS; i++) {
        int32_t x = 3 + i * 29, on = i == sv.tab;
        uint16_t c = on ? (i >= LT_BASS && i <= LT_FX ? CAT_COL[i - 1] : K_FG) : RGB(22, 22, 20);
        rrect(x, 1, 27, 14, c, 3);
        cv_text(x + 14 - text_w(&FONT_T, TAB_SHORT[i]) / 2, 2, &FONT_T, TAB_SHORT[i], on ? K_BG : K_DIM);
    }
    cv_blit(0, 32);
    top = sv.ix - 3;
    if (top > n - 7) top = n - 7;
    if (top < 0) top = 0;
    for (k = 0; k < 7; k++) {
        int ix = top + k, e = ix < n ? lib_entry(ix) : -1, sel = ix == sv.ix;
        cv_begin(240, 19, K_BG);
        if (e >= 0) {
            int loaded = e == cur_entry;
            if (sel) {
                rrect(3, 0, 234, 18, RGB(37, 36, 31), 4);
                cv_rect(3, 0, 2, 18, K_GOLD);
            }
            cv_rect(9, 4, 3, 10, CAT_COL[entry_cat(e)]);
            entry_name(e, t);
            t[19] = 0;
            cv_text(16, 3, &FONT_T, t, sel ? K_FG : loaded ? K_GOLD : RGB(189, 186, 177));
            cv_text(232 - text_w(&FONT_T, e >= NPRESETS ? "USER" : "FACT"), 3, &FONT_T, e >= NPRESETS ? "USER" : "FACT",
                    e >= NPRESETS ? K_GOLD : K_DIM);
        } else if (k == 0 && !n) {
            cv_text(120 - text_w(&FONT_XS, "NOTHING IN THIS TAB") / 2, 2, &FONT_XS,
                    sv.tab == LT_USER ? "NO USER PRESETS YET" : "NOTHING IN THIS TAB", K_DIM);
        }
        cv_blit(0, 50 + (uint32_t)k * 20);
    }
    cv_begin(240, 24, K_BG);
    {
        int armed = sv.act == 3 && e0 >= NPRESETS && sv.arm == e0 - NPRESETS, used = 0;
        can = e0 >= 0 && (sv.act <= 1 || e0 >= NPRESETS);
        rrect(4, 2, 76, 18, armed ? K_WARN : can ? RGB(42, 41, 36) : RGB(21, 21, 19), 4);
        cv_text(42 - text_w(&FONT_XS, armed ? "CONFIRM" : ACT[sv.act]) / 2, 3, &FONT_XS, armed ? "CONFIRM" : ACT[sv.act],
                armed ? K_BG : can ? K_GOLD : K_FAINT);
        for (i = 0; i < NUSER; i++)
            used += us.used[i];
        t[0] = 0;
        int_text(n ? sv.ix + 1 : 0, t);
        cat(t, "/");
        {
            char b[8];
            int_text(n, b);
            cat(t, b);
            cat(t, "   ");
            int_text(used, b);
            cat(t, b);
            cat(t, "/64 USER");
        }
        cv_text(86, 0, &FONT_T, t, K_DIM);
        cv_text(86, 11, &FONT_T, "K1 ROW K2 TAB K3 ACTION  OCT+ GO", K_DIM);
    }
    cv_blit(0, 190);
}

static void draw_save(void)
{
    if (sv.lib) draw_library();
    else draw_save_name();
}
