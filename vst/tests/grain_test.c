/* grain_test.c - spike measurements for the DJ FX 2 grain delay (own code).
 *
 * This is the T14 decision document as a test. It measures the engine against the behaviour read out of
 * the original DJ-FX rack (rate 1..150 Hz, read offset 22 -> 2 ms, dry/wet 0 -> 35 %) instead of just
 * echoing the parameters back:
 *
 *   1. grain rate      - grains actually started per second at 150 Hz and at 1 Hz
 *   2. read offset     - an impulse placed in the buffer re-appears ~`time` ms later (22 ms and 2 ms)
 *   3. dry/wet law     - white noise through an uncorrelated crossfade follows sqrt((1-m)^2 + m^2)
 *   4. transparency    - mix = 0 is a sample-exact passthrough
 *   5. determinism     - two freshly reset instances produce bit-identical output
 *   6. cost            - processing time against the already-shipped repeat module (relative figure,
 *                        because absolute x86 numbers say nothing about the armv7 device)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
#include "../include/grain.h"
#include "../include/repeat.h"

#define FS 44100
#define N  (FS * 1)
static float g_l[N], g_r[N];

static int fails = 0;

static void check(const char *what, int ok, const char *detail)
{
    printf("%-58s %s%s%s\n", what, ok ? "ok" : "FAIL", detail[0] ? "  " : "", detail);
    if (!ok) fails++;
}

static double rms(const float *b, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; i++) s += (double)b[i] * (double)b[i];
    return sqrt(s / (double)n);
}

static void noise(uint32_t *seed)
{
    for (int i = 0; i < N; i++) {
        uint32_t x = *seed;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        *seed = x;
        g_l[i] = g_r[i] = ((float)(x >> 8) * (1.0f / 8388608.0f)) - 1.0f;
    }
}

/* --- 1: how many grains actually start --- */
static double grains_per_second(float hz)
{
    grain_t *g = grain_create((float)FS);
    grain_set_rate(g, hz);
    grain_set_time(g, 20.0f);
    grain_set_mix(g, 1.0f);            /* mix 0 bypasses the stage entirely (see grain.c) */
    grain_reset(g);
    memset(g_l, 0, sizeof g_l); memset(g_r, 0, sizeof g_r);
    for (int done = 0; done < FS; done += 128)          /* exactly one second, not 344 blocks */
        grain_process(g, g_l, g_r, (FS - done < 128) ? FS - done : 128);
    const unsigned long started = grain_spawned(g);
    grain_destroy(g);
    return (double)started;                      /* one second of audio */
}

/* second attempt, sample-by-sample so the impulse position is unambiguous */
static double offset_of_impulse(float time_ms)
{
    const int P = 17640;                          /* impulse position: 0.4 s in (grains are running) */
    const int total = 44100;                      /* 1 s */
    grain_t *g = grain_create((float)FS);
    grain_set_rate(g, 150.0f);
    grain_set_time(g, time_ms);
    grain_set_spray(g, 0.0f);                     /* no jitter, so the offset is exactly measurable */
    grain_set_mix(g, 1.0f);                       /* wet only: the copy is the whole output */
    grain_reset(g);

    static float y[44100];
    for (int i = 0; i < total; i += 128) {
        int n = (i + 128 <= total) ? 128 : total - i;
        for (int k = 0; k < n; k++) { float in = (i + k == P) ? 1.0f : 0.0f; g_l[k] = in; g_r[k] = in; }
        grain_process(g, g_l, g_r, n);
        for (int k = 0; k < n; k++) y[i + k] = g_l[k];
    }
    float mx = 0.0f;
    for (int k = P + 2; k < total; k++) if (y[k] > mx) mx = y[k];
    int onset = -1;
    for (int k = P + 2; k < total; k++) if (y[k] > 0.05f * mx) { onset = k; break; }
    grain_destroy(g);
    return onset < 0 ? -1.0 : (double)(onset - P) * 1000.0 / (double)FS;
}

/* --- 6: relative cost --- */
static double time_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static double cost_of_grain(void)
{
    const int blocks = (FS * 30) / 128;          /* 30 s of stereo */
    uint32_t sd = 7;
    grain_t *g = grain_create((float)FS);
    grain_set_rate(g, 150.0f); grain_set_time(g, 5.0f); grain_set_mix(g, 0.5f);
    noise(&sd);
    const double t0 = time_seconds();
    for (int b = 0; b < blocks; b++) grain_process(g, g_l, g_r, 128);
    const double dt = time_seconds() - t0;
    grain_destroy(g);
    return dt;
}

static double cost_of_repeat(void)
{
    const int blocks = (FS * 30) / 128;
    uint32_t sd = 7;
    repeat_t *r = repeat_create((float)FS);
    repeat_set_bpm(r, 120.0f); repeat_set_mode(r, REP_1_16);
    repeat_set_gate(r, 0.25f); repeat_set_chance(r, 1.0f); repeat_set_count(r, 4); repeat_set_mix(r, 0.5f);
    noise(&sd);
    const double t0 = time_seconds();
    for (int b = 0; b < blocks; b++) repeat_process(r, g_l, g_r, 128);
    const double dt = time_seconds() - t0;
    repeat_destroy(r);
    return dt;
}

int main(void)
{
    char b[96];

    /* 1 - grain rate */
    const double r150 = grains_per_second(150.0f);
    const double r1 = grains_per_second(1.0f);
    snprintf(b, sizeof b, "(150 Hz -> %.0f/s, 1 Hz -> %.0f/s)", r150, r1);
    check("grain rate: 150 Hz spawns ~150 grains per second", r150 > 144.0 && r150 < 156.0, b);
    check("grain rate: 1 Hz spawns ~1 grain per second", r1 >= 1.0 && r1 <= 2.0, b);

    /* 2 - read offset */
    const double off22 = offset_of_impulse(22.0f);
    const double off2 = offset_of_impulse(2.0f);
    snprintf(b, sizeof b, "(22 ms -> %.1f ms, 2 ms -> %.1f ms)", off22, off2);
    check("read offset: 22 ms impulse lands at ~22 ms", off22 > 19.0 && off22 < 25.0, b);
    check("read offset: 2 ms impulse lands at ~2 ms", off2 >= 0.0 && off2 < 5.0, b);

    /* 3 - dry/wet law, checked against the measured cross-term (not against an uncorrelated guess) */
    {
        uint32_t sd = 11;
        noise(&sd);
        float x[N];
        memcpy(x, g_l, sizeof(float) * N);

        const float mixes[3] = { 0.0f, 1.0f, 0.35f };
        static float y[3][N];
        for (int m = 0; m < 3; m++) {
            grain_t *g = grain_create((float)FS);
            grain_set_rate(g, 150.0f); grain_set_time(g, 12.0f); grain_set_spray(g, 0.5f);
            grain_set_mix(g, mixes[m]);
            grain_reset(g);
            static float r[N];
            memcpy(y[m], x, sizeof(float) * N);
            memcpy(r, x, sizeof(float) * N);
            for (int i = 0; i < N; i += 128) grain_process(g, y[m], r, 128);
            grain_destroy(g);
        }
        const double e_dry = rms(y[0], N), e_wet = rms(y[1], N);
        double cross = 0.0;
        for (int i = 0; i < N; i++) cross += (double)y[0][i] * (double)y[1][i];
        cross /= (double)N;
        const double m = 0.35;
        const double predict = sqrt((1.0 - m) * (1.0 - m) * e_dry * e_dry
                                    + m * m * e_wet * e_wet
                                    + 2.0 * m * (1.0 - m) * cross);
        const double got = rms(y[2], N);
        snprintf(b, sizeof b, "(dry %.3f, wet %.3f, mix .35: got %.3f want %.3f)",
                 e_dry, e_wet, got, predict);
        check("dry/wet 0.35 equals dry/wet/cross-term prediction", fabs(got - predict) < 0.02 * predict, b);
        check("wet path is not silent (mix 1 produces output)", e_wet > 0.2 * e_dry, b);
    }

    /* 4 - transparency at mix 0 */
    {
        uint32_t sd = 13;
        noise(&sd);
        float ref[N];
        memcpy(ref, g_l, sizeof(float) * N);
        grain_t *g = grain_create((float)FS);
        grain_set_mix(g, 0.0f);
        grain_reset(g);
        for (int i = 0; i < N; i += 256) grain_process(g, g_l, g_r, 256 < N - i ? 256 : N - i);
        grain_destroy(g);
        check("mix 0 is a sample-exact passthrough", memcmp(ref, g_l, sizeof ref) == 0, "");
    }

    /* 5 - determinism */
    {
        uint32_t sd = 17;
        noise(&sd);
        static float a[N], a2[N], ar[N], ar2[N];
        memcpy(ar, g_r, sizeof(float) * N);
        memcpy(ar2, g_r, sizeof(float) * N);
        grain_t *g1 = grain_create((float)FS);
        grain_t *g2 = grain_create((float)FS);
        grain_set_rate(g1, 77.0f); grain_set_time(g1, 9.0f); grain_set_spray(g1, 0.6f); grain_set_mix(g1, 0.8f);
        grain_set_rate(g2, 77.0f); grain_set_time(g2, 9.0f); grain_set_spray(g2, 0.6f); grain_set_mix(g2, 0.8f);
        grain_reset(g1); grain_reset(g2);
        memcpy(a, g_l, sizeof(float) * N); memcpy(a2, g_l, sizeof(float) * N);
        for (int i = 0; i < N; i += 256) {
            int n = (256 < N - i) ? 256 : N - i;
            grain_process(g1, a, ar, n);
            grain_process(g2, a2, ar2, n);
        }
        grain_destroy(g1); grain_destroy(g2);
        check("two reset instances are bit-identical (deterministic)", memcmp(a, a2, sizeof a) == 0, "");
    }

    /* 6 - relative cost */
    {
        const double cg = cost_of_grain();
        const double cr = cost_of_repeat();
        const double secs = 30.0;
        const double rtf = cg / secs;              /* share of one x86 core for realtime */
        snprintf(b, sizeof b, "(grain %.3f s, repeat %.3f s for 30 s audio -> %.2f%% of realtime, %.1fx repeat)",
                 cg, cr, rtf * 100.0, cr > 0.0 ? cg / cr : 0.0);
        /* the device is roughly 5-10x slower; under 5 % here keeps the stage inside the v1 CPU budget */
        check("grain stays under 5 % of realtime on x86 (device margin)", rtf < 0.05, b);
    }

    printf("\n%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
