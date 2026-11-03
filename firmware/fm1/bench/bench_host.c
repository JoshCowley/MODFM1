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

/* the second core on the computer: the job runs when the first core waits for it, so the voices draw their random
 * numbers in the same order as on one core and the sound differs only by the order the voices are added in (on the
 * FM-1 the cores run at once, so voices that use random numbers, drift or noise, draw them in another order) */
static void (*host_job)(uint32_t);
static uint32_t host_arg;
static int host_cpu2_run(void (*fn)(uint32_t), uint32_t arg) { host_job = fn; host_arg = arg; return 1; }
static int host_cpu2_wait(void) { if (host_job) { host_job(host_arg); host_job = 0; } return 0; }

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

/* one case, played by a fresh process: 1 s (left) and the blocks the second core shared */
static int case_played(const char *self, int k, float *buf, uint32_t *shared)
{
    char cmd[512];
    FILE *f;
    size_t n, m;
    snprintf(cmd, sizeof cmd, "'%s' --case %d", self, k);
    f = popen(cmd, "r");
    if (!f)
        return -1;
    n = fread(buf, sizeof(float), 48000, f);
    m = fread(shared, sizeof *shared, 1, f);
    pclose(f);
    return n == 48000 && m == 1 ? 0 : -1;
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
    if (argc == 3 && !strcmp(argv[1], "--case")) {   /* one case, fresh: 1 s (left), then the blocks shared */
        uint32_t shared;
        bench_cpu2_run = host_cpu2_run;
        bench_cpu2_wait = host_cpu2_wait;
        bench_cases();
        k = atoi(argv[2]);
        bench_setup(k);
        for (i = 0; i < 100; i++) {
            rk_render(480);
            memcpy(a + 480 * i, rk_out_l(), 480 * sizeof(float));
        }
        shared = rk_worker_blocks();
        fwrite(a, sizeof(float), 48000, stdout);
        fwrite(&shared, sizeof shared, 1, stdout);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "--play")) {
        k = atoi(argv[2]);
        play(atoi(argv[3]) ? &PRESETS_FULL[k] : &PRESETS[k], 0, a, 48000);
        fwrite(a, sizeof(float), 48000, stdout);
        return 0;
    }
    bench_cpu2_run = host_cpu2_run;
    bench_cpu2_wait = host_cpu2_wait;
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
    /* each 2 CORES case against its one-core twin (same preset and notes): the same sound but for the order the
     * voices are added in, and the second core really took its share */
    {
        int c2, c1, worst_k = -1;
        float worst = 0;
        for (c2 = 0; c2 < bncases; c2++) {
            if (bcases[c2].group != G_CORES2)
                continue;
            for (c1 = 0; c1 < bncases; c1++)
                if (bcases[c1].group != G_CORES2 && bcases[c1].preset == bcases[c2].preset
                    && bcases[c1].notes == bcases[c2].notes && bcases[c1].ch[0][0] == 0)
                    break;
            if (c1 == bncases) {
                printf("2 cores %-14s no one-core twin\n", bcases[c2].name);
                fails++;
                continue;
            }
            {
                uint32_t s1 = 0, s2 = 0;
                float d = 0;
                if (case_played(argv[0], c1, a, &s1) || case_played(argv[0], c2, b, &s2)) {
                    printf("2 cores %-14s could not play\n", bcases[c2].name);
                    fails++;
                    continue;
                }
                for (i = 0; i < 48000; i++)
                    if (fabsf(a[i] - b[i]) > d)
                        d = fabsf(a[i] - b[i]);
                if (d > worst) { worst = d; worst_k = c2; }
                /* rounding from the new order can grow in the effects (STRING MACH: 0.00017); 0.001 is -60 dB. A preset the
                 * arena gives one voice has nothing to share */
                if (d > 1e-3f || s1 != 0 || (bres[c2].voices_on >= 2 && s2 == 0)) {
                    printf("2 cores %-14s differs by %g, blocks shared %u (one core: %u)\n", bcases[c2].name, d,
                           (unsigned)s2, (unsigned)s1);
                    fails++;
                }
            }
        }
        printf("2 cores: worst difference from one core %g (%s)\n", worst, worst_k >= 0 ? bcases[worst_k].name : "-");
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
