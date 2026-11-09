/* FM1 Rack firmware: how hard each preset clicks when a note starts and stops, measured on a computer.
 *   click [preset]   per preset: the biggest jump between two samples in the 30 ms after a key goes down and
 *                    after it comes up, against the biggest jump while the note sustains (dB). A jump well
 *                    above the sustain's is a click. Optional WAV of the test: click PRESET out.wav
 * Built by build_host.sh beside audition. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rack_core.c"

#define FR 256
#define LEN (48000 * 2)
static int32_t lr[2 * LEN];

static float jump(int a, int b)
{
    float m = 0;
    int i;
    for (i = a + 1; i < b; i++) {
        float l = fabsf((float)(lr[2 * i] - lr[2 * i - 2])), r = fabsf((float)(lr[2 * i + 1] - lr[2 * i - 1]));
        if (l > m) m = l;
        if (r > m) m = r;
    }
    return m / 8388607.0f;
}

int main(int argc, char **argv)
{
    int k, on = 4800, off = 48000, b;
    for (k = 0; k < NPRESETS; k++) {
        float jo, js, jf;
        if (argc > 1 && atoi(argv[1]) != k) continue;
        rk_seed(0x21u);
        rc_pause(); rc_load(k); rc_resume();
        for (b = 0; b + FR <= LEN; b += FR) {
            if (b <= on && on < b + FR) rc_note_on(48, 0.8f);
            if (b <= off && off < b + FR) rc_note_off(48);
            rc_render(lr + 2 * b, FR, 8388607.0f * 0.7f);
        }
        jo = jump(on, on + 1440); js = jump(on + 14400, off - 2400); jf = jump(off, off + 1440);
        printf("%2d %-16s on %6.1f  sus %6.1f  off %6.1f  (on-sus %+5.1f, off-sus %+5.1f)\n", k, PRESETS[k].name,
               20 * log10f(jo + 1e-9f), 20 * log10f(js + 1e-9f), 20 * log10f(jf + 1e-9f),
               20 * log10f((jo + 1e-9f) / (js + 1e-9f)), 20 * log10f((jf + 1e-9f) / (js + 1e-9f)));
        if (argc > 2) {
            FILE *f = fopen(argv[2], "wb");
            fwrite(lr, 4, 2 * LEN, f);
            fclose(f);
        }
    }
    return 0;
}
