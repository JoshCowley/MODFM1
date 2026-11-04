/* SPDX-License-Identifier: MPL-2.0 */
/* FM1 Rack firmware: the frame round every page (stage 4, after the HTML mockup of 4 Nov 2026), part of app.c.
 *
 *   the status strip (top, 10 rows): the voices' lights, cores and voice mode (SHED n, HEAVY), CPU and RAM as small
 *     bars with their numbers, the octave and the output meter: what the old footer held, smaller;
 *   the modulation strip (bottom, 26 rows): one chip per matrix route that is set (the 12 rows and the LFOs' direct
 *     targets): the source's icon in its colour, the source, the destination and a needle that moves with the source.
 *     On a module, AMP, LFO or OSC page the routes into that page come first, lit; the others dimmed after them.
 * Both redraw with the well, about 30 times a second, so the needles move. */

static int src_icon(int s)
{
    if (s >= 1 && s <= 4) return IC_KNOB;
    if (s == 7) return IC_RANDOM;
    if (s == 8 || s == 16) return IC_AMP;
    if (s >= 9 && s <= 12) return module_icon(rc_patch.mod[s < 11 ? RS_MODA : RS_MODB]);
    if (s >= 13 && s <= 15) return IC_LFO;
    if (s >= 17 && s <= 20) return IC_SEQ;
    return IC_KNOB;
}
static int src_bipolar(int s) { return s == 7 || (s >= 9 && s <= 15) || (s >= 17 && s <= 20); }

static void status_strip(void)
{
    char t[16], n[8];
    uint32_t cpu = plat_cpu_pct();
    float ram = (float)rk_fm1_status(0) / (float)(rk_fm1_status(1) ? rk_fm1_status(1) : 1), pk = rc_audio.peak;
    int i;
    cv_begin(240, 10, K_BG);
    for (i = 0; i < RC_MAXV; i++) {
        int on = i < rc_patch.voices_now && i < rk_fm1_status(2);
        float e = on ? rk_voice_env(i) : 0.0f;
        cv_rect(3 + i * 5, 2, 3, 6, on ? mix565(K_OK, K_OFF, (uint32_t)(38.0f + 218.0f * (e > 1.0f ? 1.0f : e))) : K_OFF);
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
    cv_text(36, -1, &FONT_T, t, rc_heavy ? K_WARN : rc_shed_count ? K_GOLD : K_DIM);
    cv_text(92, -1, &FONT_T, "CPU", cpu > 85u ? K_WARN : K_DIM);
    cv_rect(110, 4, 22, 2, K_FAINT);
    cv_rect(110, 4, (int32_t)(cpu > 100u ? 22u : cpu * 22u / 100u), 2, cpu > 85u ? K_WARN : cpu > 70u ? K_GOLD : K_OK);
    int_text((int32_t)cpu, n);
    cv_text(135, -1, &FONT_T, n, K_FG);
    cv_text(152, -1, &FONT_T, "RAM", K_DIM);
    cv_rect(171, 4, 18, 2, K_FAINT);
    cv_rect(171, 4, (int32_t)(18.0f * (ram > 1.0f ? 1.0f : ram)), 2, ram > 0.95f ? K_GOLD : K_OK);
    t[0] = 'O';
    t[1] = set.octave < 0 ? '-' : '+';
    t[2] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave));
    t[3] = 0;
    cv_text(196, -1, &FONT_T, t, ui.keys ? K_OK : K_DIM);
    rc_audio.peak = 0.0f;
    pk = pk > 1.0f ? 1.0f : pk * 1.3f > 1.0f ? 1.0f : pk * 1.3f;
    cv_rect(215, 3, 22, 4, K_FAINT);
    cv_rect(215, 3, (int32_t)(22.0f * pk + 0.5f), 4, pk > 0.9f ? K_WARN : K_OK);
    cv_rect(0, 9, 240, 1, K_MID);
    cv_blit(0, 0);
}

/* one chip: the source's icon, its name, where it goes, the needle (lit 1: into this page) */
static void mod_chip(int32_t x, int r, int src, uint16_t dst, float amt, const float *mon, int lit, int focus)
{
    uint16_t c = src_colour(src), cc = lit ? c : mix565(c, K_BG, 120);
    char t[24];
    float v = r < 12 ? (fm_fabsf(amt) > 0.001f ? mon[50 + r] / amt : 0.0f) : mon[44 + (r - 12) / 2], f;
    int32_t nx;
    rrect(x, 2, 57, 23, lit ? K_CELL : RGB(10, 10, 9), 3);
    icon(src_icon(src), x + 3, 6, cc);
    cv_text(x + 18, 0, &FONT_T, src_name(src), cc);
    dst_name(dst, t);
    {                                         /* the parameter alone, in its slot's colour (the slot is the colour) */
        char *d = t;
        while (*d && *d != '.') d++;
        if (*d == '.') {
            char u[24];
            u[0] = 0;
            cat(u, d + 1);
            t[0] = 0;
            cat(t, u);
        }
    }
    while (text_w(&FONT_T, t) > 38 && t[0] && t[1]) {   /* too long: cut */
        int k = 0;
        while (t[k]) k++;
        t[k - 1] = 0;
    }
    {
        int ds = (int)((dst >> 8) & 0x7F) - 1;
        uint16_t dc = ds < 0 ? K_FG : ds == RS_AMP && (dst & 0xFF) >= OSC_P0 ? K_OSC : slot_colour(ds);
        cv_text(x + 18, 9, &FONT_T, t, lit ? dc : mix565(dc, K_BG, 110));
    }
    (void)focus;
    f = src_bipolar(src) ? (v + 1.0f) * 0.5f : v;
    f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    cv_rect(x + 3, 22, 51, 1, K_FAINT);
    nx = x + 3 + (int32_t)(f * 48.0f);
    cv_rect(nx, 20, 3, 4, cc);
}

/* the modulation strip; slot >= 0: that page's routes first and lit */
static void mod_strip(int slot)
{
    int r, n = 0, more = 0, src, via, pass;
    float amt;
    const float *mon = rk_monitor(slot >= 0 ? slot : RS_AMP);
    cv_begin(240, 26, K_BG);
    cv_rect(0, 0, 240, 1, K_MID);
    for (pass = 0; pass < 2; pass++)
        for (r = 0; r < 18; r++) {
            uint16_t d = route_dst(r, &src, &via, &amt);
            int mine;
            if (!d || !src)
                continue;
            mine = slot >= 0 && (int)((d >> 8) & 0x7F) - 1 == slot;
            if (slot < 0 ? pass : mine == pass)  /* pass 0: this page's (or all, on home), pass 1: the rest */
                continue;
            if (n < 4)
                mod_chip(2 + n * 59, r, src, d, amt, mon, slot < 0 || mine, slot >= 0 && mine);
            else
                more++;
            n++;
        }
    if (!n)
        cv_text(6, 6, &FONT_T, "NO MODULATION ROUTED: ARP (MATRIX) OR HOLD HOME (LEARN)", K_FAINT);
    if (more) {
        char t[8] = "+";
        int_text(more, t + 1);
        rrect(232 - text_w(&FONT_T, t), 2, text_w(&FONT_T, t) + 6, 11, K_FG, 3);
        cv_text(235 - text_w(&FONT_T, t), 1, &FONT_T, t, K_BG);
    }
    cv_blit(0, 214);
}
static int frame_slot(void)
{
    if (mn.page == PG_HOME || sv.open)
        return -1;
    return page_slot(mn.page);
}
