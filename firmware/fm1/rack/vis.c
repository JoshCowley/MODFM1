/* SPDX-License-Identifier: MPL-2.0 */
/* FM1 Rack firmware: the moving band on each page (the well), after the page's visualisers (web/src/3-vis.js), part
 * of app.c. Drawn into the well's canvas (228 x 42) about 30 times a second while the rest of the screen redraws
 * ten times a second. Each one reads the module's parameters as modulated for the last voice (the engine's monitor),
 * the voices' envelopes, the modulators' and LFOs' outputs and the notes played. Simplified from the page: the same
 * pictures, drawn with lines, dots and ellipses. */
#define VW 228
#define VH 42
#define TAU 6.2831853f

static struct {
    uint32_t t0;                              /* the time base (ms) */
    uint32_t pulse_t[8];                      /* the last notes played: when, which, how hard */
    uint8_t pulse_n[8], pulse_v[8], pulse_w;
    int8_t hist[7][114];                      /* MOD A 1-2, MOD B 1-2, LFO 1-3: their last 114 values (x 100) */
    uint8_t hist_w;
    uint32_t hist_at;
    uint32_t rng;
    float at;                                 /* the animation's clock (s): GLOBAL > ANIMATION slows or stops it */
    uint32_t at_ms;
} vz;

static void vis_note(int note, int vel)       /* a note played (keys or MIDI): the drums, Rings and the delay show it */
{
    vz.pulse_t[vz.pulse_w & 7u] = plat_ms();
    vz.pulse_n[vz.pulse_w & 7u] = (uint8_t)note;
    vz.pulse_v[vz.pulse_w & 7u] = (uint8_t)vel;
    vz.pulse_w++;
}
static float vis_t(void)
{
    static const float SPEED[3] = {1.0f, 0.45f, 0.0f};
    uint32_t now = plat_ms();
    int a = GI(G_ANIM);
    vz.at += (float)(now - vz.at_ms) * 0.001f * SPEED[a < 0 ? 0 : a > 2 ? 2 : a];
    vz.at_ms = now;
    return vz.at;
}
static float rnd(void) { vz.rng = vz.rng * 1664525u + 1013904223u; return (float)(vz.rng >> 8) * (1.0f / 16777216.0f); }
static float env_max(const float *mon)
{
    float e = 0.0f;
    int i;
    for (i = 0; i < 6; i++)
        if (mon[16 + i] > e)
            e = mon[16 + i];
    return e;
}

/* the page's glow(): a soft halo above and below, then the line; ys one per 2 pixels across */
static void glow(const int16_t *ys, int n, uint16_t c)
{
    uint16_t halo = mix565(c, K_WELL, 46);
    int i;
    for (i = 1; i < n; i++) {
        cv_line((i - 1) * 2, ys[i - 1] - 1, i * 2, ys[i] - 1, halo);
        cv_line((i - 1) * 2, ys[i - 1] + 1, i * 2, ys[i] + 1, halo);
    }
    for (i = 1; i < n; i++)
        cv_line((i - 1) * 2, ys[i - 1], i * 2, ys[i], c);
}
static int16_t ycl(float y) { return (int16_t)(y < 1.0f ? 1.0f : y > (float)(VH - 2) ? (float)(VH - 2) : y); }
static void dot(int32_t x, int32_t y, int32_t r, uint16_t c)
{
    rrect(x - r, y - r, 2 * r + 1, 2 * r + 1, c, r);
}
/* an ellipse's outline (the ripples) */
static void ellipse(float cx, float cy, float rx, float ry, uint16_t c)
{
    int i;
    float px = cx + rx, py = cy;
    for (i = 1; i <= 32; i++) {
        float a = (float)i * (TAU / 32.0f), x = cx + rx * fm_cosf(a), y = cy + ry * fm_sinf(a);
        cv_line((int32_t)px, (int32_t)py, (int32_t)x, (int32_t)y, c);
        px = x;
        py = y;
    }
}
static void vlabel(const char *s) { cv_text(6, 2, &FONT_T, s, K_DIM); }
static float P(const float *mon, int i) { return i < 16 ? mon[i] : mon[168 + i - 16]; }

static void vis_plaits(const float *mon, uint16_t c)
{
    int model = (int)(P(mon, 0) + 0.5f), x, k;
    float har = P(mon, 1), tim = P(mon, 2), mor = P(mon, 3), e = env_max(mon), amp = 0.22f + 0.78f * e, t = vis_t();
    int16_t ys[115];
    if (model >= 16 && model <= 18) {         /* swarm, noise, particles: a field of dots */
        vz.rng = 12345u;
        for (k = 0; k < 50; k++) {
            float s = rnd(), px = rnd(), py = rnd();
            float xx = px + t * (0.02f + s * 0.1f) * (1.0f + tim * 3.0f);
            xx -= (float)(int)xx;
            dot((int32_t)(xx * VW), ycl(21.0f + fm_sinf(py * TAU + t * (1.0f + har * 4.0f) + s * 9.0f) * 15.0f * amp),
                mor > 0.5f ? 1 : 0, mix565(c, K_WELL, (uint32_t)(60.0f + 196.0f * s * amp)));
        }
    } else if (model >= 21) {                 /* drums: a burst on each note */
        int any = 0;
        for (k = 0; k < 8; k++) {
            float age = (float)(plat_ms() - vz.pulse_t[k]) * 0.001f, kk;
            if (age > 1.2f || !vz.pulse_t[k])
                continue;
            any = 1;
            kk = fm_expf(-age * (3.0f + (1.0f - P(mon, 5)) * 6.0f)) * (float)vz.pulse_v[k] / 127.0f;
            for (x = 0; x < 115; x++) {
                float u = (float)x / 114.0f;
                ys[x] = ycl(21.0f - fm_sinf(u * TAU * (2.0f + har * 6.0f) * (1.0f + (1.0f - u) * mor * 3.0f)) * fm_expf(-u * 3.0f) * kk * 17.0f);
            }
            glow(ys, 115, c);
        }
        if (!any)
            cv_rect(4, 21, 220, 1, mix565(c, K_WELL, 100));
    } else {                                  /* the rest: a wave the timbre folds and the morph turns to a saw */
        float cycles = 1.5f + har * 3.5f, fold = 1.0f + tim * 2.6f, norm = 1.0f / fm_sinf(fold < 1.57f ? fold : 1.57f);
        for (x = 0; x < 115; x++) {
            float ph = (float)x / 114.0f * cycles * TAU - t * 3.0f, y, saw;
            y = fm_sinf(ph) + har * 0.55f * fm_sinf(2.0f * ph + t) + har * har * 0.35f * fm_sinf(3.0f * ph - t * 0.5f);
            y = fm_sinf(y * fold) * norm;
            saw = ph / TAU;
            saw = (saw - fm_floorf(saw)) * 2.0f - 1.0f;
            y = y * (1.0f - mor * 0.55f) + saw * mor * 0.55f;
            ys[x] = ycl(21.0f - y * amp * 16.0f);
        }
        glow(ys, 115, c);
    }
    if (model >= 0 && model < 24)
        vlabel(UI_OPT[UP_PLAITS[0].opt + model]);
}

static void vis_braids(const float *mon, uint16_t c)
{
    static const uint8_t BITS[] = {2, 3, 4, 6, 8, 12, 16}, STEP[] = {12, 6, 3, 2, 1, 1};
    int model = (int)(P(mon, 0) + 0.5f), bi = (int)(P(mon, 4) + 0.5f), ri = (int)(P(mon, 5) + 0.5f), s, x;
    float tim = P(mon, 1), col = P(mon, 2), drift = P(mon, 3), e = env_max(mon), amp = 0.25f + 0.75f * e, t = vis_t();
    int bits = BITS[bi < 0 ? 0 : bi > 6 ? 6 : bi], step = STEP[ri < 0 ? 0 : ri > 5 ? 5 : ri];
    float levels = (float)(1 << (bits < 6 ? bits : 6));
    static const uint16_t TINT[3] = {0, RGB(255, 217, 160), RGB(255, 143, 90)};
    int16_t ys[115];
    char lab[24];
    for (s = 2; s >= 0; s--) {
        float held = 0.0f;
        for (x = 0; x < 115; x++) {
            if (x % step == 0) {
                float ph = (float)x / 114.0f * TAU * (1.5f + tim * 2.0f) - t * (2.2f + (float)s * drift * 2.0f) + (float)s * (0.6f + col * 1.5f);
                held = fm_sinf(ph) * (0.45f + 0.55f * tim) * fm_cosf(ph * 0.5f + (float)s);
                if (bits <= 6)
                    held = (float)(int)(held * levels * 0.5f + (held >= 0 ? 0.5f : -0.5f)) / (levels * 0.5f);
            }
            ys[x] = ycl(21.0f - held * amp * 16.0f);
        }
        glow(ys, 115, s ? mix565(TINT[s], K_WELL, 150) : c);
    }
    lab[0] = 0;
    if (model >= 0 && model < 47)
        cat(lab, UI_OPT[UP_BRAIDS[0].opt + model]);
    {
        char b[8];
        int_text(bits, b);
        cat(lab, "  ");
        cat(lab, b);
        cat(lab, "BIT");
    }
    vlabel(lab);
}

static void vis_rings(const float *mon, uint16_t c)
{
    static const char *const NAME[] = {"MODAL", "SYMPATHETIC", "STRING", "FM VOICE", "SYMP QUANT", "STRING+VERB"};
    int model = (int)(P(mon, 0) + 0.5f), rings = 1 + (int)(P(mon, 1) * 4.0f + 0.5f), k, j;
    float bright = P(mon, 2), damp = P(mon, 3), pos = P(mon, 4), life = 0.6f + damp * 4.0f, sp = 4.0f + P(mon, 1) * 6.0f;
    float cx = 114.0f + (pos - 0.5f) * 114.0f;
    for (k = 1; k <= rings + 1; k++)
        ellipse(cx, 21.0f, (float)k * (sp + 4.0f), (float)k * (sp + 4.0f) * 0.42f, mix565(c, K_WELL, 26));
    for (j = 0; j < 8; j++) {
        float age = (float)(plat_ms() - vz.pulse_t[j]) * 0.001f, a;
        float rx = cx + (float)(vz.pulse_n[j] % 7 - 3) * 6.0f;
        if (!vz.pulse_t[j] || age >= life)
            continue;
        a = (1.0f - age / life) * (float)vz.pulse_v[j] / 127.0f;
        for (k = 0; k < rings; k++) {
            float rad = age * (24.0f + bright * 40.0f) - (float)k * sp;
            if (rad <= 0.0f)
                continue;
            ellipse(rx, 21.0f, rad, rad * 0.42f, mix565(k ? RGB(191, 245, 238) : c, K_WELL, (uint32_t)(256.0f * a * (1.0f - (float)k / (float)(rings + 1)))));
        }
    }
    if (model >= 0 && model < 6)
        vlabel(NAME[model]);
}

static float fmag(int type, float r, float q, float reso)
{
    float d = fm_sqrtf((1.0f - r * r) * (1.0f - r * r) + (r / q) * (r / q));
    if (type == 3) {
        float q1 = 0.5f + reso * reso * 4.0f, q2 = 0.6f + reso * reso * 20.0f;
        float d1 = fm_sqrtf((1.0f - r * r) * (1.0f - r * r) + (r / q1) * (r / q1)), d2 = fm_sqrtf((1.0f - r * r) * (1.0f - r * r) + (r / q2) * (r / q2));
        return 1.0f / (d1 * d2);
    }
    if (type == 4) return fm_fabsf(1.0f - r * r) / d;
    if (type == 5) return (1.0f + r * r) / d * 0.5f;
    if (type == 1) return (r / q) / d * (q < 3.0f ? q : 3.0f);
    if (type == 2) return r * r / d;
    return 1.0f / d;
}
static void vis_filter(const float *mon, uint16_t c)
{
    static const char *const NAME[] = {"LP", "BP", "HP", "LP24", "NOTCH", "PEAK"};
    int type = (int)(P(mon, 0) + 0.5f), x;
    float cut = P(mon, 1), reso = P(mon, 2), q = 0.5f + reso * reso * 25.0f;
    float fc = 440.0f * fm_exp2f((cut * 120.0f - 30.0f + P(mon, 4) * env_max(mon) * 60.0f - 9.0f) / 12.0f);
    int16_t ys[115];
    if (type < 0) type = 0;
    if (type > 5) type = 5;
    fc = fc < 20.0f ? 20.0f : fc > 20000.0f ? 20000.0f : fc;
    for (x = 0; x < 115; x++) {
        float f = 20.0f * fm_exp2f((float)x / 114.0f * 9.9658f), m = fmag(type, f / fc, q, reso);
        float db = 6.0206f * fm_log2f(m + 1e-6f);
        db = db < -36.0f ? -36.0f : db > 18.0f ? 18.0f : db;
        ys[x] = ycl(6.0f + (18.0f - db) / 54.0f * 34.0f);
        if (x & 1)
            cv_rect(x * 2, ys[x], 2, VH - ys[x], mix565(c, K_WELL, 22));
    }
    glow(ys, 115, c);
    vlabel(NAME[type]);
}

/* a modulator's or LFO's recent output, scrolling (the page's trace()) */
static void trace(int h, uint16_t c, int bip)
{
    int16_t ys[115];
    int i;
    for (i = 0; i < 114; i++) {
        float v = (float)vz.hist[h][(vz.hist_w + i) % 114u] * 0.01f;
        if (!bip)
            v = v * 2.0f - 1.0f;
        ys[i] = ycl(21.0f - v * 17.0f);
    }
    ys[114] = ys[113];
    glow(ys, 115, c);
    dot(226, ys[113], 1, c);
}

static void vis_grids(const float *mon, uint16_t c)
{
    int inst, st, step = (int)mon[47] & 31;
    for (inst = 0; inst < 3; inst++)
        for (st = 0; st < 32; st++) {
            float lv = mon[64 + inst * 32 + st];
            uint16_t col = st == step ? K_FG : mix565(c, K_WELL, (uint32_t)(30.0f + 226.0f * lv));
            int32_t h = 2 + (int32_t)(lv * 8.0f);
            cv_rect(4 + st * 7, 8 + inst * 11 + (10 - h), 5, h, col);
        }
    vlabel("BD SD HH");
}

static void vis_delay(const float *mon, uint16_t c)
{
    float fb = P(mon, 1), mix = P(mon, 3), t = vis_t();
    int k, n = 1 + (int)(fb * 10.0f);
    uint32_t last = vz.pulse_t[(vz.pulse_w - 1u) & 7u];
    float age = last ? (float)(plat_ms() - last) * 0.001f : 9.0f;
    for (k = 0; k < n && k < 12; k++) {
        float a = 1.0f, x = 8.0f + (float)k * 18.0f;
        int j, h;
        for (j = 0; j < k; j++)
            a *= fb;
        a *= 0.3f + 0.7f * mix;
        if (age < (float)k * 0.25f)
            a *= 0.25f;
        else
            a *= 0.4f + 0.6f * fm_expf(-(age - (float)k * 0.25f) * 4.0f);
        h = (int)(a * 34.0f);
        rrect((int32_t)x, 38 - h, 8, h + 1, mix565(c, K_WELL, (uint32_t)(60.0f + 196.0f * a)), 2);
    }
    (void)t;
    vlabel(UI_OPT[UP_DELAY[0].opt + ((int)(P(mon, 0) + 0.5f) & 15) % 9]);
}

static void vis_reverb(const float *mon, uint16_t c)
{
    float size = P(mon, 0), damp = P(mon, 1), t = vis_t(), lv = mon[48] * 2.0f + env_max(mon) * 0.5f;
    int k;
    vz.rng = 777u;
    lv = lv > 1.0f ? 1.0f : lv;
    for (k = 0; k < 60; k++) {
        float s = rnd(), a = rnd() * TAU + t * (0.2f + s * 0.5f), r = (0.2f + rnd() * 0.8f) * (40.0f + size * 70.0f);
        float x = 114.0f + fm_cosf(a) * r, y = 21.0f + fm_sinf(a) * r * 0.3f;
        if (x < 1 || x > VW - 2)
            continue;
        cv_pset((int32_t)x, ycl(y), mix565(k % 3 ? c : K_FG, K_WELL, (uint32_t)(40.0f + 200.0f * s * (0.3f + 0.7f * lv) * (1.0f - damp * 0.6f))));
    }
    vlabel(UI_OPT[UP_REVERB[4].opt + (((int)(P(mon, 4) + 0.5f)) % 3)]);
}

static void vis_chorus(const float *mon, uint16_t c)
{
    float rate = 0.05f + P(mon, 0) * 4.0f, depth = P(mon, 1), t = vis_t();
    int16_t a[115], b[115];
    int x;
    for (x = 0; x < 115; x++) {
        float ph = (float)x / 114.0f * TAU * 2.0f;
        a[x] = ycl(21.0f - fm_sinf(ph - t * 3.0f + fm_sinf(t * rate) * depth * 2.0f) * 14.0f);
        b[x] = ycl(21.0f - fm_sinf(ph - t * 3.0f - fm_sinf(t * rate) * depth * 2.0f) * 14.0f);
    }
    glow(b, 115, mix565(c, K_WELL, 120));
    glow(a, 115, c);
    vlabel(UI_OPT[UP_CHORUS[4].opt + (((int)(P(mon, 4) + 0.5f)) % 3)]);
}

/* the history of the modulators' and LFOs' outputs, ~30 times a second */
static void vis_tick(void)
{
    const float *mon;
    int i;
    if (plat_ms() - vz.hist_at < 33u)
        return;
    vz.hist_at = plat_ms();
    mon = rk_monitor(RS_AMP);
    for (i = 0; i < 4; i++)
        vz.hist[i][vz.hist_w % 114u] = (int8_t)(fm_clampf(mon[40 + i], -1.27f, 1.27f) * 100.0f);
    for (i = 0; i < 3; i++)
        vz.hist[4 + i][vz.hist_w % 114u] = (int8_t)(fm_clampf(mon[44 + i], -1.27f, 1.27f) * 100.0f);
    vz.hist_w = (uint8_t)((vz.hist_w + 1u) % 114u);
}

/* the well for a page: slot -1 the output's scope (home) */
static void vis_draw(int slot, int id, int sub, uint16_t c)
{
    const float *mon;
    if (slot < 0) {
        cv_rect(4, 21, 220, 1, K_MID);
        scope(VW, VH, c);
        return;
    }
    mon = rk_monitor(slot == RS_LFO ? RS_AMP : slot);
    if (slot == RS_AMP) {
        adsr(VW, VH, c);                      /* (every AMP page: the voices' bars are gone from it) */
        return;
    }
    if (slot == RS_LFO) {
        int l = sub % 3;
        trace(4 + l, c, 1);
        {
            char t[6] = "LFO 1";
            t[4] = (char)('1' + l);
            vlabel(t);
        }
        return;
    }
    switch (id) {
    case 1: vis_plaits(mon, c); break;
    case 2: vis_braids(mon, c); break;
    case 10: vis_rings(mon, c); break;
    case 12: vis_filter(mon, c); break;
    case 20: case 21:
        trace(slot == RS_MODA ? 0 : 2, c, 1);
        if (id == 21) trace(slot == RS_MODA ? 1 : 3, mix565(c, K_WELL, 110), 1);
        vlabel(id == 20 ? "PEAKS" : "TIDES");
        break;
    case 23: vis_grids(mon, c); break;
    case 31: vis_delay(mon, c); break;
    case 32: vis_reverb(mon, c); break;
    case 33: vis_chorus(mon, c); break;
    default:
        cv_rect(4, 21, 220, 1, K_MID);
        scope(VW, VH, mix565(c, K_WELL, 150));
    }
}
