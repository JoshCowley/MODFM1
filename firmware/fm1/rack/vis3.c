/* SPDX-License-Identifier: MPL-2.0 */
/* FM1 Rack firmware: the wells of the SEL, ARP and GLOBAL pages (menus stage 3), part of app.c after vis.c. After the
 * page's visChord (5b-seq.js), visArp, visTuning, visMidiOut, visMaster and visSystem (5c-tools.js), drawn into the
 * well's 228 x 42 canvas: a keyboard lighting the chord with its name, the keys mapped to the scale, the arp's
 * pattern stepping, a metronome, the velocity curve, each tuning's degrees against equal temperament, MIDI traffic,
 * the EQ's curve and the CPU's history. */
static const char *const NOTE12[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static void note_name(int n, char *t)
{
    t[0] = 0;
    cat(t, NOTE12[pc12(n)]);
    int_text(idiv(n, 12) - 1, t + (NOTE12[pc12(n)][1] ? 2 : 1));
}

/* the page's chordName: the root and quality that explain most of the notes, the bass after a slash */
static int chord_quality(uint16_t iv, char *name)
{
#define H(x) (iv >> (x) & 1u)
    int third = H(4) ? 4 : H(3) ? 3 : 0, hits = 0, size = 0, i;
    uint16_t used = 1u;
    name[0] = 0;
    if (third == 4) {
        int aug = H(8) && !H(7);
        cat(name, aug ? "aug" : "");
        used |= 1u << 4 | 1u << (aug ? 8 : 7);
    } else if (third == 3) {
        int dim = H(6) && !H(7);
        cat(name, dim ? "dim" : "m");
        used |= 1u << 3 | 1u << (dim ? 6 : 7);
    } else if (H(7)) {
        cat(name, H(5) ? "sus4" : H(2) ? "sus2" : "5");
        used |= 1u << 7 | 1u << (H(5) ? 5 : 2);
    } else if (H(5) && H(10)) {
        cat(name, "sus4");
        used |= 1u << 5;
    } else {
        return -999;
    }
    if (H(11)) {
        char t[12] = "";
        used |= 1u << 11;
        if (same(name, "m")) cat(t, "mMaj7");
        else if (!name[0]) cat(t, "maj7");
        else { cat(t, name); cat(t, "M7"); }
        name[0] = 0;
        cat(name, t);
    } else if (H(10)) {
        used |= 1u << 10;
        if (!name[0]) cat(name, "7");
        else if (same(name, "dim")) { name[0] = 0; cat(name, "m7b5"); }
        else if (same(name, "sus4")) { name[0] = 0; cat(name, "7sus4"); }
        else cat(name, "7");
    } else if (H(9) && third) {
        used |= 1u << 9;
        if (same(name, "dim")) { name[0] = 0; cat(name, "dim7"); }
        else cat(name, "6");
    }
    if (H(2) && third) {                      /* a 9th: "7" becomes "9", or add9 */
        char *p = name;
        used |= 1u << 2;
        while (*p && *p != '7') p++;
        if (*p == '7') *p = '9';
        else cat(name, "add9");
    }
    for (i = 0; i < 12; i++) {
        size += H(i);
        hits += H(i) && (used >> i & 1u);
    }
    return hits * 2 - (size - hits) * 3 + (third ? 1 : 0);
#undef H
}
static void chord_name(const uint8_t *notes, int n, char *out)
{
    uint16_t pcs = 0;
    int i, r, lo = 127, best = -1000, br = -1;
    char q[16], bq[16];
    out[0] = 0;
    if (!n) { cat(out, "-"); return; }
    if (n == 1) { note_name(notes[0], out); return; }
    for (i = 0; i < n; i++) {
        pcs |= (uint16_t)(1u << pc12(notes[i]));
        if (notes[i] < lo) lo = notes[i];
    }
    for (r = 0; r < 12; r++) {
        uint16_t iv = 0;
        int sc;
        if (!(pcs >> r & 1u)) continue;
        for (i = 0; i < 12; i++)
            if (pcs >> i & 1u) iv |= (uint16_t)(1u << pc12(i - r));
        sc = chord_quality(iv, q);
        if (sc > best || (sc == best && r == pc12(lo))) {
            best = sc;
            br = r;
            bq[0] = 0;
            cat(bq, q);
        }
    }
    if (br < 0) {                             /* nothing it knows: the notes */
        for (i = 0; i < n && i < 3; i++) { cat(out, NOTE12[pc12(notes[i])]); if (i < 2 && i < n - 1) cat(out, " "); }
        return;
    }
    cat(out, NOTE12[br]);
    cat(out, bq);
    if (br != pc12(lo)) { cat(out, "/"); cat(out, NOTE12[pc12(lo)]); }
}

/* -------------------------------------------------- SEL: a keyboard lighting the chord, its name, the scale */
static uint32_t vs_seq, vs_at;                /* the chord last seen, and when it came */
static void vis_sel_chord(uint16_t c)
{
    uint8_t show[PL_MAXN];
    int n = 0, i, o, key = CI(C_KEY), fresh;
    uint32_t now = plat_ms(), lit_age;
    static const uint8_t WH[7] = {0, 2, 4, 5, 7, 9, 11};
    static const uint8_t BK[5][2] = {{1, 0}, {3, 1}, {6, 3}, {8, 4}, {10, 5}};
    uint16_t pcs = 0;
    char name[24], t[24];
    float age;
    if (pv.chord_seq != vs_seq) { vs_seq = pv.chord_seq; vs_at = now; }
    lit_age = now - vs_at;
    fresh = vs_seq && lit_age < 1500u;
    if (fresh) {
        n = pv.nchord;
        for (i = 0; i < n && i < PL_MAXN; i++) show[i] = pv.chord[i];
    } else if (CI(C_TYPE) != CT_OFF) {
        n = chord_notes(60 + key, 0, 0, 1, show);   /* a preview: the chord on the root */
    }
    for (i = 0; i < n; i++) pcs |= (uint16_t)(1u << pc12(show[i]));
    age = fresh ? (float)lit_age * 0.001f : 9.0f;
    /* two octaves of keys */
    for (o = 0; o < 2; o++)
        for (i = 0; i < 7; i++) {
            int pc = WH[i], x = 4 + (o * 7 + i) * 10, on = pcs >> pc & 1u, sc = in_scale(pc + 60);
            uint32_t g = on ? (uint32_t)(150.0f + 106.0f * fm_expf(-age * 2.5f)) : 0u;
            uint16_t col = on ? mix565(c, K_WELL, g) : sc ? (pc == key ? RGB(59, 53, 36) : RGB(30, 34, 28)) : RGB(16, 16, 16);
            rrect(x, 3, 9, 36, col, 2);
            if (!on && sc)
                cv_rect(x + 3, 33, 3, 3, pc == key ? K_GOLD : RGB(74, 90, 68));
            if (on)
                dot(x + 4, 33, 1, K_FG);
        }
    for (o = 0; o < 2; o++)
        for (i = 0; i < 5; i++) {
            int pc = BK[i][0], x = 4 + (o * 7 + BK[i][1] + 1) * 10 - 3, on = pcs >> pc & 1u, sc = in_scale(pc + 60);
            uint16_t col = on ? mix565(c, K_WELL, 220) : sc ? (pc == key ? RGB(90, 74, 38) : RGB(47, 58, 43)) : RGB(5, 5, 5);
            rrect(x, 3, 6, 20, col, 2);
        }
    /* the name, a ring as each new chord comes */
    if (fresh && age < 0.6f)
        ellipse(189.0f, 18.0f, 12.0f + age * 50.0f, 8.0f + age * 26.0f, mix565(c, K_WELL, (uint32_t)((0.6f - age) * 400.0f)));
    if (fresh || CI(C_TYPE) != CT_OFF)
        chord_name(show, n, name);
    else {
        name[0] = 0;
        cat(name, CI(C_QNT) ? "SCALE" : "SINGLE");
    }
    cv_text(189 - text_w(text_w(&FONT_B, name) > 74 ? &FONT_XS : &FONT_B, name) / 2, 6,
            text_w(&FONT_B, name) > 74 ? &FONT_XS : &FONT_B, name, fresh ? K_FG : CI(C_TYPE) ? mix565(K_FG, K_WELL, 160) : K_DIM);
    t[0] = 0;
    cat(t, NOTE12[key]);
    cat(t, " ");
    cat(t, UI_OPT[UP_SEL[C_SCALE].opt + (CI(C_SCALE) % NSCALE)]);
    cv_text(189 - text_w(&FONT_T, t) / 2, 28, &FONT_T, t, CI(C_QNT) ? c : K_DIM);
    if (n > rc_patch.voices_now && n > 1) {   /* more notes than voices */
        t[0] = 0;
        int_text(rc_patch.voices_now, t);
        cat(t, " VOICES!");
        cv_text(222 - text_w(&FONT_T, t), 0, &FONT_T, t, K_WARN);
    }
}

/* SEL page 2: the FM-1's 27 keys and where each lands (QNT), the zone keys, the strum as a stair */
static void vis_sel_keys(uint16_t c)
{
    int k, oct = set.octave, z = zone_on();
    float strum = set.sel[C_STRUM];
    for (k = 0; k < NKEYS; k++) {
        int note = 53 + k + 12 * oct, pc = pc12(note), black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        int x = 4 + k * 8, q = quantize(note), zk = z && k < 7, held = ui.keys >> k & 1u;
        uint16_t top = zk ? K_GOLD : black ? RGB(40, 40, 38) : RGB(150, 148, 140);
        if (held) top = c;
        cv_rect(x, black ? 3 : 2, 7, black ? 8 : 10, zk ? mix565(K_GOLD, K_WELL, 150) : top);
        if (zk)
            continue;
        if (q < 0) {                          /* WHITE: a black key plays nothing */
            cv_rect(x + 2, 30, 3, 1, K_FAINT);
            continue;
        }
        {
            int tx = 4 + (q - 53 - 12 * oct) * 8;
            uint16_t lc = q == note ? mix565(c, K_WELL, 90) : mix565(K_GOLD, K_WELL, 150);
            tx = tx < 0 ? 0 : tx > 220 ? 220 : tx;
            cv_line(x + 3, 14, tx + 3, 26, lc);
            rrect(tx, 27, 7, 10, pc12(q - CI(C_KEY)) == 0 ? K_GOLD : held ? c : mix565(c, K_WELL, 120), 2);
        }
    }
    if (strum > 0.0f) {                       /* the strum: notes one after another */
        int i;
        for (i = 0; i < 4; i++)
            cv_rect(206 + (int32_t)((float)i * (2.0f + strum * 3.0f)), 36 - i * 3, 2, 3, K_FG);
    }
}

/* -------------------------------------------------- ARP's STYLE pages: the style's bass row, the step playing lit;
 * on LAYER CH and CHORD + ARP, where each layer goes */
static void vis_arp(uint16_t c);
static uint32_t vst_seq, vst_at;
static void vis_style(uint16_t c, int sub)
{
    static const char *const LN[3] = {"CHORD", "BASS", "ARP"};
    int s = AI(A_STYLE), i, L;
    uint32_t now = plat_ms();
    float fl;
    char t[32], m[8];
    if (sub >= 3) {                           /* the layers: name, channel and sent or not, FM-1 or not */
        vf_keep = 1;                          /* (boxes with words in them) */
        for (L = 0; L < 3; L++) {
            int32_t y = 1 + L * 12, ch = AI(A_CHCH + L), loc = lay_local(L), snd = lay_send(L);
            int act = L == L_ARP ? AI(A_MODE) != AM_OFF : L == L_BASS ? s > 0 : 1;
            uint16_t col = act ? (L == 0 ? K_CHD : L == 1 ? c : K_ARP) : K_FAINT;
            rrect(4, y, 50, 11, col, 3);
            cv_text(8, y, &FONT_T, LN[L], K_BG);
            t[0] = 0;
            if (snd) { cat(t, "SENT CH "); int_text(ch, m); cat(t, m); } else cat(t, "NOT SENT");
            cv_text(62, y, &FONT_T, t, snd ? K_FG : K_DIM);
            cv_text(150, y, &FONT_T, loc ? "+ FM-1" : "FM-1 OFF", loc ? K_FG : K_DIM);
        }
        vf_keep = 0;
        if (!GI(G_MOUT))
            cv_text(4, 38, &FONT_T, "GLOBAL > MIDI OUT IS OFF", K_WARN);
        else if (!s)
            cv_text(4, 38, &FONT_T, "BASS: TURN ON A STYLE", K_DIM);
        return;
    }
    if (s < 2) {
        cv_text(114 - text_w(&FONT_XS, s ? "HELD: THE BASS HOLDS THE ROOT" : "STYLE OFF: TURN K1") / 2, 14, &FONT_XS,
                s ? "HELD: THE BASS HOLDS THE ROOT" : "STYLE OFF: TURN K1", K_DIM);
        return;
    }
    {
        const style_t *S = &STYLES[(s - 2) % NSTYLE];
        int n = S->len, cur = st.on ? (int)(pv.st_step % (uint32_t)n) : -1;
        int32_t bw = 220 / n, x0 = 4 + (220 - bw * n) / 2;
        if (pv.st_seq != vst_seq) { vst_seq = pv.st_seq; vst_at = now; }
        fl = 1.0f - (float)(now - vst_at) * 0.004f;
        fl = fl < 0.0f ? 0.0f : fl;
        {                                     /* the bass row: the rhythm shapes the bass only */
            const char *p = S->bass;
            int32_t y = 14;
            uint16_t base = c;
            for (i = 0; i < n; i++) {
                char ch = p[i];
                int hit = ch != '.' && ch != '-';
                int32_t h = ch == 'R' ? 22 : hit ? 16 : ch == '-' ? 5 : 1;
                uint16_t col = i == cur ? mix565(K_FG, base, (uint32_t)(255.0f * fl)) : hit || ch == '-' ? mix565(base, K_WELL, ch == '-' ? 110 : 240) : RGB(29, 29, 27);
                rrect(x0 + i * bw, y + 22 - h, bw > 3 ? bw - 2 : 1, h, col, 1);
            }
        }
        t[0] = 0;
        cat(t, UI_OPT[UP_ARP[A_STYLE].opt + s]);
        cat(t, n == 12 ? "  12/8" : n == 32 ? "  2 BARS" : "");
        if (S->swing > 0.01f) cat(t, "  SWING");
        cv_text(4, 1, &FONT_T, t, K_FG);
        t[0] = 0;
        cat(t, st.on ? "PLAYING" : "HOLD A CHORD");
        cv_text(224 - text_w(&FONT_T, t), 1, &FONT_T, t, st.on ? c : K_DIM);
    }
}

/* -------------------------------------------------- ARP: the pattern, the step it plays, the Turing register */
static uint32_t va_seq, va_at;
static void vis_arp(uint16_t c)
{
    uint8_t sq[PL_SEQ * 2];
    int n, i, lo = 127, hi = 0, mode = AI(A_MODE), top;
    uint32_t now = plat_ms();
    float fl;
    char t[32];
    if (mode == AM_OFF) {
        cv_text(114 - text_w(&FONT_XS, "ARP OFF: TURN K1") / 2, 14, &FONT_XS, "ARP OFF: TURN K1", K_DIM);
        return;
    }
    if (pv.arp_seq != va_seq) { va_seq = pv.arp_seq; va_at = now; }
    fl = 1.0f - (float)(now - va_at) * 0.006f;
    fl = fl < 0.0f ? 0.0f : fl;
    n = pa.count ? arp_sequence(sq) : 0;
    if (mode == AM_CHORD && n) {              /* CHORD: the whole pool each step */
        int k = 0;
        for (i = 0; i < pa.count; i++) k += pa.n[i];
        n = k > 24 ? 24 : k;
    }
    top = mode == AM_TURING ? 13 : 4;
    if (mode == AM_TURING) {                  /* the register: LOOP bits, lit where set */
        int L = AI(A_LOOP) < 1 ? 1 : AI(A_LOOP);
        for (i = 0; i < 16; i++) {
            int on = i < L && ((uint32_t)pa.reg >> (L - 1 - i) & 1u);
            rrect(4 + i * 14, 2, 11, 8, i >= L ? RGB(14, 14, 13) : on ? mix565(c, K_WELL, (uint32_t)(130.0f + 126.0f * fl)) : RGB(29, 29, 27), 2);
        }
    }
    if (!n) {
        t[0] = 0;
        cat(t, UI_OPT[UP_ARP[A_MODE].opt + mode]);
        cat(t, ": HOLD SOME KEYS");
        cv_text(114 - text_w(&FONT_XS, t) / 2, top + 10, &FONT_XS, t, K_DIM);
        return;
    }
    if (n > 24) n = 24;
    for (i = 0; i < n; i++) {
        if (sq[i] < lo) lo = sq[i];
        if (sq[i] > hi) hi = sq[i];
    }
    {
        int span = hi - lo < 12 ? 12 : hi - lo, bw = 220 / n, cur = -1;
        if (mode != AM_RANDOM && mode != AM_TURING && mode != AM_CHORD)
            cur = (int)((pv.step) % (uint32_t)n);
        for (i = 0; i < n; i++) {
            int on = mode == AM_CHORD ? fl > 0.0f : mode == AM_RANDOM || mode == AM_TURING ? (pv.narp && pv.arp[0] == sq[i] && fl > 0.0f) : i == cur;
            int32_t h = 4 + (int32_t)((float)(sq[i] - lo) / (float)span * (float)(38 - top - 6));
            uint16_t col = on ? mix565(K_FG, c, (uint32_t)(255.0f * fl)) : mix565(c, K_WELL, 70);
            rrect(4 + i * bw, 39 - h, bw > 3 ? bw - 2 : 1, h, col, 2);
        }
    }
    t[0] = 0;                                 /* the rate and the tempo it follows */
    cat(t, UI_OPT[UP_ARP[A_RATE].opt + (AI(A_RATE) % 6)]);
    cat(t, " @ ");
    {
        char b[8];
        int_text((int32_t)(tempo_bpm() + 0.5f), b);
        cat(t, b);
    }
    if (mode != AM_TURING)
        cv_text(222 - text_w(&FONT_T, t), 1, &FONT_T, t, K_DIM);
}

/* -------------------------------------------------- GLOBAL */
static void vis_metronome(uint16_t c)         /* SETTINGS: the tempo as a bar of four beats, the clock's source */
{
    float bpm = tempo_bpm(), ph = vis_t() * bpm / 60.0f, f = ph - fm_floorf(ph);
    int beat = (int)fm_floorf(ph) & 3, i, midi = GI(G_CLOCK) == 1, lock = midi && plat_ms() - pv.clock_at < 500u;
    char t[24];
    t[0] = 0;
    int_text((int32_t)(bpm + 0.5f), t);
    cv_text(8, 6, &FONT_B, t, K_FG);
    cv_text(10 + text_w(&FONT_B, t), 14, &FONT_T, "BPM", K_DIM);
    cv_text(8, 26, &FONT_T, midi ? (lock ? "MIDI CLOCK: LOCKED" : "MIDI CLOCK: WAITING") : "INTERNAL CLOCK", lock ? K_OK : midi ? K_GOLD : K_DIM);
    for (i = 0; i < 4; i++) {                 /* the bar's four beats: the one now swells and fades, the first gold */
        int on = i == beat;
        uint16_t col = i == 0 ? K_GOLD : K_FG;
        int32_t x = 112 + i * 30;
        dot(x, 21, 6, mix565(col, K_WELL, 34));
        if (on) {
            dot(x, 21, 9 - (int32_t)(f * 4.0f), mix565(col, K_WELL, (uint32_t)(255.0f * (1.0f - f * 0.7f))));
            if (f < 0.3f)
                ellipse((float)x, 21.0f, 10.0f + f * 30.0f, 10.0f + f * 30.0f, mix565(col, K_WELL, (uint32_t)((0.3f - f) * 500.0f)));
        }
    }
    (void)c;
}
static void vis_velocity(uint16_t c)          /* PLAYING: the velocity curve, the last note's force on it */
{
    int16_t ys[48];
    int i;
    float e = fm_exp2f((0.5f - set.glo[G_VEL]) * 3.0f), lb = set.glo[G_LED];
    uint8_t lv = pv.ring_v[(pv.ring_w - 1u) & 15u];
    for (i = 0; i < 48; i++)
        ys[i] = ycl(38.0f - 34.0f * fm_powf((float)i / 47.0f, e));
    cv_line(0, 38, 94, 4, K_FAINT);
    glow(ys, 48, c);
    if (pv.ring_w) {
        int32_t y = 38 - (int32_t)((float)lv / 127.0f * 34.0f);
        cv_rect(0, y, 96, 1, mix565(K_GOLD, K_WELL, 120));
    }
    cv_text(102, 4, &FONT_T, "SOFT", K_DIM);
    cv_text(102, 28, &FONT_T, "HARD", K_DIM);
    for (i = 0; i < 5; i++)                   /* the lights as they will glow */
        rrect(150 + i * 15, 18, 11, 7, i == 2 ? K_FG : mix565(K_FG, K_WELL, (uint32_t)(lb < 0.2f ? 12.0f : lb < 0.6f ? 40.0f : 90.0f)), 2);
    cv_text(150, 30, &FONT_T, UI_OPT[UP_GLO[G_ANIM].opt + (GI(G_ANIM) % 3)], K_DIM);
}
static void vis_tuning(uint16_t c)            /* TUNING: each degree against equal temperament (+-50 cents) */
{
    int t = GI(G_TUNE) < NTUNE ? GI(G_TUNE) : 0, n = TUNE_N[t], i;
    const float *cents = TUNE_CENTS + TUNE_AT[t];
    float w = 120.0f / (float)n, tm = vis_t();
    char s[32];
    cv_text(6, 2, &FONT_S, UI_OPT[UP_GLO[G_TUNE].opt + t], K_FG);
    s[0] = 0;
    int_text(n, s);
    cat(s, " NOTES  ROOT ");
    cat(s, NOTE12[GI(G_TROOT) % 12]);
    cv_text(6, 18, &FONT_T, s, K_DIM);
    s[0] = 0;
    cat(s, "A4 ");
    int_text(GI(G_A4), s + 3);
    cat(s, " HZ");
    cv_text(6, 29, &FONT_T, s, GI(G_A4) == 440 ? K_DIM : K_GOLD);
    cv_rect(100, 21, 124, 1, K_FAINT);
    for (i = 0; i < n; i++) {
        float d = n == 12 ? cents[i] - (float)i * 100.0f : cents[i] - (float)(int32_t)(cents[i] / 100.0f + 0.5f) * 100.0f;
        int32_t h = (int32_t)(fm_clampf(d, -50.0f, 50.0f) / 50.0f * 16.0f), x = 102 + (int32_t)((float)i * w);
        int32_t wob = (int32_t)(fm_sinf(tm * 2.0f + (float)i) * 0.8f);
        int32_t bw = (int32_t)w - 1 < 1 ? 1 : (int32_t)w - 1;
        if (h == 0) cv_rect(x, 20 + wob, bw, 2, mix565(c, K_WELL, 120));
        else cv_rect(x, (h > 0 ? 21 - h : 21) + wob, bw, h > 0 ? h : -h, mix565(c, K_WELL, 200));
    }
}
static uint32_t vm_count, vm_at;
static void vis_midi(uint16_t c)              /* MIDI: traffic in, what goes out, the clock */
{
    char t[32], n[8];
    uint32_t now = plat_ms();
    int out = GI(G_MOUT), act;
    if (midi_count != vm_count) { vm_count = midi_count; vm_at = now; }
    act = now - vm_at < 150u;
    icon(IC_MIDI, 6, 4, act ? K_OK : K_DIM);
    t[0] = 0;
    cat(t, "IN ");
    cat(t, GI(G_MIDICH) ? "CH " : "OMNI");
    if (GI(G_MIDICH)) { int_text(GI(G_MIDICH), n); cat(t, n); }
    cv_text(24, 3, &FONT_T, t, act ? K_FG : K_DIM);
    t[0] = 0;
    if (out) {
        cat(t, "OUT CH ");
        int_text(GI(G_OUTCH) + 1, n);
        cat(t, n);
        if (out == 2) cat(t, " + CC 21-24");
    } else {
        cat(t, "MIDI OUT OFF");
    }
    cv_text(24, 15, &FONT_T, t, out ? K_FG : K_DIM);
    t[0] = 0;
    if (GI(G_LOCAL)) cat(t, "LOCAL OFF: KEYS ONLY SEND");
    else { cat(t, "BEND +-"); int_text(GI(G_BEND), n); cat(t, n); cat(t, GI(G_PROG) ? "  PROG CHG ON" : "  PROG CHG OFF"); }
    cv_text(24, 27, &FONT_T, t, GI(G_LOCAL) ? K_WARN : K_DIM);
    {                                         /* the last notes in and out, as dots fading */
        int i;
        for (i = 0; i < 8; i++) {
            uint32_t k = (pv.ring_w - 1u - (uint32_t)i) & 15u;
            if (pv.ring_w > (uint32_t)i)
                dot(176 + (pc12(pv.ring_n[k]) * 4) % 48, 6 + (pv.ring_n[k] % 30), 1, mix565(c, K_WELL, (uint32_t)(240 - i * 26)));
        }
    }
}
static void vis_master(uint16_t c)            /* MASTER: the EQ's curve and the compressor */
{
    int16_t ys[80];
    int i;
    float g0 = set.glo[G_LOW] * 12.0f, g1 = set.glo[G_MID] * 10.0f, g2 = set.glo[G_HIGH] * 12.0f, comp = set.glo[G_COMP];
    for (i = 0; i < 80; i++) {
        float lf = 4.32f + (float)i / 79.0f * 9.97f;   /* log2 of 20 Hz .. 20 kHz */
        float db = g0 / (1.0f + fm_exp2f((lf - 7.23f) * 3.0f)) + g1 * fm_expf(-(lf - 9.97f) * (lf - 9.97f) * 1.2f)
                   + g2 / (1.0f + fm_exp2f(-(lf - 12.29f) * 3.0f));
        ys[i] = ycl(21.0f - db * 1.4f);
    }
    cv_rect(0, 21, 160, 1, K_FAINT);
    glow(ys, 80, c);
    cv_text(4, 1, &FONT_T, "LOW", K_DIM);
    cv_text(72, 1, &FONT_T, "MID", K_DIM);
    cv_text(136, 1, &FONT_T, "HIGH", K_DIM);
    rrect(176, 4, 12, 30, RGB(20, 20, 19), 2);
    if (comp > 0.0f)
        rrect(176, 34 - (int32_t)(comp * 30.0f), 12, (int32_t)(comp * 30.0f) + 1, K_GOLD, 2);
    cv_text(194, 4, &FONT_T, "COMP", K_DIM);
    {
        char t[8];
        int_text((int32_t)(comp * 100.0f + 0.5f), t);
        cv_text(194, 18, &FONT_S, t, K_FG);
    }
}
static uint8_t vy_cpu[114];                   /* SYSTEM: the CPU's last 114 readings */
static uint32_t vy_w, vy_at;
static void vis_system(uint16_t c)
{
    int16_t ys[114];
    int i, n = vy_w < 114u ? (int)vy_w : 114;
    uint32_t cpu = plat_cpu_pct(), pk = 0;
    char t[32], m[8];
    if (plat_ms() - vy_at > 120u) {
        vy_at = plat_ms();
        vy_cpu[vy_w % 114u] = (uint8_t)(cpu > 100u ? 100u : cpu);
        vy_w++;
    }
    for (i = 0; i < n; i++) {
        uint8_t v = vy_cpu[(vy_w - (uint32_t)n + (uint32_t)i) % 114u];
        ys[i] = ycl(39.0f - (float)v * 0.3f);
        if (v > pk) pk = v;
    }
    cv_rect(0, 9, 228, 1, mix565(K_WARN, K_WELL, 60));   /* 100 % */
    if (n > 1)
        glow(ys, n, cpu > 85u ? K_WARN : c);
    t[0] = 0;
    cat(t, "CPU ");
    int_text((int32_t)cpu, m);
    cat(t, m);
    cat(t, "%  PEAK ");
    int_text((int32_t)pk, m);
    cat(t, m);
    cat(t, "%");
    cv_text(4, 0, &FONT_T, t, cpu > 85u ? K_WARN : K_FG);
    t[0] = 0;
    cat(t, plat_cpu2_on() ? "2 CORES  " : "1 CORE  ");
    int_text(rk_fm1_status(2), m);
    cat(t, m);
    cat(t, " VOICES FIT");
    cv_text(224 - text_w(&FONT_T, t), 0, &FONT_T, t, K_DIM);
}

static void vis_fixed(int pg, int sub, uint16_t c)
{
    if (pg == PG_SEL) {                       /* the keyboards: drawn solid, as before */
        vf_keep = 2;
        if (sub == 0) vis_sel_chord(c);
        else vis_sel_keys(c);
        vf_keep = 0;
    } else if (pg == PG_ARP) {
        if (sub >= 2) vis_style(c, sub);
        else vis_arp(c);
    } else {
        switch (sub) {
        case 0: vis_metronome(c); break;
        case 1: vis_velocity(c); break;
        case 2: vis_tuning(c); break;
        case 3: vis_midi(c); break;
        case 4: vis_master(c); break;
        default: vis_system(c); break;
        }
    }
}

/* SYSTEM's cells: what is measured, in the cells' look (no knob moves them) */
static void info_cell(int i, const char *label, const char *value, float f, uint16_t c, int warn)
{
    char t[2];
    cv_begin(112, CELL_H, K_BG);
    rrect(0, 0, 112, CELL_H, K_CELL, 4);
    t[0] = (char)('1' + i);
    t[1] = 0;
    cv_text(6, 3, &FONT_T, t, K_FAINT);
    cv_text(16, 3, &FONT_T, label, K_DIM);
    cv_text(6, 17, &FONT_B, value, K_FG);
    vbar(8, 52, 96, 4, f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f, warn && f > 0.9f ? K_WARN : c);
    cv_blit(i & 1 ? 122u : 6u, (uint32_t)CELL_Y(i));
}
static void system_cells(uint16_t c)
{
    char t[16];
    uint32_t cpu = plat_cpu_pct();
    int used = rk_fm1_status(0), all = rk_fm1_status(1);
    t[0] = 0;
    int_text((int32_t)cpu, t);
    cat(t, "%");
    info_cell(0, "CPU", t, (float)cpu / 100.0f, c, 1);
    t[0] = 0;
    int_text(used / 1024, t);
    cat(t, "K");
    info_cell(1, "ARENA", t, all ? (float)used / (float)all : 0.0f, c, 0);
    t[0] = 0;
    int_text(rc_patch.voices_now, t);
    cat(t, " / ");
    {
        char m[4];
        int_text(rk_fm1_status(2), m);
        cat(t, m);
    }
    info_cell(2, "VOICES", t, (float)rc_patch.voices_now / 6.0f, c, 0);
    info_cell(3, "CORES", plat_cpu2_on() ? "2" : plat_cpu2_gave_up() ? "1 (FELL BACK)" : "1", plat_cpu2_on() ? 1.0f : 0.5f, c, 0);
}

/* the home page's well: what plays the keys besides the preset (SEL, ARP, a tuning), as small tabs over the scope */
static void home_chips(void)
{
    char t[24];
    int32_t x = 222;
    vf_keep = 1;
    {                                         /* update 37: a warning when the keys or the arp aren't on the FM-1 */
        int kc = !lay_local(L_CHORD), ka = AI(A_MODE) != AM_OFF && !lay_local(L_ARP);
        if (kc || ka) {
            const char *s = kc && ka ? "KEYS+ARP: FM-1 OFF" : kc ? "KEYS: FM-1 OFF" : "ARP: FM-1 OFF";
            rrect(4, 2, text_w(&FONT_T, s) + 6, 11, K_WARN, 3);
            cv_text(7, 2, &FONT_T, s, K_BG);
        }
    }
    if (GI(G_TUNE)) {
        const char *s = UI_OPT[UP_GLO[G_TUNE].opt + GI(G_TUNE) % NTUNE];
        x -= text_w(&FONT_T, s) + 8;
        rrect(x, 2, text_w(&FONT_T, s) + 6, 11, K_GLO, 3);
        cv_text(x + 3, 2, &FONT_T, s, K_BG);
        x -= 4;
    }
    if (AI(A_STYLE)) {                        /* STYLE: its name */
        const char *s = UI_OPT[UP_ARP[A_STYLE].opt + AI(A_STYLE) % (NSTYLE + 2)];
        x -= text_w(&FONT_T, s) + 8;
        rrect(x, 2, text_w(&FONT_T, s) + 6, 11, K_ARP, 3);
        cv_text(x + 3, 2, &FONT_T, s, K_BG);
        x -= 4;
    }
    if (AI(A_MODE)) {
        t[0] = 0;
        cat(t, "ARP ");
        cat(t, UI_OPT[UP_ARP[A_MODE].opt + AI(A_MODE) % 15]);
        x -= text_w(&FONT_T, t) + 8;
        rrect(x, 2, text_w(&FONT_T, t) + 6, 11, K_ARP, 3);
        cv_text(x + 3, 2, &FONT_T, t, K_BG);
        x -= 4;
    }
    if (CI(C_TYPE) || CI(C_QNT)) {
        t[0] = 0;
        cat(t, NOTE12[CI(C_KEY) % 12]);
        cat(t, " ");
        cat(t, CI(C_TYPE) ? UI_OPT[UP_SEL[C_TYPE].opt + CI(C_TYPE) % 8] : "QNT");
        x -= text_w(&FONT_T, t) + 8;
        rrect(x, 2, text_w(&FONT_T, t) + 6, 11, K_CHD, 3);
        cv_text(x + 3, 2, &FONT_T, t, K_BG);
    }
    vf_keep = 0;
}

/* -------------------------------------------------- OSC: two cycles of its wave at its level, and the mixer's faders */
static void vis_osc(uint16_t c)
{
    const float *mon = rk_monitor(RS_AMP);
    int wave = (int)(rk_get_param(RS_AMP, OSC_P0) + 0.5f), x, oct = (int)rk_get_param(RS_AMP, OSC_P0 + 1);
    int semi = (int)rk_get_param(RS_AMP, OSC_P0 + 2);
    float pw = wave == 3 ? 0.5f : mon[168 + OSC_P0 + 4 - 16], lvl = mon[168 + OSC_P0 + 5 - 16], mainl = mon[168 + OSC_P0 + 6 - 16];
    float amp = 15.0f * (0.25f + 0.75f * lvl), t = vis_t();
    int16_t ys[80];
    char s[24], n[8];
    vz.rng = 4242u + (uint32_t)(t * 12.0f);
    for (x = 0; x < 80; x++) {                /* 160 pixels: two cycles */
        float ph = (float)(x % 40) / 40.0f, y;
        switch (wave) {
        case 0: y = fm_sinf(ph * TAU); break;
        case 1: y = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;
        case 2: y = 2.0f * ph - 1.0f; break;
        case 5: y = rnd() * 2.0f - 1.0f; break;
        default: y = ph < pw ? 1.0f : -1.0f;
        }
        ys[x] = ycl(21.0f - y * amp);
    }
    cv_rect(0, 21, 160, 1, K_MID);
    glow(ys, 80, lvl > 0.0001f ? c : K_DIM);
    s[0] = 0;
    cat(s, UI_OPT[UP_OSC[0].opt + (wave < 0 ? 0 : wave > 5 ? 5 : wave)]);
    if (oct || semi) {
        cat(s, oct < 0 ? " " : " +");
        int_text(oct * 12 + semi, n);
        cat(s, n);
        cat(s, "ST");
    }
    cv_text(4, 1, &FONT_T, s, lvl > 0.0001f ? K_FG : K_DIM);
    if (lvl <= 0.0001f)
        cv_text(4, 31, &FONT_T, "OFF: TURN OSC LVL", K_DIM);
    {                                         /* the mixer: MAIN and OSC as faders */
        static const char *const LB[2] = {"MAIN", "OSC"};
        int k;
        for (k = 0; k < 2; k++) {
            float f = k ? lvl : mainl;
            int32_t fx = 176 + k * 26, h = (int32_t)(f * 28.0f);
            rrect(fx, 4, 10, 30, RGB(20, 20, 19), 2);
            if (h > 0)
                rrect(fx, 34 - h, 10, h, k ? c : K_SRC, 2);
            cv_rect(fx - 2, 33 - h, 14, 2, K_FG);
            cv_text(fx + 5 - text_w(&FONT_T, LB[k]) / 2, 34, &FONT_T, LB[k], K_DIM);
        }
    }
}
