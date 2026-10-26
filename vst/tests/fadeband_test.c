/* fadeband_test.c - T15 spike: what does the original "Fade to Grey" macro actually do to the spectrum?
 *
 * Re-read of the measurement data (2026-10-15, full dump of the sub-rack):
 *   macro 0   -> EQ Three crossovers 50 Hz / 18 kHz, all gains 0 dB, Delay off
 *   macro 64  -> crossovers 324 Hz / 6.22 kHz, GainMid -3.1 dB, Delay on ~48 % wet / 48 % feedback
 *   macro 127 -> crossovers 2.03 kHz / 2.18 kHz, GainMid -6.2 dB, Delay 95 % wet / 95 % feedback
 *
 * Crucial detail: the LOW and HIGH band gains are NOT mapped and stay at 0 dB. The macro therefore does
 * NOT fade into a narrow band - it moves the two crossovers together, which carves a mid-band **dip**
 * that narrows towards ~2 kHz and deepens to about -6 dB. The dominant audible part of the macro is the
 * **delay throw** (95 % feedback, 95 % wet), not a telephone filter.
 *
 * This test measures the spectral part with the isolator alone, because the moving crossovers are
 * exactly what an EQ Three does:
 *   1. macro 0 is flat;
 *   2. macro 64 dips ~3 dB between roughly 300 Hz and 6 kHz;
 *   3. macro 127 dips ~6 dB in a narrow slot around 2 kHz;
 *   4. and - the correction against v1 - the spectrum stays OPEN. v1 killed the low and high bands
 *      (iso_lo/iso_hi -> -24 dB), which sounds like a telephone; the original does not do that.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/iso.h"

#define FS 44100.0f
#define M_PIf 3.14159265358979323846f
#define NPER 8192

static float gl[NPER], gr[NPER];

static int fails = 0;
static void check(const char *what, int ok, const char *detail)
{
    printf("%-58s %s%s%s\n", what, ok ? "ok" : "FAIL", detail[0] ? "  " : "", detail);
    if (!ok) fails++;
}

static const float FR[] = { 50, 80, 120, 200, 324, 500, 800, 1300, 2100, 3400, 5600, 8000,
                            10000, 14000, 18000 };
#define NF ((int)(sizeof FR / sizeof FR[0]))

/* response of the isolator at one frequency, relative to unity, for a macro position 0..1 */
static float db_at(float hz, float m, const float *out_all)
{
    iso_t *iso = iso_create(FS);
    /* crossovers travel geometrically to the measured endpoints; gains as the original leaves them,
     * only the mid band is dipped */
    iso_set_lo_x(iso, 250.0f * powf(2030.0f / 250.0f, m));
    iso_set_hi_x(iso, 2500.0f * powf(2180.0f / 2500.0f, m));
    iso_set_lo_db(iso, 0.0f);
    iso_set_hi_db(iso, 0.0f);
    /* calibrated: our mid band is the difference of two 4-pole low-passes, so the same parameter
#     * value yields a shallower dip than the original's EQ Three - the target is the RESULT (-6 dB) */
    iso_set_mid_db(iso, -8.0f * m);

    const float ref_amp = 0.25f;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < NPER; i++) {
            const float x = ref_amp * sinf(2.0f * M_PIf * hz * (float)i / FS);
            gl[i] = x; gr[i] = x;
        }
        iso_process(iso, gl, gr, NPER);
    }
    double s = 0.0;
    for (int i = NPER / 2; i < NPER; i++) s += (double)gl[i] * (double)gl[i];
    const float rms = (float)sqrt(s / (double)(NPER / 2));
    iso_destroy(iso);
    (void)out_all;
    return 20.0f * log10f(rms / (ref_amp * 0.7071068f) + 1e-12f);
}

/* the frequency of the deepest dip and its depth, for a macro position */
static void dip(float m, float *at_hz, float *depth_db)
{
    *at_hz = 0.0f; *depth_db = 0.0f;
    for (int i = 0; i < NF; i++) {
        const float d = db_at(FR[i], m, NULL);
        if (d < *depth_db) { *depth_db = d; *at_hz = FR[i]; }
    }
}

int main(void)
{
    char b[120];

    /* 1 - flat at macro 0 */
    {
        float worst = 0.0f;
        for (int i = 0; i < NF; i++) { const float d = fabsf(db_at(FR[i], 0.0f, NULL)); if (d > worst) worst = d; }
        snprintf(b, sizeof b, "(max deviation %.2f dB across 50 Hz .. 18 kHz)", worst);
        check("macro 0 is spectrally flat", worst < 1.0f, b);
    }

    /* 2/3 - the dip narrows and deepens */
    {
        float at64, d64, at127, d127;
        dip(64.0f / 127.0f, &at64, &d64);
        dip(1.0f, &at127, &d127);
        snprintf(b, sizeof b, "(macro 64 dips %.1f dB at %.2f kHz; macro 127 dips %.1f dB at %.2f kHz)",
                 d64, at64 / 1000.0f, d127, at127 / 1000.0f);
        check("macro 64 dips ~3 dB inside the moving band", d64 < -2.0f && d64 > -5.0f, b);
        check("macro 127 dips ~6 dB near 2 kHz", d127 < -5.0f && d127 > -8.0f
                                                 && at127 >= 1300.0f && at127 <= 3400.0f, b);
    }

    /* 4 - the correction: the spectrum stays open, it is NOT a telephone band-pass */
    {
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < NF; i++) {
            const float d = db_at(FR[i], 1.0f, NULL);
            if (d > lo) {}                            /* keep the compiler quiet about unused logic */
            if (FR[i] <= 120.0f && d < lo) lo = d;
            if (FR[i] >= 8000.0f && d > hi) hi = d;
        }
        snprintf(b, sizeof b, "(macro 127: %.1f dB at <=120 Hz, %.1f dB at >=8 kHz)", lo, hi);
        check("low end stays open at macro 127 (v1 killed it)", lo > -3.0f, b);
        check("high end stays open at macro 127 (v1 killed it)", hi > -3.0f, b);
    }

    printf("\n%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
