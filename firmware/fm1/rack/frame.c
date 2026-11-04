/* SPDX-License-Identifier: MPL-2.0 */
/* FM1 Rack firmware: the frame round every page (stage 4, after the HTML mockup of 4 Nov 2026), part of app.c.
 *
 *   the status strip (top, 10 rows): the voices' lights, cores and voice mode (SHED n, HEAVY), CPU and RAM as small
 *     bars with their numbers, the octave and the output meter: what the old footer held, smaller;
 * (The modulation strip that was at the bottom is gone: the sliders' halos show the modulation now.) */

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
