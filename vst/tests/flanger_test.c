/* flanger_test.c — unit test for the DJ FX through-zero flanger (mda ThruZero DSP, MIT branch).
 *
 * Checks, all offline on x86:
 *   1. mix 0 -> transparent;
 *   2. depth 0 + mix 0.5 -> through-zero null (the wet is the dry, subtracted -> a 60 Hz tone dies);
 *   3. max depth + max feedback -> bounded (stable, no NaN/inf);
 *   4. rate 0 -> the LFO is stopped and it stays stable.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/flanger.h"

#define FS 44100.0f
#define N  44100
#define M_PIf 3.14159265358979323846f

static float g_in[N], g_l[N], g_r[N];

static void fill_sine(float hz, float amp)
{
    for (int i = 0; i < N; i++) {
        const float x = amp * sinf(2.0f * M_PIf * hz * (float)i / FS);
        g_l[i] = x; g_r[i] = x; g_in[i] = x;
    }
}

static float rms(const float *b) { double s = 0; for (int i = N/2; i < N; i++) s += (double)b[i]*b[i]; return (float)sqrt(s/(N/2)); }
static float peak(const float *b) { float p = 0; for (int i = 0; i < N; i++) { const float a = fabsf(b[i]); if (a > p) p = a; } return p; }

static int fails = 0;
static void check(const char *what, int ok, const char *detail)
{
    printf("%-56s %s%s%s\n", what, ok ? "ok" : "FAIL", detail[0] ? "  " : "", detail);
    if (!ok) fails++;
}

int main(void)
{
    flanger_t *fl = flanger_create(FS);
    if (!fl) { printf("alloc failed\n"); return 2; }
    char b[96];

    /* 1) mix 0 = transparent */
    flanger_set_mix(fl, 0.0f);
    flanger_reset(fl);
    fill_sine(400.0f, 0.5f);
    flanger_process(fl, g_l, g_r, N);
    {
        float md = 0.0f;
        for (int i = 0; i < N; i++) { const float d = fabsf(g_l[i] - g_in[i]); if (d > md) md = d; }
        snprintf(b, sizeof b, "(max|diff| = %.2e)", md);
        check("mix 0: transparent", md == 0.0f, b);
    }

    /* 2) through-zero: depth 0 + mix 0.5 subtracts the dry from itself */
    flanger_set_rate(fl, 0.0f);
    flanger_set_depth(fl, 0.0f);
    flanger_set_fbk(fl, 0.5f);   /* mda's feedback is bipolar: 0.5 = none */
    flanger_set_mix(fl, 0.5f);
    flanger_reset(fl);
    fill_sine(60.0f, 0.5f);
    flanger_process(fl, g_l, g_r, N);
    {
        const float e = rms(g_l);
        snprintf(b, sizeof b, "(rms %.5f, in 0.354)", e);
        check("depth 0, mix 0.5: through-zero null", e < 1e-3f, b);
    }

    /* 3) stability at max depth + max feedback */
    flanger_set_rate(fl, 0.6f);
    flanger_set_depth(fl, 1.0f);
    flanger_set_fbk(fl, 1.0f);
    flanger_set_mix(fl, 0.5f);
    flanger_reset(fl);
    fill_sine(120.0f, 0.5f);
    flanger_process(fl, g_l, g_r, N);
    {
        const float p = peak(g_l);
        int finite = 1;
        for (int i = 0; i < N; i++) if (!isfinite(g_l[i])) finite = 0;
        snprintf(b, sizeof b, "(peak %.3f)", p);
        check("max depth + max feedback stays bounded", finite && p < 8.0f, b);
    }

    /* 4) rate 0 (LFO stopped) stays stable */
    flanger_set_rate(fl, 0.0f);
    flanger_reset(fl);
    fill_sine(1000.0f, 0.5f);
    flanger_process(fl, g_l, g_r, N);
    {
        const float p = peak(g_l);
        int finite = 1;
        for (int i = 0; i < N; i++) if (!isfinite(g_l[i])) finite = 0;
        snprintf(b, sizeof b, "(peak %.3f)", p);
        check("rate 0: stable", finite && p < 8.0f, b);
    }

    flanger_destroy(fl);
    printf("\n%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
