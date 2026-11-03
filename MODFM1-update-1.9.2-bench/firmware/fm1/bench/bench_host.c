/* The benchmark on a computer: the same cases and engine build (-DRACK_FM1 -DRACK_LITE), timed on the computer's
 * clock. What it checks before the firmware goes onto an FM-1:
 *   1. every case makes sound (no silence, no NaN) and its modules fit the arena as the FM-1's will;
 *   2. loading INIT then a preset's differences (cases.h) gives the same sound as INIT then the preset's own calls
 *      (cases_full.h, as the page loads a preset), sample for sample: each played by a fresh process (bench_host
 *      --play), since the engine keeps some state (delay lines, the clock, filters) across init();
 *   3. the FPU check's expected result (FPU_EXPECT).
 * Its timings are the computer's, not the FM-1's. Build and run: sh build_host.sh */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include "bench.c"
#include "cases_full.h"

static uint32_t host_ticks(void)   /* 24 ticks a microsecond, as the FM-1's plat_ticks24 */
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 24000000u + (uint64_t)ts.tv_nsec * 24u / 1000u);
}

/* a preset loaded both ways, a chord, 1 s: the largest difference */
static float play(const bpreset_t *p, int diff, float *buf, int n)
{
    int i, f;
    rk_seed(0x21u);
    rk_init();
    run_ops(PRESETS[P_INIT].ops, PRESETS[P_INIT].nops);
    run_ops(p->ops, p->nops);   /* the difference, or the preset's own calls, after INIT (as the page loads one) */
    (void)diff;
    rk_set_voices(p->voices);
    for (i = 0; i < (p->mode ? 1 : 3); i++)
        rk_note_on(CHORD[i], 0.8f);
    for (f = 0; f < n; f += 480) {
        rk_render(480);
        memcpy(buf + f, rk_out_l(), 480 * sizeof(float));
    }
    return 0;
}

/* one preset, played by a fresh process: its first 48000 samples (left) */
static int played(const char *self, int k, int full, float *buf)
{
    char cmd[512];
    FILE *f;
    size_t n;
    snprintf(cmd, sizeof cmd, "'%s' --play %d %d", self, k, full);
    f = popen(cmd, "r");
    if (!f)
        return -1;
    n = fread(buf, sizeof(float), 48000, f);
    pclose(f);
    return n == 48000 ? 0 : -1;
}

int main(int argc, char **argv)
{
    static float a[48000], b[48000];
    int k, i, fails = 0;
    uint32_t took, fpu;
    if (argc == 4 && !strcmp(argv[1], "--play")) {
        k = atoi(argv[2]);
        play(atoi(argv[3]) ? &PRESETS_FULL[k] : &PRESETS[k], 0, a, 48000);
        fwrite(a, sizeof(float), 48000, stdout);
        return 0;
    }
    bench_cases();
    printf("%d cases\n", bncases);
    for (k = 0; k < bncases; k++) {
        bench_setup(k);
        for (i = 0; i < WARM_STEPS; i++)
            bench_warm();
        for (i = 0; i < NRUN; i++)
            bench_run(k, host_ticks);
        bench_done(k);
        {
            const bres_t *r = &bres[k];
            int silent = r->peak < 1e-4f && strcmp(bcases[k].name, "NO SOURCE") != 0;
            printf("%-8s %-14s %6.1f%%  %s%5.1f%%  peak %.3f  voices %d/%d  arena %3u KB  dropped %02x%s%s\n",
                   GROUP[bcases[k].group], bcases[k].name, bench_pct10(k, 1, 24) / 10.0,
                   bcases[k].minus >= 0 ? "own " : "    ", bcases[k].minus >= 0 ? bench_pct10(k, 0, 24) / 10.0 : 0.0,
                   r->peak, r->voices_on, r->voices_fit, r->arena / 1024u, r->dropped, r->bad ? "  NAN" : "",
                   silent ? "  SILENT" : "");
            if (r->bad || silent)
                fails++;
        }
    }
    for (k = 1; k < NPRESETS; k++) {
        float d = 0;
        if (played(argv[0], k, 0, a) || played(argv[0], k, 1, b)) {
            printf("diff load %-14s could not play\n", PRESETS[k].name);
            fails++;
            continue;
        }
        for (i = 0; i < 48000; i++)
            if (fabsf(a[i] - b[i]) > d)
                d = fabsf(a[i] - b[i]);
        printf("diff load %-14s max difference %g\n", PRESETS[k].name, d);
        if (d != 0.0f)
            fails++;
    }
    fpu = fpu_check(host_ticks, &took);
    printf("fpu check %08x (expected %08x)\n", fpu, FPU_EXPECT);
    if (fpu != FPU_EXPECT)
        fails++;
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
