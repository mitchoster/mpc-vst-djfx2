/* repeat_test.c — unit test for the DJ FX beat repeat (own code, after WrongPeople/JustRepeat, MIT).
 *
 * At 120 bpm one 4/4 bar is 2 s (88200 samples); a 1/8 grid makes each slice 11025 samples.
 * Checks, all offline on x86:
 *   1. grid Off / chance 0 -> transparent (bit-identical);
 *   2. grid exactness: the first slice is replayed sample-exact at the slice period;
 *   3. count: after `count` repeats the module passes the input through again;
 *   4. gate: with gate 0.5 the second half of every slice is silent (mix 1);
 *   5. click-free: no sample step much larger than the input's own.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/repeat.h"

#define FS 44100.0f
#define BAR_SAMP 88200            /* 2 s */
#define SLICE    11025            /* 1/8 of a bar */
#define N        (3 * BAR_SAMP)   /* three bars */
#define M_PIf 3.14159265358979323846f

static float g_in[N], g_l[N], g_r[N];

static void fill_sine(float hz, float amp, int n)
{
    for (int i = 0; i < n; i++) {
        const float x = amp * sinf(2.0f * M_PIf * hz * (float)i / FS);
        g_l[i] = x; g_r[i] = x; g_in[i] = x;
    }
}

static float max_abs_diff(const float *a, const float *b, int from, int to)
{
    float md = 0.0f;
    for (int i = from; i < to; i++) { const float d = fabsf(a[i] - b[i]); if (d > md) md = d; }
    return md;
}

static int fails = 0;
static void check(const char *what, int ok, const char *detail)
{
    printf("%-58s %s%s%s\n", what, ok ? "ok" : "FAIL", detail[0] ? "  " : "", detail);
    if (!ok) fails++;
}

int main(void)
{
    repeat_t *rep = repeat_create(FS);
    if (!rep) { printf("alloc failed\n"); return 2; }
    char b[96];

    fill_sine(200.0f, 0.5f, N);

    /* 1) transparent when off / chance 0 */
    repeat_set_mode(rep, REP_OFF);
    repeat_reset(rep);
    memcpy(g_l, g_in, N * sizeof(float));
    repeat_process(rep, g_l, g_r, N);
    {
        const float md = max_abs_diff(g_l, g_in, 0, N);
        snprintf(b, sizeof b, "(max|diff| = %.2e)", md);
        check("grid Off: transparent", md == 0.0f, b);
    }
    repeat_set_mode(rep, REP_1_8);
    repeat_set_chance(rep, 0.0f);
    repeat_reset(rep);
    memcpy(g_l, g_in, N * sizeof(float));
    repeat_process(rep, g_l, g_r, N);
    {
        const float md = max_abs_diff(g_l, g_in, 0, N);
        snprintf(b, sizeof b, "(max|diff| = %.2e)", md);
        check("chance 0: transparent", md == 0.0f, b);
    }

    /* 2/3) 1/8 grid, gate 1, 4 repeats, mix 1 */
    repeat_set_mode(rep, REP_1_8);
    repeat_set_gate(rep, 1.0f);
    repeat_set_chance(rep, 1.0f);
    repeat_set_count(rep, 4);
    repeat_set_mix(rep, 1.0f);
    repeat_reset(rep);
    memcpy(g_l, g_in, N * sizeof(float));
    repeat_process(rep, g_l, g_r, N);
    {
        /* the first repeat (second slice) should be the captured slice, sample-exact in the interior */
        float md = 0.0f;
        for (int j = SLICE + 200; j < 2 * SLICE - 200; j++) {
            const float d = fabsf(g_l[j] - g_in[j - SLICE]);
            if (d > md) md = d;
        }
        snprintf(b, sizeof b, "(interior max|diff| = %.2e)", md);
        check("1/8 grid: repeat is sample-exact", md < 1e-6f, b);

        /* after 1 capture + 4 repeats the input passes through again (within the same bar) */
        const int dry_from = (1 + 4) * SLICE;
        const float md2 = max_abs_diff(g_l, g_in, dry_from + 200, BAR_SAMP - 200);
        snprintf(b, sizeof b, "(max|diff| = %.2e)", md2);
        check("count 4: input returns after the repeats", md2 == 0.0f, b);
    }

    /* 4) gate 0.5 -> the second half of every slice is silent */
    repeat_set_gate(rep, 0.5f);
    repeat_reset(rep);
    memcpy(g_l, g_in, N * sizeof(float));
    repeat_process(rep, g_l, g_r, N);
    {
        float peak = 0.0f;
        for (int i = SLICE + SLICE / 2 + 100; i < 2 * SLICE - 100; i++)
            { const float a = fabsf(g_l[i]); if (a > peak) peak = a; }
        snprintf(b, sizeof b, "(peak in the gated half = %.4f)", peak);
        check("gate 0.5: second half of a slice is silent", peak < 1e-3f, b);
    }

    /* 5) click-free: the largest sample step stays small */
    repeat_set_gate(rep, 1.0f);
    repeat_reset(rep);
    memcpy(g_l, g_in, N * sizeof(float));
    repeat_process(rep, g_l, g_r, N);
    {
        float maxstep = 0.0f;
        for (int i = 1; i < N; i++) { const float d = fabsf(g_l[i] - g_l[i - 1]); if (d > maxstep) maxstep = d; }
        /* a 0.5-amp 200 Hz sine steps by ~0.014; the fades must not produce a hard step */
        snprintf(b, sizeof b, "(max step = %.4f)", maxstep);
        check("click-free at the slice boundaries", maxstep < 0.08f, b);
    }

    repeat_destroy(rep);
    printf("\n%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
