/* SPDX-License-Identifier: MPL-2.0 */
/* FM1 Rack firmware: the sequencer's pages (stage 4, after the HTML mockup of 4 Nov 2026 and the page's 5d-sequencer.js),
 * part of app.c after frame.c.
 *
 *   SEQ (tap)   NOTES, PATTERN, SPICE, GEN, BANKS, SONG in turn     PLAY  start / stop        SAVE  keep the sequencer
 *   SEQ (hold)  the 16 white keys are steps (tap: on with the last notes played / off; held + K1-K4: its notes'
 *               pitch, force, ratchet, nudge; OCT- / OCT+ the next 16)
 *   REC         stopped: REC READY (the first note or PLAY starts it, recording); playing: recording on / off
 *   LFO         ... then MSEQ, four lanes of 16 values (the matrix's SEQ 1-4)
 * On each page SELECT moves the cursor (the step, the bank, the row), ALGORITHM does the page's action, K1-K4 its four
 * values, drawn as dials. The pages draw a 94-row area in three bands (the canvas holds 40 rows). */

enum { SQP_NOTES, SQP_PATTERN, SQP_SPICE, SQP_GEN, SQP_BANKS, SQP_SONG, SQP_N };
static const char *const SQP_NAME[SQP_N] = {"NOTES", "PATTERN", "SPICE", "GEN", "BANKS", "SONG"};
static const char *const GEN_TOOLS[] = {"EUCLID", "RANDOM", "ACID", "ARP UP", "ARP UPDN", "BERLIN", "OCTAVES", "OFFBEAT",
                                        "ROLLING", "STABS", "PROGRESS", "GATES", "SHIFT >", "SHIFT <", "REVERSE", "DOUBLE",
                                        "HALF", "CLEAR"};
#define NGEN 18
static const char *const BANK_ACTS[4] = {"COPY TO", "CLEAR", "DOUBLE", "HALVE"};
static const char *const MS_SHAPES[10] = {"-", "RAMP+", "RAMP-", "TRI", "SINE", "SQR", "RND", "FLAT", "INVERT", "SHIFT"};

static struct {
    int note_ix;                              /* the note picked in the cursor's step, -1 none */
    int gen_tool, gen_a, gen_b, gen_seed;     /* GEN: the tool and its two values (0-100), the seed */
    int bank_act, bank_tgt, now;              /* BANKS: the action, its target, switching now (1) or at the loop end */
    int song_cur;
    int ms_lane, ms_cur, ms_shape;
    int layer, layer_page, held, turned;      /* hold SEQ: the layer, its 16 steps' page, the step held, a knob moved */
    uint32_t seq_at;                          /* SEQ went down */
    int undo_ok, seq_sub;
    int lasso, lasso_a, lasso_b;              /* NOTES (update 38): hold SEL + turn K1, steps lasso_a..lasso_b picked */
    uint32_t save_at;                         /* SAVE went down on a sequencer page (held on NOTES: copy / paste) */
    int save_long;
} sp = {0, 0, 50, 30, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
#define SQ_CLIP 32
static sq_step_t sq_clip[SQ_CLIP];            /* the steps hold SAVE copied (at most two pages of 16) */
static int sq_nclip;
#define sp_undo share.undo                    /* GEN and BANKS: the bank before, ALGO left puts it back (shares save.c's RAM) */

static int is_seq_page(int pg) { return pg == PG_SEQ || pg == PG_MSEQ; }
static sq_bank_t *cur_bank(void) { return &sqb[sq.bank]; }
static void sq_touch(void) { sq_dirty |= (uint8_t)(1u << sq.bank); ui.redraw = 1; }

/* ------------------------------------------------------------------ drawing helpers --- */
/* the area (after the stage 4 build: the sliders moved to the top, the area taller for the piano roll): 228 x AH,
 * drawn in bands of AB rows (the canvas holds 9600 pixels) */
#define AH 152
#define AB 38
#define AREA_Y 68
static void band(int b)                       /* draw the next things in the 228 x AB band b of the area */
{
    cv_begin(228, AB, K_BG);
    cv_oy = -AB * b;
    cv_y0 = 0;
    cv_y1 = AB;
}
static uint16_t lvl_col(int lvl, uint16_t c) { static const uint8_t A[5] = {70, 130, 200, 245, 255}; return mix565(c, K_WELL, A[lvl > 4 ? 2 : lvl]); }
static float sq_pos(void)                     /* where the playhead is, in steps */
{
    if (!sq.run || sq.cur < 0)
        return -1.0f;
    return (float)sq.cur + (float)(pl_now - sq.cur_at) / (sq.dur > 1.0f ? sq.dur : 1.0f);
}
static int flash_on(int i) { return sq.run && pl_now - sq_flash[i] < 6000u; }

/* four sliders in a row at the top, over the area: number, label, value, how far (0-1) */
static void seq_dials(const char *const *l, char v[4][16], const float *f, uint16_t c)
{
    int i;
    for (i = 0; i < 4; i++) {                 /* (one canvas each) */
        char n[4] = "1";
        cv_begin(57, 32, K_BG);
        rrect(0, 0, 57, 32, l[i] ? K_CELL : RGB(10, 10, 9), 4);
        if (l[i]) {
            n[0] = (char)('1' + i);
            cv_text(3, -1, &FONT_T, n, K_FAINT);
            cv_text(11, -1, &FONT_T, l[i], K_DIM);
            cv_text_acc(4, 9, &FONT_T, v[i], K_FG);
            slider(5, 23, 47, f[i], c, 0, 0.0f, 0.0f, f[i], 0.0f, K_CELL);
        }
        cv_blit(2u + (uint32_t)i * 59u, 34);
    }
}

/* ------------------------------------------------------------------ the pictures (228 x AH) --- */
#define RX 14                                 /* the roll's left edge: the see-through keyboard is left of it */
static void pic_roll(void)                    /* NOTES: a piano roll of 16 steps */
{
    sq_bank_t *b = cur_bank();
    int L = b->len, start = (sq_cursor / 16) * 16, i, j, lo = 127, hi = 0, any = 0;
    float sw = (float)(224 - RX) / 16.0f, pos = sq_pos();   /* (update 38: from RX, the keyboard left of it) */
    char t[16];
    for (i = 0; i < L; i++)
        for (j = 0; j < b->s[i].cnt; j++) {
            if (b->s[i].n[j] < lo) lo = b->s[i].n[j];
            if (b->s[i].n[j] > hi) hi = b->s[i].n[j];
            any = 1;
        }
    if (!any) { lo = 52; hi = 72; }
    lo -= 2; hi += 2;
    if (hi - lo < 20) { int m = (hi + lo) / 2; lo = m - 10; hi = m + 10; }
    {                                         /* update 38: a lane per semitone, the black keys' darker, and a see-through
                                               * keyboard down the left as the lanes' reference; C named */
        int p, lh = (AH - 10) / (hi - lo);
        for (p = lo; p <= hi; p++) {
            int pc = p % 12, blk = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10, r;
            int32_t yc = AH - 4 - (p - lo) * (AH - 10) / (hi - lo), y0 = yc - lh / 2;
            for (r = 0; r < lh; r++) {
                if (!blk) vf_span(RX, y0 + r, 224 - RX, K_FG, 10u);
                vf_span(1, y0 + r, blk ? 7 : RX - 3, blk ? RGB(10, 10, 10) : K_FG, blk ? 210u : 90u);
            }
            if (pc == 0) {
                vf_span(0, y0 + lh - 1, 224, K_FG, 26u);   /* the octave's line */
                if (lh >= 6) {
                    char nm[6];
                    note_name(p, nm);
                    cv_text(RX + 1, y0 + lh / 2 - 6, &FONT_T, nm, K_FAINT);
                }
            }
        }
    }
    if (sp.lasso) {                           /* the lasso: the steps picked, lit */
        int a = sp.lasso_a < sp.lasso_b ? sp.lasso_a : sp.lasso_b, z = sp.lasso_a < sp.lasso_b ? sp.lasso_b : sp.lasso_a;
        for (j = 0; j < 16; j++)
            if (start + j >= a && start + j <= z) {
                int32_t x = RX + (int32_t)((float)j * sw), r;
                for (r = 2; r < AH - 2; r++)
                    vf_span(x, r, (int32_t)sw, K_GOLD, 40u);
                vf_span(x, 2, (int32_t)sw, K_GOLD, 220u);
                vf_span(x, AH - 3, (int32_t)sw, K_GOLD, 220u);
            }
    }
    for (j = 0; j <= 16; j++) {
        int si = start + j;
        int32_t x = RX + (int32_t)((float)j * sw);
        if (si % 4 == 0) cv_rect(x, 3, 1, AH - 6, si % 16 == 0 ? RGB(43, 42, 39) : RGB(24, 24, 22));
        if (si >= L && j < 16) cv_rect(x, 2, (int32_t)sw, AH - 4, RGB(6, 6, 6));
    }
    for (j = 0; j < 16; j++) {
        int si = start + j;
        const sq_step_t *s;
        int32_t x0;
        if (si >= L) break;
        s = &b->s[si];
        x0 = RX + (int32_t)((float)j * sw + (float)s->nudge / 64.0f * sw);
        if ((s->flags >> 1) & 3u)
            cv_text(x0 + 1, 1, &FONT_T, ((s->flags >> 1) & 3u) == 1u ? "F" : "x", K_GOLD);
        for (i = 0; i < s->cnt; i++) {
            int32_t y = AH - 4 - (s->n[i] - lo) * (AH - 10) / (hi - lo), w = (int32_t)((float)s->len[i] * 0.25f * sw) - 1, sel = si == sq_cursor && i == sp.note_ix;
            int32_t nh = (AH - 10) / (hi - lo) - 1, t0;   /* (update 38: as tall as its lane, 4 to 9 px) */
            int dyn = SQ_LV(s->lvl[i]), art = SQ_ART(s->lvl[i]);
            uint16_t col = lvl_col(dyn, K_SEQC);
            nh = nh < 4 ? 4 : nh > 9 ? 9 : nh;
            t0 = y - nh / 2;
            if (w < (int32_t)(sw * 0.75f)) w = (int32_t)(sw * 0.75f);   /* short notes: at least 3/4 of a step wide */
            if (flash_on(si)) col = mix565(K_FG, col, 90);
            if (dyn == 0) {                   /* PP: an outline */
                cv_rect(x0, t0, w, 1, col); cv_rect(x0, t0 + nh - 1, w, 1, col);
                cv_rect(x0, t0, 1, nh, col); cv_rect(x0 + w - 1, t0, 1, nh, col);
            } else {
                cv_rect(x0, t0, w, nh, col);
                cv_rect(x0, t0, w, 1, mix565(K_FG, col, 70));   /* a light top edge */
            }
            if (dyn == 4) cv_rect(x0, t0 - 1, 3, 2, K_FG);   /* FFF: a mark */
            if (art) {                        /* the articulation, over the note's start (update 39) */
                int32_t ax = x0 + 1, ay = t0 - 4;
                if (art == 1) cv_rect(ax + 1, ay + 1, 2, 2, K_FG);                       /* staccato: a dot */
                else if (art == 2) cv_rect(ax, ay + 2, 5, 1, K_FG);                      /* tenuto: a line */
                else if (art == 3) { cv_line(ax, ay, ax + 4, ay + 2, K_FG); cv_line(ax + 4, ay + 2, ax, ay + 4, K_FG); }   /* accent > */
                else { cv_line(ax, ay + 3, ax + 2, ay - 1, K_FG); cv_line(ax + 2, ay - 1, ax + 4, ay + 3, K_FG); }   /* marcato: a tepee */
            }
            if (s->ratchet > 1) { int k; for (k = 1; k < s->ratchet; k++) cv_rect(x0 + k * (int32_t)sw / s->ratchet, y - 2, 1, 4, K_BG); }
            if (s->flags & 1u) cv_rect(x0 + w - 1, y - 1, 3, 2, K_FG);
            if (sel) {
                cv_rect(x0 - 2, y - 4, w + 4, 1, K_FG); cv_rect(x0 - 2, y + 3, w + 4, 1, K_FG);
                cv_rect(x0 - 2, y - 4, 1, 8, K_FG); cv_rect(x0 + w + 1, y - 4, 1, 8, K_FG);
                note_name(s->n[i], t);
                cv_text_acc(x0 + w + 4 > 200 ? x0 - 26 : x0 + w + 4, y - 7, &FONT_T, t, K_FG);
            }
        }
        if (si == sq_cursor && sp.note_ix < 0 && (plat_ms() / 300u) & 1u) {
            cv_rect(x0, 2, (int32_t)sw, 1, K_DIM); cv_rect(x0, AH - 3, (int32_t)sw, 1, K_DIM);
        }
    }
    if (pos >= (float)start && pos < (float)(start + 16)) {
        int32_t px = RX + (int32_t)((pos - (float)start) * sw);
        cv_rect(px, 2, 1, AH - 4, K_SEQC);
    }
    t[0] = 0;
    int_text(start + 1, t);
    cat(t, "-");
    {
        char u[6];
        int_text(start + 16 < L ? start + 16 : L, u);
        cat(t, u);
        cat(t, "/");
        int_text(L, u);
        cat(t, u);
    }
    cv_text(222 - text_w(&FONT_T, t), 1, &FONT_T, t, K_DIM);
}
static void pic_steps(int spice)              /* PATTERN, SPICE, GEN: the 64 steps as tiles, 4 rows of 16 */
{
    sq_bank_t *b = cur_bank();
    int i, pc = sq.run ? sq.cur : -1;
    for (i = 0; i < SQ_STEPS; i++) {
        const sq_step_t *s = &b->s[i];
        int32_t x = 4 + (i % 16) * 14 - (i % 16) / 4 * 0, y = 3 + (i / 16) * 37;
        int used = i < b->len, beat = i % 4 == 0;
        uint16_t col = !used ? RGB(6, 6, 6) : s->cnt ? (beat ? K_SEQC : mix565(K_SEQC, K_WELL, 150)) : (beat ? RGB(31, 30, 28) : RGB(20, 20, 19));
        if (used && s->cnt && flash_on(i)) col = mix565(K_FG, col, 120);
        rrect(x, y, 12, 34, col, 2);
        if (used && s->cnt && s->chance < 100)
            cv_rect(x, y + 34 - (100 - s->chance) * 34 / 100, 12, (100 - s->chance) * 34 / 100, RGB(40, 20, 30));
        if (used && ((s->flags >> 1) & 3u))
            cv_text(x + 2, y - 1, &FONT_T, ((s->flags >> 1) & 3u) == 1u ? "F" : "x", K_GOLD);
        if (spice && used && s->cnt && b->spice) {
            float sp2 = (float)b->spice * 0.01f;
            if (hash01(b->dice, (uint32_t)i * 4u) < sp2 * 0.3f) cv_rect(x + 5, y + 15, 3, 3, K_WARN);
            else if (hash01(b->dice, (uint32_t)i * 4u + 1u) < sp2 * 0.25f) cv_text(x + 3, y + 11, &FONT_T, hash01(b->dice, (uint32_t)i * 4u + 2u) < 0.5f ? "-" : "+", K_GOLD);
        }
        if (i == sq_cursor) { cv_rect(x - 1, y - 1, 14, 1, K_FG); cv_rect(x - 1, y + 34, 14, 1, K_FG); cv_rect(x - 1, y - 1, 1, 36, K_FG); cv_rect(x + 12, y - 1, 1, 36, K_FG); }
        if (i == pc) cv_rect(x, y + 35, 12, 1, K_SEQC);
    }
}
static void pic_gen(void)
{
    int i, t0 = sp.gen_tool - 3;
    if (t0 < 0) t0 = 0;
    if (t0 > NGEN - 8) t0 = NGEN - 8;
    for (i = 0; i < 8; i++) {
        int k = t0 + i, on = k == sp.gen_tool;
        if (on) { rrect(4, 4 + i * 18, 84, 16, RGB(37, 36, 31), 3); cv_rect(4, 4 + i * 18, 2, 16, K_SEQC); }
        cv_text(10, 4 + i * 18, &FONT_XS, GEN_TOOLS[k], on ? K_FG : K_DIM);
    }
    if (sp.gen_tool == 0) {                   /* EUCLID: the ring of the pattern it will make */
        sq_bank_t *b = cur_bank();
        int L = b->len, k = 1 + sp.gen_a * (L - 1) / 100, rot = sp.gen_b * (L - 1) / 100;
        char t[12], u[6];
        for (i = 0; i < L; i++) {
            int hit = (i * k) % L < k, j = (i + rot) % L;
            float a = (float)j / (float)L * TAU - 1.5708f;
            int32_t x = 156 + (int32_t)(fm_cosf(a) * 56.0f), y = AH / 2 + (int32_t)(fm_sinf(a) * 56.0f);
            if (hit) rrect(x - 3, y - 3, 7, 7, K_SEQC, 3);
            else cv_rect(x - 1, y - 1, 3, 3, K_FAINT);
        }
        int_text(k, t);
        cat(t, "/");
        int_text(L, u);
        cat(t, u);
        cv_text(156 - text_w(&FONT_XS, t) / 2, AH / 2 - 8, &FONT_XS, t, K_FG);
    } else {
        cv_text(100, 56, &FONT_T, "ALGO RIGHT: MAKE IT", K_DIM);
        cv_text(100, 70, &FONT_T, "ALGO LEFT: PUT IT BACK", K_DIM);
        cv_text(100, 84, &FONT_T, "IN SEL'S SCALE AND KEY", K_DIM);
    }
}
static void pic_banks(void)
{
    int b, i, j;
    for (b = 0; b < SQ_BANKS; b++) {
        int32_t x = 4 + b * 27;
        int cur = b == sq.bank, q = b == sq.queue && (plat_ms() / 250u) & 1u, lo = 127, hi = 0;
        char t[4] = "A";
        sq_bank_t *bk = &sqb[b];
        rrect(x, 4, 25, AH - 8, cur ? RGB(42, 32, 38) : RGB(20, 20, 19), 4);
        if (cur || q) { uint16_t e = cur ? K_SEQC : K_FG; cv_rect(x, 4, 25, 1, e); cv_rect(x, AH - 5, 25, 1, e); cv_rect(x, 4, 1, AH - 8, e); cv_rect(x + 24, 4, 1, AH - 8, e); }
        for (i = 0; i < bk->len; i++)
            for (j = 0; j < bk->s[i].cnt; j++) { if (bk->s[i].n[j] < lo) lo = bk->s[i].n[j]; if (bk->s[i].n[j] > hi) hi = bk->s[i].n[j]; }
        if (hi < lo + 6) hi = lo + 6;
        for (i = 0; i < bk->len; i++)
            for (j = 0; j < bk->s[i].cnt; j++)
                cv_rect(x + 3 + i * 19 / bk->len, AH - 24 - (bk->s[i].n[j] - lo) * (AH - 44) / (hi - lo), 2, 2, cur ? K_SEQC : mix565(K_SEQC, K_BG, 140));
        if (cur && sq.run && sq.cur >= 0) cv_rect(x + 3 + sq.cur * 19 / bk->len, 16, 1, AH - 36, mix565(K_SEQC, K_BG, 180));
        t[0] = (char)('A' + b);
        cv_text(x + 13 - text_w(&FONT_XS, t) / 2, AH - 22, &FONT_XS, t, cur ? K_SEQC : K_DIM);
        if (sq_bank_empty(b)) cv_text(x + 11, 5, &FONT_T, ".", K_DIM);
        else { char u[4]; int_text(bk->len, u); cv_text(x + 13 - text_w(&FONT_T, u) / 2, 5, &FONT_T, u, K_DIM); }
    }
}
static void pic_song(void)
{
    int r, tot = 0, empty = 1;
    int32_t x = 4;
    char t[40], u[6];
    for (r = 0; r < 16; r++) { tot += sqs.bank[r] >= 0 ? sqs.rep[r] : 1; if (sqs.bank[r] >= 0) empty = 0; }
    t[0] = 0;
    cat(t, "JAM: ");
    int_text(sq.njam, u);
    cat(t, u);
    cat(t, " PASSES   ALGO LEFT: JAM TO SONG");
    cv_text(4, 0, &FONT_T, t, K_DIM);
    for (r = 0; r < 16; r++) {
        int32_t w = (int32_t)((float)(sqs.bank[r] >= 0 ? sqs.rep[r] : 1) * 220.0f / (float)tot);
        int on = sq.song_on && r == sq.song_row;
        uint16_t c = sqs.bank[r] < 0 ? RGB(12, 12, 11) : on ? mix565(K_SEQC, K_BG, 190) : mix565(K_SEQC, K_BG, 60);
        if (w < 2) w = 2;
        rrect(x, 16, w - 1, AH - 40, c, 2);
        if (sqs.bank[r] >= 0 && w > 9) {
            char bn[2] = "A";
            bn[0] = (char)('A' + sqs.bank[r]);
            cv_text(x + w / 2 - 4, AH / 2 - 14, &FONT_XS, bn, on ? K_BG : K_FG);
            if (sqs.trn[r] && w > 14) {
                u[0] = 0;
                if (sqs.trn[r] > 0) cat(u, "+");
                int_text(sqs.trn[r], u + (sqs.trn[r] > 0));
                cv_text(x + w / 2 - text_w(&FONT_T, u) / 2, AH / 2 + 8, &FONT_T, u, K_GOLD);
            }
        }
        if (r == sp.song_cur) { cv_rect(x, 14, w - 1, 1, K_FG); cv_rect(x, AH - 23, w - 1, 1, K_FG); }
        x += w;
    }
    if (empty) cv_text(10, AH - 18, &FONT_T, "EMPTY: K2 GIVES A ROW A BANK", K_DIM);
}
static void pic_mseq(void)
{
    int i, l, row = 0;
    float sw = 220.0f / 16.0f;
    cv_rect(4, 50, 220, 1, K_FAINT);
    for (i = 0; i < 16; i++) {
        int v = sqs.ms[sp.ms_lane][i];
        int32_t x = 4 + (int32_t)((float)i * sw), h = v * 44 / 100;
        uint16_t c = i == ms_cur ? K_LFO : mix565(K_LFO, K_WELL, 110);
        if (h > 0) cv_rect(x + 1, 50 - h, (int32_t)sw - 2, h, c);
        else if (h < 0) cv_rect(x + 1, 51, (int32_t)sw - 2, -h, c);
        else cv_rect(x + 1, 50, (int32_t)sw - 2, 1, c);
        if (i == sp.ms_cur && (plat_ms() / 300u) & 1u) { cv_rect(x, 2, (int32_t)sw, 1, K_FG); cv_rect(x, 97, (int32_t)sw, 1, K_FG); }
        if (i == ms_cur) cv_rect(x + 1, 99, (int32_t)sw - 2, 2, K_FG);
    }
    for (l = 0; l < 4; l++) {
        char t[2] = "1";
        if (l == sp.ms_lane) continue;
        t[0] = (char)('1' + l);
        cv_text(4, 104 + row * 15, &FONT_T, t, K_DIM);
        for (i = 0; i < 16; i++) {
            int v = sqs.ms[l][i] < 0 ? -sqs.ms[l][i] : sqs.ms[l][i];
            cv_rect(14 + i * 13, 108 + row * 15, 12, 8, mix565(K_LFO, K_WELL, (uint32_t)(30 + v * 2) * (i == ms_cur ? 1u : 1u)));
        }
        row++;
    }
}

static void seq_area(void)
{
    int b;
    for (b = 0; b < AH / AB; b++) {
        int32_t x, y;
        band(b);
        rrect(0, 0, 228, AH, K_WELL, 4);
        for (x = 12; x < 228; x += 12)
            for (y = 6; y < AH; y += 6)
                cv_pset(x, y, K_GRID);
        if (mn.page == PG_MSEQ) pic_mseq();
        else switch (mn.sub) {
            case SQP_NOTES: pic_roll(); break;
            case SQP_PATTERN: pic_steps(0); break;
            case SQP_SPICE: pic_steps(1); break;
            case SQP_GEN: pic_gen(); break;
            case SQP_BANKS: pic_banks(); break;
            default: pic_song();
        }
        cv_blit(6, (uint32_t)AREA_Y + (uint32_t)AB * (uint32_t)b);
    }
}

/* the page: header, area, dials, help */
static void draw_seq_page(void)
{
    sq_bank_t *b = cur_bank();
    sq_step_t *s = &b->s[sq_cursor < SQ_STEPS ? sq_cursor : 0];
    const char *l[4];
    char v[4][16], t[40], u[8];
    float f[4] = {0, 0, 0, 0};
    const char *state = sq.rec && sq.run ? "REC" : sq.armed ? "REC READY" : sq.run ? "PLAY" : "STOP";
    int i;
    for (i = 0; i < 4; i++) v[i][0] = 0;
    if (mn.page == PG_MSEQ) {
        t[0] = 0;
        cat(t, "LANE ");
        int_text(sp.ms_lane + 1, u); cat(t, u);
        cat(t, " / STEP ");
        int_text(sp.ms_cur + 1, u); cat(t, u);
        header("MSEQ", K_LFO, t, IC_SEQ, 1, 0);
        seq_area();
        l[0] = "VALUE"; l[1] = "SHAPE"; l[2] = "RATE"; l[3] = "SMOOTH";
        {
            int val = sqs.ms[sp.ms_lane][sp.ms_cur];
            if (val >= 0) cat(v[0], "+");
            int_text(val, v[0] + (val >= 0));
            f[0] = (float)(val + 100) / 200.0f;
        }
        cat(v[1], MS_SHAPES[sp.ms_shape]); f[1] = (float)sp.ms_shape / 9.0f;
        cat(v[2], MS_RATES[sqs.ms_rate]); f[2] = (float)sqs.ms_rate / 6.0f;
        int_text(sqs.ms_smooth[sp.ms_lane], v[3]); f[3] = (float)sqs.ms_smooth[sp.ms_lane] / 100.0f;
        seq_dials(l, v, f, K_LFO);
        help("SELECT: STEP   ALGO: LANE   SAVE: KEEP");
        return;
    }
    t[0] = 0;
    {
        char bn[2] = "A";
        bn[0] = (char)('A' + sq.bank);
        cat(t, bn);
    }
    cat(t, " ");
    cat(t, state);
    if (mn.sub == SQP_NOTES || mn.sub == SQP_SPICE) {
        cat(t, " ");
        int_text(sq_cursor + 1, u);
        cat(t, u);
        if (mn.sub == SQP_NOTES && sp.note_ix >= 0 && sp.note_ix < s->cnt) {
            cat(t, " ");
            note_name(s->n[sp.note_ix], u);
            cat(t, u);
        }
    } else if (mn.sub == SQP_BANKS && sq.queue >= 0) {
        char bn[2] = "A";
        bn[0] = (char)('A' + sq.queue);
        cat(t, " > ");
        cat(t, bn);
    } else if (mn.sub == SQP_SONG) {
        cat(t, sq.song_on ? " SONG ON" : " SONG OFF");
    }
    header(SQP_NAME[mn.sub], sq.armed || (sq.rec && sq.run) ? K_WARN : K_SEQC, t, IC_SEQ, SQP_N, mn.sub);
    seq_area();
    switch (mn.sub) {
    case SQP_NOTES: {
        int ok = sp.note_ix >= 0 && sp.note_ix < s->cnt;
        int artm = (in_btn >> B_SEL & 1u) != 0;   /* SEL held: K4 is the articulation (update 39) */
        l[0] = "NOTE"; l[1] = "PITCH"; l[2] = "LENGTH"; l[3] = artm ? "ARTIC" : "DYNAMICS";
        if (ok) {
            int_text(sp.note_ix + 1, v[0]); cat(v[0], "/"); int_text(s->cnt, u); cat(v[0], u);
            f[0] = (float)(sp.note_ix + 1) / (float)s->cnt;
            note_name(s->n[sp.note_ix], v[1]); f[1] = (float)(s->n[sp.note_ix] - 24) / 84.0f;
            int_text(s->len[sp.note_ix] / 4, v[2]);
            if (s->len[sp.note_ix] % 4) { static const char *const Q[4] = {"", ".25", ".5", ".75"}; cat(v[2], Q[s->len[sp.note_ix] % 4]); }
            f[2] = (float)s->len[sp.note_ix] / 64.0f;
            if (artm) { int a = SQ_ART(s->lvl[sp.note_ix]); cat(v[3], SQ_ARTS[a > 4 ? 0 : a]); f[3] = (float)a / 4.0f; }
            else { int d = SQ_LV(s->lvl[sp.note_ix]); cat(v[3], SQ_LEVELS[d > 4 ? 2 : d]); f[3] = (float)(d + 1) / 5.0f; }
        } else {
            cat(v[0], s->cnt ? "-" : "EMPTY"); cat(v[1], "-"); cat(v[2], "-"); cat(v[3], "-");
        }
        seq_dials(l, v, f, K_SEQC);
        help(sp.lasso ? "LASSO: K1 STEPS  K2 PITCH  EDIT: DELETE" : artm ? "SEL HELD: K4 ARTICULATION" :
             sq.run ? "SELECT: PAGE  K1 NOTE  EDIT: DELETE" : "PLAY A KEY: WRITE  EDIT: DELETE");
        break;
    }
    case SQP_PATTERN:
        l[0] = "LENGTH"; l[1] = "DIV"; l[2] = "SWING"; l[3] = "GATE";
        int_text(b->len, v[0]); f[0] = (float)b->len / 64.0f;
        cat(v[1], SQ_DIVS[b->div]); f[1] = (float)b->div / 9.0f;
        int_text(b->swing, v[2]); f[2] = (float)b->swing / 100.0f;
        int_text(b->gate, v[3]); f[3] = (float)b->gate / 100.0f;
        seq_dials(l, v, f, K_SEQC);
        help("SELECT: STEP   PLAY: START / STOP   SAVE: KEEP");
        break;
    case SQP_SPICE:
        l[0] = "SPICE"; l[1] = "DICE"; l[2] = "CHANCE"; l[3] = "FILL";
        int_text(b->spice, v[0]); f[0] = (float)b->spice / 100.0f;
        int_text(b->dice, v[1]); f[1] = (float)b->dice / 99.0f;
        int_text(s->chance, v[2]); f[2] = (float)s->chance / 100.0f;
        { static const char *const FL[3] = {"-", "FILL", "NO FILL"}; cat(v[3], FL[(s->flags >> 1) & 3u]); f[3] = (float)((s->flags >> 1) & 3u) / 2.0f; }
        seq_dials(l, v, f, K_SEQC);
        help("SPICE: RESTS, OCTAVES, FORCE; DICE: ANOTHER");
        break;
    case SQP_GEN: {
        int L = b->len;
        l[0] = "TOOL"; l[1] = sp.gen_tool == 0 ? "PULSES" : "A"; l[2] = sp.gen_tool == 0 ? "ROTATE" : "B"; l[3] = "SEED";
        cat(v[0], GEN_TOOLS[sp.gen_tool]); f[0] = (float)sp.gen_tool / (float)(NGEN - 1);
        if (sp.gen_tool == 0) { int_text(1 + sp.gen_a * (L - 1) / 100, v[1]); int_text(sp.gen_b * (L - 1) / 100, v[2]); }
        else { int_text(sp.gen_a, v[1]); int_text(sp.gen_b, v[2]); }
        f[1] = (float)sp.gen_a / 100.0f; f[2] = (float)sp.gen_b / 100.0f;
        int_text(sp.gen_seed, v[3]); f[3] = (float)sp.gen_seed / 99.0f;
        seq_dials(l, v, f, K_SEQC);
        help("ALGO RIGHT: APPLY   ALGO LEFT: UNDO");
        break;
    }
    case SQP_BANKS:
        l[0] = "BANK"; l[1] = "ACTION"; l[2] = "TARGET"; l[3] = "SWITCH";
        v[0][0] = (char)('A' + (sq.queue >= 0 ? sq.queue : sq.bank)); v[0][1] = 0; f[0] = (float)(sq.queue >= 0 ? sq.queue : sq.bank) / 7.0f;
        cat(v[1], BANK_ACTS[sp.bank_act]); f[1] = (float)sp.bank_act / 3.0f;
        v[2][0] = (char)('A' + sp.bank_tgt); v[2][1] = 0; f[2] = (float)sp.bank_tgt / 7.0f;
        cat(v[3], sp.now ? "NOW" : "LOOP END"); f[3] = sp.now ? 1.0f : 0.0f;
        seq_dials(l, v, f, K_SEQC);
        help("SELECT: BANK   ALGO RIGHT: DO THE ACTION");
        break;
    default:
        l[0] = "ROW"; l[1] = "BANK"; l[2] = "REPEATS"; l[3] = "TRANSP";
        int_text(sp.song_cur + 1, v[0]); f[0] = (float)sp.song_cur / 15.0f;
        if (sqs.bank[sp.song_cur] < 0) cat(v[1], "END"); else { v[1][0] = (char)('A' + sqs.bank[sp.song_cur]); v[1][1] = 0; }
        f[1] = (float)(sqs.bank[sp.song_cur] + 1) / 8.0f;
        int_text(sqs.rep[sp.song_cur], v[2]); f[2] = (float)sqs.rep[sp.song_cur] / 16.0f;
        if (sqs.trn[sp.song_cur] > 0) cat(v[3], "+");
        int_text(sqs.trn[sp.song_cur], v[3] + (sqs.trn[sp.song_cur] > 0)); f[3] = (float)(sqs.trn[sp.song_cur] + 12) / 24.0f;
        seq_dials(l, v, f, K_SEQC);
        help("SELECT: ROW   ALGO RIGHT: SONG ON / OFF");
    }
}

/* hold SEQ: the 16 white keys as steps, the held step's values beside them */
static void draw_seq_layer(void)
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS;
    sq_bank_t *b = cur_bank();
    char t[48], u[8];
    int r, w, pc = sq.run ? sq.cur : -1;
    (void)WK;
    cv_begin(240, 16, K_SEQC);
    t[0] = 0;
    cat(t, "SEQ STEPS   HOLD ONE + KNOBS   OCT: PAGE");
    cv_text(6, 1, &FONT_T, t, K_BG);
    t[0] = 0;
    int_text(sp.layer_page * 16 + 1, t);
    cat(t, "-");
    int_text(sp.layer_page * 16 + 16, u);
    cat(t, u);
    cv_text(234 - text_w(&FONT_T, t), 1, &FONT_T, t, K_BG);
    cv_blit(0, 10);
    for (r = 0; r < 4; r++) {
        cv_begin(240, 36, K_BG);
        for (w = 0; w < 4; w++) {
            int i = sp.layer_page * 16 + r * 4 + w;
            int32_t x = 6 + w * 37;
            const sq_step_t *s = &b->s[i];
            uint16_t col = i >= b->len ? RGB(9, 9, 9) : s->cnt ? (i % 4 == 0 ? K_SEQC : mix565(K_SEQC, K_BG, 150)) : RGB(20, 20, 19);
            if (i < b->len && s->cnt && flash_on(i)) col = mix565(K_FG, col, 110);
            rrect(x, 1, 34, 33, col, 4);
            if (i == sp.held) { cv_rect(x, 1, 34, 1, K_FG); cv_rect(x, 33, 34, 1, K_FG); cv_rect(x, 1, 1, 33, K_FG); cv_rect(x + 33, 1, 1, 33, K_FG); }
            if (i == pc) cv_rect(x + 2, 31, 30, 2, K_FG);
            int_text(i + 1, t);
            cv_text(x + 17 - text_w(&FONT_XS, t) / 2, 3, &FONT_XS, t, s->cnt && i % 4 == 0 ? K_BG : K_FG);
            if (s->cnt) {
                if (s->cnt >= 3) chord_name(s->n, s->cnt, t);
                else note_name(s->n[0], t);
                while (text_w_acc(&FONT_T, t) > 32 && t[1]) { int k = 0; while (t[k]) k++; t[k - 1] = 0; }
                cv_text_acc(x + 17 - text_w_acc(&FONT_T, t) / 2, 19, &FONT_T, t, i % 4 == 0 ? K_BG : K_FG);
            }
            if (s->ratchet > 1) { int k; for (k = 0; k < s->ratchet; k++) cv_rect(x + 3 + k * 4, 29, 2, 2, K_FG); }
        }
        if (r == 0 || r == 1 || r == 2 || r == 3) {   /* the held step's values, one a row */
            static const char *const LB[4] = {"K1 NOTE", "K2 DYNAMICS", "K3 RATCHET", "K4 NUDGE"};
            const sq_step_t *s = sp.held >= 0 ? &b->s[sp.held] : 0;
            rrect(158, 1, 78, 33, K_CELL, 4);
            cv_text(162, -1, &FONT_T, LB[r], K_DIM);
            t[0] = 0;
            if (!s || !s->cnt) cat(t, s ? "EMPTY" : "-");
            else if (r == 0) note_name(s->n[0], t);
            else if (r == 1) cat(t, SQ_LEVELS[SQ_LV(s->lvl[0]) > 4 ? 2 : SQ_LV(s->lvl[0])]);
            else if (r == 2) { cat(t, "X"); int_text(s->ratchet, t + 1); }
            else { if (s->nudge >= 0) cat(t, "+"); int_text(s->nudge, t + (s->nudge >= 0)); }
            cv_text_acc(162, 13, &FONT_XS, t, K_FG);
        }
        cv_blit(0, 28u + (uint32_t)r * 40u);
    }
    help("TAP: ON WITH THE LAST NOTES PLAYED / OFF");
}


/* ------------------------------------------------------------------ editing --- */
static void scale_note_k(int R, int k, int *out)  /* the page's scaleNote: SEL's scale, degree k above R */
{
    int n;
    const uint8_t *d = sdeg(&n);
    *out = R + 12 * idiv(k, n) + d[fmodi(k, n)];
}
static uint32_t gen_rng;
static float gen_rand(void) { gen_rng = gen_rng * 1664525u + 1013904223u; return (float)(gen_rng >> 8) / 16777216.0f; }
static void gen_set(int i, const int *notes, int n, int lvl, int len4, int slide)
{
    sq_step_t *s = &cur_bank()->s[i % SQ_STEPS];
    int j;
    sq_blank_step(s);
    for (j = 0; j < n && j < SQ_NOTES; j++) {
        int x = notes[j] < 12 ? 12 : notes[j] > 120 ? 120 : notes[j];
        s->n[j] = (uint8_t)x;
        s->len[j] = (uint8_t)(len4 < 1 ? 1 : len4 > 255 ? 255 : len4);
        s->lvl[j] = (uint8_t)lvl;
    }
    s->cnt = (uint8_t)(n < SQ_NOTES ? n : SQ_NOTES);
    if (slide) s->flags |= 1u;
}
static void gen_apply(void)
{
    sq_bank_t *b = cur_bank();
    int L = b->len, i, key = CI(C_KEY), N, bass = 36 + key, mid = 48 + key, x[4];
    int g4[5] = {1, 1, 2, 3, 4}, gate = g4[sp.gen_b * 4 / 100];
    float A = (float)sp.gen_a * 0.01f, B = (float)sp.gen_b * 0.01f;
    sdeg(&N);
    sp_undo = *b;
    sp.undo_ok = share_undo = 1;
    gen_rng = (uint32_t)(sp.gen_seed * 7919 + sp.gen_tool * 101) | 1u;
    if (sp.gen_tool != 0 && sp.gen_tool < 12)
        for (i = 0; i < SQ_STEPS; i++) sq_blank_step(&b->s[i]);
    switch (sp.gen_tool) {
    case 0: {                                 /* EUCLID: the steps there keep their notes, the new hits take the first */
        int k = 1 + sp.gen_a * (L - 1) / 100, rot = sp.gen_b * (L - 1) / 100, fb = -1;
        const sq_step_t *old = sp_undo.s;     /* (the bank as it was: sp_undo) */
        for (i = 0; i < L; i++) if (fb < 0 && b->s[i].cnt) fb = i;
        for (i = 0; i < L; i++) {
            int j = (i + rot) % L, hit = (i * k) % L < k;
            if (!hit) { b->s[j].cnt = 0; continue; }
            if (old[j].cnt) b->s[j] = old[j];
            else if (fb >= 0) b->s[j] = old[fb];
            else { x[0] = mid; gen_set(j, x, 1, 2, 2, 0); }
        }
        break;
    }
    case 1: { int dd = 0; for (i = 0; i < L; i++) if (gen_rand() < 0.25f + A * 0.7f) { dd += (int)((gen_rand() * 2.0f - 1.0f) * (1.0f + B * 5.0f)); dd = dd < -N ? -N : dd > 2 * N ? 2 * N : dd; scale_note_k(mid, dd, x); gen_set(i, x, 1, gen_rand() < 0.3f ? 3 : 2, gen_rand() < 0.12f ? 8 : gen_rand() < 0.5f ? 2 : 1, 0); } break; }
    case 2: { static const int D[9] = {0, 0, 0, 0, 2, 4, 7, -1, 1}; for (i = 0; i < L; i++) if (gen_rand() < 0.45f + A * 0.5f) { int sl = gen_rand() < B * 0.6f, dg = D[(int)(gen_rand() * 9.0f) % 9]; scale_note_k(bass, dg == 7 ? N : dg, x); if (gen_rand() < 0.2f) x[0] += 12; gen_set(i, x, 1, gen_rand() < 0.3f ? 4 : 2, sl ? 4 : gen_rand() < 0.5f ? 1 : 2, sl); } break; }
    case 3: case 4: { int up[12], nu = 0, o, k, arr[24], na; int oct = 1 + (int)(A * 2.0f + 0.5f); for (o = 0; o < oct; o++) for (k = 0; k < 3; k++) scale_note_k(mid, k * 2 + o * N, &up[nu++]); na = 0; for (k = 0; k < nu; k++) arr[na++] = up[k]; if (sp.gen_tool == 4) for (k = nu - 2; k >= 1; k--) arr[na++] = up[k]; for (i = 0; i < L; i++) { x[0] = arr[i % na]; gen_set(i, x, 1, i % 4 ? 2 : 3, gate, 0); } break; }
    case 5: { static const int V[3][16] = {{0, 4, 7, 4, 0, 4, 9, 4, 0, 4, 7, 4, 2, 4, 9, 4}, {0, 7, 2, 7, 4, 7, 2, 7, 0, 7, 2, 7, 5, 7, 4, 7}, {0, 2, 4, 7, 9, 7, 4, 2, 0, 2, 4, 7, 11, 7, 4, 2}}; int vv = (int)(A * 3.0f); vv = vv > 2 ? 2 : vv; for (i = 0; i < L; i++) { scale_note_k(bass + 12, V[vv][i % 16], x); gen_set(i, x, 1, i % 2 ? 2 : 3, gate, 0); } break; }
    case 6: for (i = 0; i < L; i++) if (gen_rand() < 0.6f + A * 0.4f) { x[0] = bass + (i % 2 ? 12 : 0); gen_set(i, x, 1, i % 2 ? 2 : 3, gate, 0); } break;
    case 7: for (i = 2; i < L; i += 4) { int k; for (k = 0; k < 3; k++) scale_note_k(mid, k * 2, &x[k]); gen_set(i, x, A < 0.5f ? 1 : 3, 3, gate, 0); } break;
    case 8: for (i = 0; i < L; i++) if (i % 4 && gen_rand() < 0.6f + A * 0.4f) { x[0] = bass; gen_set(i, x, 1, i % 4 == 1 ? 3 : 2, gate, 0); } break;
    case 9: { static const int H[6] = {0, 3, 6, 10, 12, 14}; int n2, k; for (n2 = 0; n2 < 6; n2++) if (H[n2] < L && (n2 < 4 || gen_rand() < A)) { for (k = 0; k < (B > 0.5f ? 4 : 3); k++) scale_note_k(mid, (n2 % 2 ? 4 : 0) + k * 2, &x[k]); gen_set(H[n2], x, B > 0.5f ? 4 : 3, 3, 1, 0); } break; }
    case 10: { static const int P[4][4] = {{0, 5, 3, 4}, {0, 3, 4, 0}, {5, 3, 0, 4}, {0, 4, 5, 3}}; int pv = (int)(A * 4.0f), span = L / 4 < 1 ? 1 : L / 4, n2, k; pv = pv > 3 ? 3 : pv; for (n2 = 0; n2 < 4; n2++) if (n2 * span < L) { for (k = 0; k < (B > 0.5f ? 4 : 3); k++) scale_note_k(mid, P[pv][n2] + k * 2, &x[k]); gen_set(n2 * span, x, B > 0.5f ? 4 : 3, 2, span * 4, 0); } break; }
    case 11: { static const int GL[4] = {1, 1, 2, 3}; for (i = 0; i < L; i++) if (gen_rand() < 0.4f + A * 0.6f) { x[0] = mid; gen_set(i, x, 1, gen_rand() < 0.5f ? 3 : 2, GL[(int)(gen_rand() * (2.0f + B * 2.0f)) & 3], 0); } break; }
    case 12: case 13: { int r = sp.gen_tool == 12 ? 1 : L - 1; for (i = 0; i < L; i++) b->s[(i + r) % L] = sp_undo.s[i]; break; }
    case 14: for (i = 0; i < L / 2; i++) { sq_step_t t2 = b->s[i]; b->s[i] = b->s[L - 1 - i]; b->s[L - 1 - i] = t2; } break;
    case 15: if (L * 2 > SQ_STEPS) { app_say("64 STEPS", "AT MOST"); return; } for (i = 0; i < L; i++) b->s[L + i] = b->s[i]; b->len = (uint8_t)(L * 2); break;
    case 16: b->len = (uint8_t)(L / 2 < 1 ? 1 : L / 2); break;
    default: for (i = 0; i < SQ_STEPS; i++) sq_blank_step(&b->s[i]);
    }
    sq_touch();
    app_say(GEN_TOOLS[sp.gen_tool], "ALGO LEFT: UNDO");
}
static void bank_action(void)
{
    sq_bank_t *b = cur_bank();
    int i;
    sp_undo = *b;
    sp.undo_ok = share_undo = 1;
    switch (sp.bank_act) {
    case 0:
        if (sp.bank_tgt == sq.bank) { app_say("PICK ANOTHER", "BANK"); return; }
        sqb[sp.bank_tgt] = *b;
        sq_dirty |= (uint8_t)(1u << sp.bank_tgt);
        app_say("COPIED", "");
        break;
    case 1: { int L = b->len; for (i = 0; i < SQ_STEPS; i++) sq_blank_step(&b->s[i]); b->len = (uint8_t)L; app_say("BANK", "CLEARED"); break; }
    case 2: if (b->len * 2 > SQ_STEPS) { app_say("64 STEPS", "AT MOST"); return; } for (i = 0; i < b->len; i++) b->s[b->len + i] = b->s[i]; b->len = (uint8_t)(b->len * 2); app_say("LENGTH", "X2"); break;
    default: b->len = (uint8_t)(b->len / 2 < 1 ? 1 : b->len / 2); app_say("LENGTH", "HALF");
    }
    sq_touch();
}
static void select_bank(int i)
{
    if (sq.song_on) { sq.song_on = 0; sq.trn = 0; }
    if (!sq.run || sp.now) {
        sq.bank = i;
        if (sq_cursor >= sqb[i].len) sq_cursor = 0;
    } else {
        sq.queue = i == sq.bank ? -1 : i;
    }
    ui.redraw = 1;
}
static void ms_shape_apply(void)              /* the page's shapeLane */
{
    int8_t *v = sqs.ms[sp.ms_lane];
    int i;
    int8_t t[16];
    for (i = 0; i < 16; i++) t[i] = v[i];
    for (i = 0; i < 16; i++) {
        float f = (float)i / 15.0f, x = 0.0f;
        switch (sp.ms_shape) {
        case 1: x = f * 2.0f - 1.0f; break;
        case 2: x = 1.0f - f * 2.0f; break;
        case 3: x = 1.0f - fm_fabsf((float)i / 7.5f - 1.0f) * 2.0f; break;
        case 4: x = fm_sinf((float)i / 16.0f * TAU); break;
        case 5: x = i % 8 < 4 ? 1.0f : -1.0f; break;
        case 6: x = (float)((int)(rnd() * 17.0f) - 8) / 8.0f; break;
        case 7: x = 0.0f; break;
        case 8: x = -(float)t[i] * 0.01f; break;
        case 9: x = (float)t[(i + 15) % 16] * 0.01f; break;
        default: return;
        }
        v[i] = (int8_t)(x * 100.0f);
    }
}
static void ms_apply(void)                    /* MSEQ's smoothing into the engine (after a preset load too) */
{
    int l;
    for (l = 0; l < 4; l++)
        rk_set_ext_slew(l, 1.0f - (float)sqs.ms_smooth[l] * 0.01f * 0.995f);
}

/* SELECT, ALGORITHM, K1-K4 on a sequencer page */
/* the lasso's steps moved d steps (as far as the pattern allows; what they move over is replaced, update 39) */
static void seq_lasso_move(int d)
{
    sq_bank_t *b = cur_bank();
    int a = sp.lasso_a < sp.lasso_b ? sp.lasso_a : sp.lasso_b, z = sp.lasso_a < sp.lasso_b ? sp.lasso_b : sp.lasso_a, n, i;
    sq_step_t tmp[SQ_CLIP];
    d = d > 0 ? 1 : -1;
    n = z - a + 1;
    if (n > SQ_CLIP || a + d < 0 || z + d >= b->len)
        return;
    for (i = 0; i < n; i++) { tmp[i] = b->s[a + i]; b->s[a + i].cnt = 0; }
    for (i = 0; i < n; i++) b->s[a + d + i] = tmp[i];
    sp.lasso_a += d;
    sp.lasso_b += d;
    sq_cursor += d;
    sq_cursor = sq_cursor < 0 ? 0 : sq_cursor >= b->len ? b->len - 1 : sq_cursor;
    sq_touch();
}
static void seq_lasso_pitch(int d)                /* every note in the lasso a semitone a click */
{
    sq_bank_t *b = cur_bank();
    int a = sp.lasso_a < sp.lasso_b ? sp.lasso_a : sp.lasso_b, z = sp.lasso_a < sp.lasso_b ? sp.lasso_b : sp.lasso_a, i, j;
    for (i = a; i <= z && i < b->len; i++)
        for (j = 0; j < b->s[i].cnt; j++) {
            int v = b->s[i].n[j] + d;
            b->s[i].n[j] = (uint8_t)(v < 12 ? 12 : v > 120 ? 120 : v);
        }
    sq_touch();
}
static void seq_encoders(void)
{
    sq_bank_t *b = cur_bank();
    sq_step_t *s = &b->s[sq_cursor < SQ_STEPS ? sq_cursor : 0];
    int32_t e[6];
    int k, d;
    e[0] = plat_enc(EN_SELECT);
    e[1] = plat_enc(EN_ALGO);
    for (k = 0; k < 4; k++)
        e[2 + k] = plat_enc(EN_K1 + k);
    for (k = 0; k < 6; k++)
        if (e[k])
            ui.redraw = 1;
    if (mn.page == PG_MSEQ) {
        if (e[0]) sp.ms_cur = (sp.ms_cur + (e[0] > 0 ? 1 : 15)) % 16;
        if (e[1]) { sp.ms_lane = (sp.ms_lane + (e[1] > 0 ? 1 : 3)) % 4; sp.ms_shape = 0; }
        if (e[2]) { int v = sqs.ms[sp.ms_lane][sp.ms_cur] + (int)e[2] * 2; sqs.ms[sp.ms_lane][sp.ms_cur] = (int8_t)(v < -100 ? -100 : v > 100 ? 100 : v); }
        if (e[3]) { sp.ms_shape = sp.ms_shape + (e[3] > 0 ? 1 : -1); sp.ms_shape = sp.ms_shape < 0 ? 0 : sp.ms_shape > 9 ? 9 : sp.ms_shape; ms_shape_apply(); }
        if (e[4]) { int v = sqs.ms_rate + (e[4] > 0 ? 1 : -1); sqs.ms_rate = (uint8_t)(v < 0 ? 0 : v > 6 ? 6 : v); }
        if (e[5]) { int v = sqs.ms_smooth[sp.ms_lane] + (int)e[5]; sqs.ms_smooth[sp.ms_lane] = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v); ms_apply(); }
        return;
    }
    if (e[0]) {                               /* SELECT: the sequencer's pages (update 39) */
        mn.sub = sp.seq_sub = (mn.sub + (e[0] > 0 ? 1 : SQP_N - 1)) % SQP_N;
        sp.lasso = 0;
        e[0] = 0;
        ui.clear = 1;
    }
    switch (mn.sub) {
    case SQP_NOTES:
        if (sp.lasso && !(in_btn >> B_SEL & 1u) && (e[2] || e[3])) {   /* the lasso: K1 moves it by steps, K2 its pitch */
            if (e[2]) seq_lasso_move((int)e[2]);
            if (e[3]) seq_lasso_pitch((int)e[3]);
            break;
        }
        if (e[2] && (in_btn >> B_SEL & 1u)) {   /* SEL held + K1: the lasso, a step a click from where it started */
            if (!sp.lasso) { sp.lasso = 1; sp.lasso_a = sq_cursor; }
            d = sq_cursor + (int)e[2];
            sq_cursor = d < 0 ? 0 : d >= b->len ? b->len - 1 : d;
            sp.lasso_b = sq_cursor;
            sp.note_ix = -1;
            mn.sel_long = 1;                  /* (SEL let go now is no tap) */
            mn.sel_n = 0;
            e[2] = 0;
        }
        if (e[1] || e[2]) sp.lasso = 0;       /* moving on: the lasso goes */
        if (e[1]) { sq_cursor = (sq_cursor + (e[1] > 0 ? 4 : b->len * 4 - 4)) % b->len; sp.note_ix = b->s[sq_cursor].cnt ? 0 : -1; }
        s = &b->s[sq_cursor];
        if (e[2]) {                           /* K1: the next / previous note, across the steps */
            d = e[2] > 0 ? 1 : -1;
            sp.note_ix += d;
            if (sp.note_ix >= s->cnt || sp.note_ix < 0) {
                int n;
                for (n = 1; n <= b->len; n++) {
                    int c = (sq_cursor + d * n + b->len * 2) % b->len;
                    if (b->s[c].cnt) { sq_cursor = c; sp.note_ix = d > 0 ? 0 : b->s[c].cnt - 1; break; }
                }
                if (n > b->len) sp.note_ix = -1;
            }
            s = &b->s[sq_cursor];
        }
        if (sp.note_ix >= 0 && sp.note_ix < s->cnt) {
            int j = sp.note_ix;
            if (e[3]) { int v = s->n[j] + (e[3] > 0 ? 1 : -1); s->n[j] = (uint8_t)(v < 12 ? 12 : v > 120 ? 120 : v); sq_touch(); }
            if (e[4]) { int v = s->len[j] + (int)e[4]; s->len[j] = (uint8_t)(v < 1 ? 1 : v > 255 ? 255 : v); sq_touch(); }
            if (e[5] && (in_btn >> B_SEL & 1u)) {   /* SEL held: the articulation (only EDIT deletes, update 39) */
                int a = SQ_ART(s->lvl[j]) + (e[5] > 0 ? 1 : -1);
                s->lvl[j] = SQ_MKLVL(SQ_LV(s->lvl[j]), a < 0 ? 0 : a > 4 ? 4 : a);
                mn.sel_long = 1;              /* (SEL let go now is no tap) */
                mn.sel_n = 0;
                sq_touch();
            } else if (e[5]) {                /* the dynamics, PP to FFF */
                int v = SQ_LV(s->lvl[j]) + (e[5] > 0 ? 1 : -1);
                s->lvl[j] = SQ_MKLVL(v < 0 ? 0 : v > 4 ? 4 : v, SQ_ART(s->lvl[j]));
                sq_touch();
            }
        }
        break;
    case SQP_PATTERN: case SQP_SPICE:             /* (ALGORITHM moves the cursor: SELECT turns the pages) */
        if (e[1]) sq_cursor = (sq_cursor + (e[1] > 0 ? 1 : b->len - 1)) % b->len;
        if (mn.sub == SQP_PATTERN) {
            if (e[2]) { int v = b->len + (int)e[2]; b->len = (uint8_t)(v < 1 ? 1 : v > SQ_STEPS ? SQ_STEPS : v); }
            if (e[3]) { int v = b->div + (e[3] > 0 ? 1 : -1); b->div = (uint8_t)(v < 0 ? 0 : v > 9 ? 9 : v); }
            if (e[4]) { int v = b->swing + (int)e[4]; b->swing = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v); }
            if (e[5]) { int v = b->gate + (int)e[5]; b->gate = (uint8_t)(v < 5 ? 5 : v > 100 ? 100 : v); }
        } else {
            if (e[2]) { int v = b->spice + (int)e[2]; b->spice = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v); }
            if (e[3]) { int v = b->dice + (e[3] > 0 ? 1 : -1); b->dice = (uint8_t)(v < 0 ? 0 : v > 99 ? 99 : v); }
            if (e[4]) { int v = s->chance + (int)e[4]; s->chance = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v); }
            if (e[5]) { int f = ((s->flags >> 1) & 3u) + (e[5] > 0 ? 1 : 2); s->flags = (uint8_t)((s->flags & ~6u) | (uint8_t)((f % 3) << 1)); }
        }
        if (sq_cursor >= b->len) sq_cursor = 0;
        if (e[2] || e[3] || e[4] || e[5]) sq_touch();
        break;
    case SQP_GEN:
        if (e[1] > 0) gen_apply();
        if (e[1] < 0 && sp.undo_ok && share_undo) { *b = sp_undo; sp.undo_ok = 0; sq_touch(); app_say("PUT", "BACK"); }
        if (e[2]) { sp.gen_tool += e[2] > 0 ? 1 : -1; sp.gen_tool = sp.gen_tool < 0 ? 0 : sp.gen_tool >= NGEN ? NGEN - 1 : sp.gen_tool; }
        if (e[3]) { sp.gen_a += (int)e[3]; sp.gen_a = sp.gen_a < 0 ? 0 : sp.gen_a > 100 ? 100 : sp.gen_a; }
        if (e[4]) { sp.gen_b += (int)e[4]; sp.gen_b = sp.gen_b < 0 ? 0 : sp.gen_b > 100 ? 100 : sp.gen_b; }
        if (e[5]) { sp.gen_seed += e[5] > 0 ? 1 : -1; sp.gen_seed = sp.gen_seed < 0 ? 0 : sp.gen_seed > 99 ? 99 : sp.gen_seed; }
        break;
    case SQP_BANKS:
        if (e[2]) { int c = sq.queue >= 0 ? sq.queue : sq.bank, dd = e[2]; c += dd > 0 ? 1 : -1; select_bank(c < 0 ? 0 : c > 7 ? 7 : c); }
        if (e[1] > 0) bank_action();
        if (e[1] < 0 && sp.undo_ok && share_undo) { *b = sp_undo; sp.undo_ok = 0; sq_touch(); app_say("PUT", "BACK"); }
        if (e[3]) { sp.bank_act += e[3] > 0 ? 1 : -1; sp.bank_act = sp.bank_act < 0 ? 0 : sp.bank_act > 3 ? 3 : sp.bank_act; }
        if (e[4]) { sp.bank_tgt += e[4] > 0 ? 1 : -1; sp.bank_tgt = sp.bank_tgt < 0 ? 0 : sp.bank_tgt > 7 ? 7 : sp.bank_tgt; }
        if (e[5]) sp.now = e[5] > 0;
        break;
    default:
        if (e[2]) { int dd = e[2]; sp.song_cur += dd > 0 ? 1 : -1; sp.song_cur = sp.song_cur < 0 ? 0 : sp.song_cur > 15 ? 15 : sp.song_cur; }
        if (e[1] > 0) {
            int r, any = 0;
            for (r = 0; r < 16; r++) any |= sqs.bank[r] >= 0;
            if (!any) app_say("SONG", "IS EMPTY");
            else { sq.song_on = !sq.song_on; sq.queue = -1; if (sq.song_on) sq_song_row(sq_first_row()); else sq.trn = 0; app_say("SONG", sq.song_on ? "ON" : "OFF"); }
        }
        if (e[1] < 0) {                       /* the jam (the banks of the passes played) as rows */
            int r = 0, i;
            if (!sq.njam) app_say("PLAY SOME", "BANKS FIRST");
            else {
                for (i = 0; i < 16; i++) { sqs.bank[i] = -1; sqs.rep[i] = 1; sqs.trn[i] = 0; }
                for (i = 0; i < sq.njam && r < 16; i++) {
                    if (r > 0 && sqs.bank[r - 1] == sq.jam[i] && sqs.rep[r - 1] < 16) sqs.rep[r - 1]++;
                    else { sqs.bank[r] = (int8_t)sq.jam[i]; sqs.rep[r] = 1; r++; }
                }
                app_say("SONG FROM", "THE JAM");
            }
        }
        if (e[3]) { int v = sqs.bank[sp.song_cur] + (e[3] > 0 ? 1 : -1); sqs.bank[sp.song_cur] = (int8_t)(v < -1 ? -1 : v > 7 ? 7 : v); }
        if (e[4]) { int v = sqs.rep[sp.song_cur] + (e[4] > 0 ? 1 : -1); sqs.rep[sp.song_cur] = (uint8_t)(v < 1 ? 1 : v > 16 ? 16 : v); }
        if (e[5]) { int v = sqs.trn[sp.song_cur] + (e[5] > 0 ? 1 : -1); sqs.trn[sp.song_cur] = (int8_t)(v < -12 ? -12 : v > 12 ? 12 : v); }
    }
}

/* NOTES (update 38): EDIT deletes the note picked, or every note in the lasso */
static void seq_delete(void)
{
    sq_bank_t *b = cur_bank();
    sq_step_t *s = &b->s[sq_cursor];
    char t[16];
    if (sp.lasso) {
        int a = sp.lasso_a < sp.lasso_b ? sp.lasso_a : sp.lasso_b, z = sp.lasso_a < sp.lasso_b ? sp.lasso_b : sp.lasso_a, i;
        for (i = a; i <= z && i < b->len; i++)
            b->s[i].cnt = 0;
        sp.lasso = 0;
        sp.note_ix = -1;
        t[0] = 0;
        int_text(z - a + 1, t);
        cat(t, z > a ? " STEPS CLEARED" : " STEP CLEARED");
        app_say("DELETED", t);
    } else if (sp.note_ix >= 0 && sp.note_ix < s->cnt) {
        int k, j = sp.note_ix;
        for (k = j; k < s->cnt - 1; k++) { s->n[k] = s->n[k + 1]; s->len[k] = s->len[k + 1]; s->lvl[k] = s->lvl[k + 1]; }
        s->cnt--;
        sp.note_ix = s->cnt ? (j < s->cnt ? j : s->cnt - 1) : -1;
        app_say("NOTE", "DELETED");
    } else {
        app_say("NOTHING PICKED", "K1 PICKS A NOTE");
        return;
    }
    sq_touch();
}
/* NOTES: hold SAVE: copy the lasso's steps; with no lasso, paste what was copied from the cursor's step on */
static void seq_copy_paste(void)
{
    sq_bank_t *b = cur_bank();
    char t[16];
    int i;
    if (sp.lasso) {
        int a = sp.lasso_a < sp.lasso_b ? sp.lasso_a : sp.lasso_b, z = sp.lasso_a < sp.lasso_b ? sp.lasso_b : sp.lasso_a;
        sq_nclip = 0;
        for (i = a; i <= z && i < b->len && sq_nclip < SQ_CLIP; i++)
            sq_clip[sq_nclip++] = b->s[i];
        sp.lasso = 0;
        t[0] = 0;
        int_text(sq_nclip, t);
        cat(t, sq_nclip > 1 ? " STEPS" : " STEP");
        app_say("COPIED", t);
    } else if (sq_nclip) {
        for (i = 0; i < sq_nclip && sq_cursor + i < b->len; i++)
            b->s[sq_cursor + i] = sq_clip[i];
        sp.note_ix = b->s[sq_cursor].cnt ? 0 : -1;
        t[0] = 0;
        int_text(i, t);
        cat(t, i > 1 ? " STEPS" : " STEP");
        app_say("PASTED", t);
        sq_touch();
    } else {
        app_say("NOTHING COPIED", "HOLD SEL + TURN K1");
    }
    ui.redraw = 1;
}
static void seq_save(void);
/* SAVE on a sequencer page: a tap saves the sequencer (when let go); held on NOTES: copy / paste */
static void seq_save_btn(uint32_t press, uint32_t btn)
{
    if (press >> B_SAVE & 1u) {
        sp.save_at = ms_stamp();
        sp.save_long = 0;
    }
    if (sp.save_at && (btn >> B_SAVE & 1u) && !sp.save_long && plat_ms() - sp.save_at >= HOLD_MS) {
        sp.save_long = 1;
        if (mn.page == PG_SEQ && mn.sub == SQP_NOTES)
            seq_copy_paste();
    }
    if (sp.save_at && !(btn >> B_SAVE & 1u)) {
        if (!sp.save_long)
            seq_save();
        sp.save_at = 0;
    }
}

/* SEQ, PLAY, REC (on every page); hold SEQ: the layer. The keys while it shows: seq_layer_keys */
static void seq_buttons(uint32_t press, uint32_t btn)
{
    if (press >> B_SEQ & 1u) {
        sp.seq_at = ms_stamp();
        sp.turned = 0;
    }
    if (sp.seq_at && (btn >> B_SEQ & 1u) && !sp.layer && plat_ms() - sp.seq_at >= HOLD_MS) {
        sp.layer = 1;
        sp.held = -1;
        ui.clear = 1;
        ui.redraw = 1;
    }
    if (sp.seq_at && !(btn >> B_SEQ & 1u)) {
        if (sp.layer) {
            sp.layer = 0;
            ui.clear = 1;
        } else if (mn.page != PG_SEQ) {       /* to the sequencer: the page it was on */
            go(PG_SEQ);
            mn.sub = sp.seq_sub;
        } else {
            mn.sub = sp.seq_sub = (mn.sub + 1) % SQP_N;
        }
        sp.seq_at = 0;
        ui.redraw = 1;
    }
    if (press >> B_PLAY & 1u) {
        if (sq.run)                           /* STOP also ends what HOLD keeps: the arp's notes and latched chords */
            rc_push(PLE_ARP_CLEAR, 0, 0.0f, 0.0f);
        sq.req = sq.run ? 2 : 1;
        ui.redraw = 1;
    }
    if (press >> B_REC & 1u) {
        if (sq.run) { sq.rec = !sq.rec; app_say(sq.rec ? "REC" : "REC OFF", ""); }
        else { sq.armed = !sq.armed; app_say(sq.armed ? "REC READY" : "REC", sq.armed ? "THE FIRST NOTE STARTS" : "OFF"); }
        ui.redraw = 1;
    }
}
/* the layer: the white keys are this page's 16 steps (1 when it took the keys) */
static int seq_layer_keys(uint32_t keys, uint32_t prev)
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS;
    sq_bank_t *b = cur_bank();
    int w;
    if (!sp.layer)
        return 0;
    for (w = 0; w < 16; w++) {
        uint32_t bit = 1u << WK[w];
        int i = sp.layer_page * 16 + w;
        if ((keys & bit) && !(prev & bit)) {
            sp.held = i;
            sp.turned = 0;
            ui.redraw = 1;
        } else if (!(keys & bit) && (prev & bit) && sp.held == i) {
            if (!sp.turned && i < b->len) {   /* a tap: on with the last notes, or off */
                sq_step_t *s = &b->s[i];
                if (s->cnt) s->cnt = 0;
                else {
                    int gate4 = (b->gate * 4 + 50) / 100;
                    sq_blank_step(s);
                    sq_write(s, sq_last, sq_nlast, gate4 < 1 ? 1 : gate4);
                }
                sq_touch();
            }
            sp.held = -1;
            ui.redraw = 1;
        }
    }
    return 1;
}
static void seq_layer_encoders(void)
{
    sq_bank_t *b = cur_bank();
    int k;
    for (k = 0; k < 4; k++) {
        int32_t e = plat_enc(EN_K1 + k);
        sq_step_t *s;
        int j;
        if (!e || sp.held < 0)
            continue;
        s = &b->s[sp.held];
        if (!s->cnt) {
            int gate4 = (b->gate * 4 + 50) / 100;
            sq_write(s, sq_last, sq_nlast, gate4 < 1 ? 1 : gate4);
        }
        for (j = 0; j < s->cnt; j++) {
            if (k == 0) { int v = s->n[j] + (e > 0 ? 1 : -1); s->n[j] = (uint8_t)(v < 12 ? 12 : v > 120 ? 120 : v); }
            if (k == 1) { int v = SQ_LV(s->lvl[j]) + (e > 0 ? 1 : -1); s->lvl[j] = SQ_MKLVL(v < 0 ? 0 : v > 4 ? 4 : v, SQ_ART(s->lvl[j])); }
        }
        if (k == 2) { int v = s->ratchet + (e > 0 ? 1 : -1); s->ratchet = (uint8_t)(v < 1 ? 1 : v > 4 ? 4 : v); }
        if (k == 3) { int v = s->nudge + (int)e; s->nudge = (int8_t)(v < -32 ? -32 : v > 31 ? 31 : v); }
        sp.turned = 1;
        sq_touch();
    }
    plat_enc(EN_SELECT);
    plat_enc(EN_ALGO);
}

/* the main loop, each frame: step entry's cursor, keys write the cursor's step on NOTES while stopped */
static void seq_tick(void)
{
    sq_entry_on = mn.page == PG_SEQ && mn.sub == SQP_NOTES && !sq.run && !sp.layer;
    if (sq_entry_done) {
        sq_entry_done = 0;
        sp.note_ix = cur_bank()->s[sq_cursor].cnt ? 0 : -1;
        ui.redraw = 1;
    }
}
static void seq_save(void)
{
    app_say(sq_save_all() ? "SEQUENCER NOT SAVED" : "SEQUENCER SAVED", "");
}
