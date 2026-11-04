/* FM1 Rack firmware: the factory presets played through the firmware's sound path on a computer, as WAV files,
 * with their loudness, so the presets can be heard before an install and set to the same level.
 *   audition OUTDIR            every preset: OUTDIR/NN_NAME.wav (44.1 kHz stereo, a phrase that suits its
 *                              category) and a line each: name, peak, loudness (dB, the loud part), tail
 * Built by build_host.sh beside rack_host. A phrase per category: KEYS chords and a melody, BASS a line, LEAD
 * a legato melody, PAD and FX two held chords; velocities vary so velocity shows. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rack_core.c"

#define SR 44100
#define SECS 10
typedef struct { float t, d; int n; float v; } ev_t;

static const ev_t KEYS[] = {
    {0.0f, 1.4f, 48, 0.7f}, {0.0f, 1.4f, 52, 0.7f}, {0.0f, 1.4f, 55, 0.7f}, {0.0f, 1.4f, 59, 0.7f},
    {1.5f, 1.4f, 45, 0.55f}, {1.5f, 1.4f, 48, 0.55f}, {1.5f, 1.4f, 52, 0.55f}, {1.5f, 1.4f, 55, 0.55f},
    {3.0f, 1.4f, 41, 0.85f}, {3.0f, 1.4f, 48, 0.85f}, {3.0f, 1.4f, 52, 0.85f}, {3.0f, 1.4f, 57, 0.85f},
    {4.5f, 0.3f, 67, 0.6f}, {4.8f, 0.3f, 69, 0.8f}, {5.1f, 0.3f, 72, 1.0f}, {5.4f, 0.6f, 71, 0.6f},
    {6.0f, 0.3f, 67, 0.45f}, {6.3f, 1.2f, 64, 0.75f}, {6.3f, 1.4f, 43, 0.6f}, {6.3f, 1.4f, 50, 0.6f},
};
static const ev_t BASS[] = {
    {0.0f, 0.25f, 36, 0.9f}, {0.27f, 0.12f, 36, 0.5f}, {0.55f, 0.25f, 43, 0.8f}, {0.82f, 0.25f, 46, 0.7f},
    {1.1f, 0.5f, 36, 0.9f}, {1.65f, 0.25f, 48, 0.6f}, {1.93f, 0.25f, 46, 0.7f}, {2.2f, 0.25f, 41, 0.95f},
    {2.48f, 0.12f, 41, 0.5f}, {2.75f, 0.25f, 44, 0.8f}, {3.03f, 0.25f, 48, 0.7f}, {3.3f, 0.5f, 43, 0.9f},
    {3.86f, 0.25f, 38, 0.7f}, {4.13f, 0.25f, 39, 0.8f}, {4.4f, 1.2f, 36, 0.9f},
    {6.0f, 0.12f, 36, 1.0f}, {6.3f, 0.12f, 36, 0.4f}, {6.6f, 0.12f, 48, 0.8f}, {6.9f, 0.6f, 31, 0.9f},
};
static const ev_t LEAD[] = {
    {0.0f, 0.55f, 60, 0.7f}, {0.5f, 0.55f, 63, 0.75f}, {1.0f, 0.55f, 67, 0.85f}, {1.5f, 1.0f, 70, 0.9f},
    {2.5f, 0.3f, 68, 0.6f}, {2.8f, 0.3f, 67, 0.65f}, {3.1f, 0.55f, 65, 0.7f}, {3.6f, 1.4f, 67, 0.85f},
    {5.2f, 0.25f, 72, 0.9f}, {5.45f, 0.25f, 70, 0.6f}, {5.7f, 0.25f, 67, 0.7f}, {5.95f, 1.6f, 63, 0.8f},
};
static const ev_t PAD[] = {
    {0.0f, 3.2f, 48, 0.6f}, {0.0f, 3.2f, 55, 0.6f}, {0.0f, 3.2f, 59, 0.6f}, {0.0f, 3.2f, 64, 0.6f},
    {3.4f, 3.4f, 45, 0.75f}, {3.4f, 3.4f, 52, 0.75f}, {3.4f, 3.4f, 57, 0.75f}, {3.4f, 3.4f, 60, 0.75f},
};
static const ev_t TEX[] = {
    {0.0f, 3.0f, 43, 0.7f}, {0.0f, 3.0f, 50, 0.7f}, {1.0f, 2.0f, 62, 0.5f},
    {3.3f, 3.5f, 41, 0.8f}, {3.3f, 3.5f, 48, 0.8f}, {4.0f, 2.8f, 57, 0.6f}, {4.6f, 2.2f, 64, 0.5f},
};

static int32_t lr[2 * SR * SECS];

static void wav(const char *path, const int32_t *x, int frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t n = (uint32_t)frames * 4u, v;
    int i;
    if (!f)
        return;
    fwrite("RIFF", 1, 4, f); v = 36u + n; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); v = 1u | 2u << 16; fwrite(&v, 4, 1, f); v = SR; fwrite(&v, 4, 1, f);
    v = SR * 4u; fwrite(&v, 4, 1, f); v = 4u | 16u << 16; fwrite(&v, 4, 1, f); fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f);
    for (i = 0; i < frames * 2; i++) {
        int32_t s = x[i] >> 8;
        int16_t h = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        fwrite(&h, 2, 1, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    int k;
    if (argc < 2) {
        fprintf(stderr, "audition OUTDIR [preset]\n");
        return 1;
    }
    for (k = 0; k < NPRESETS; k++) {
        const char *c = PRESETS[k].cat;
        const ev_t *ev = !strcmp(c, "KEYS") || !strcmp(c, "INIT") ? KEYS : !strcmp(c, "BASS") ? BASS : !strcmp(c, "LEAD") ? LEAD
                         : !strcmp(c, "PAD") ? PAD : TEX;
        int nev = ev == KEYS ? (int)(sizeof KEYS / sizeof *ev) : ev == BASS ? (int)(sizeof BASS / sizeof *ev)
                  : ev == LEAD ? (int)(sizeof LEAD / sizeof *ev) : ev == PAD ? (int)(sizeof PAD / sizeof *ev) : (int)(sizeof TEX / sizeof *ev);
        int b, i, on[64] = {0};
        float peak = 0, win[SR * SECS / 4410 + 1], tail = 0;
        int nw = 0;
        char path[512], nm[32];
        if (argc > 2 && atoi(argv[2]) != k)
            continue;
        rk_seed(0x21u);
        rc_pause();
        rc_load(k);
        rc_resume();
        for (b = 0; b + 256 <= SR * SECS; b += 256) {
            float t0 = (float)b / SR, t1 = (float)(b + 256) / SR;
            for (i = 0; i < nev; i++) {
                if (!on[i] && ev[i].t >= t0 - 1e-6f && ev[i].t < t1) { rc_note_on(ev[i].n, ev[i].v); on[i] = 1; }
                if (on[i] == 1 && ev[i].t + ev[i].d < t1) { rc_note_off(ev[i].n); on[i] = 2; }
            }
            rc_render(lr + 2 * b, 256, 8388607.0f * 0.7f);
        }
        for (i = 0; i + 4410 <= SR * SECS; i += 4410) {   /* 100 ms windows */
            double s = 0;
            int j;
            for (j = i; j < i + 4410; j++) {
                float l = (float)lr[2 * j] / 8388607.0f, r = (float)lr[2 * j + 1] / 8388607.0f;
                s += 0.5 * (l * l + r * r);
                if (fabsf(l) > peak) peak = fabsf(l);
                if (fabsf(r) > peak) peak = fabsf(r);
            }
            win[nw++] = (float)sqrt(s / 4410.0);
        }
        {   /* loudness: the mean of the loudest quarter of the windows (the part that plays) */
            int a, q = nw / 4;
            double s = 0;
            for (a = 0; a < nw; a++)
                for (i = a + 1; i < nw; i++)
                    if (win[i] > win[a]) { float t = win[a]; win[a] = win[i]; win[i] = t; }
            for (a = 0; a < q; a++) s += win[a] * win[a];
            tail = 0;
            printf("%-16s %-5s peak %.3f  loud %6.1f dB\n", PRESETS[k].name, c, peak, 10.0 * log10(s / q + 1e-12));
        }
        for (i = 0; PRESETS[k].name[i] && i < 31; i++) nm[i] = PRESETS[k].name[i] == ' ' ? '_' : PRESETS[k].name[i];
        nm[i] = 0;
        snprintf(path, sizeof path, "%s/%02d_%s.wav", argv[1], k, nm);
        wav(path, lr, SR * SECS);
        (void)tail;
    }
    return 0;
}
