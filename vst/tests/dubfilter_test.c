/* dubfilter_test.c — unit test for the shared dub_filter, focused on what DjFx2 added for the VERB
 * macro's wet-path high-pass:
 *   1. hp_only is a pure 2nd-order HPF: its measured response matches the analytic RBJ HPF (a stray
 *      LPF would show up as a big deviation at 16-20 kHz), and the full mode still has its LPF, i.e.
 *      the flag is load-bearing.
 *   2. the corner boost equals 20*log10(Q): the resonance knob is a real Q, which is the unit the VERB
 *      macro's measured "reso 0.51 -> 2.50" is in.
 *   3. disabled = bit-transparent.
 *
 * Note on the settling window: dub_filter smooths its controls once per control-rate tick (every 32
 * samples, see DUB_FILTER_CTRL), so a cutoff step needs ~2 s to converge even though the coefficient
 * itself is a "8 ms" one-pole. Measuring earlier reads the glide, not the filter (it cost 1.1 dB
 * before this was understood).
 */
#include <stdio.h>
#include <math.h>
#include "../include/dub_filter.h"

#define FS  44100.0
#define NS  (5 * 44100 / 2)             /* 2.5 s: 2 s settling, then a 0.5 s measurement window */
#define NW  22050                       /* the measured window (0.5 s) */

static int fails = 0;
static void check(const char *what, int ok, const char *detail)
{
    printf("%-58s %s%s%s\n", what, ok ? "ok" : "FAIL", detail[0] ? "  " : "", detail);
    if (!ok) fails++;
}

/* Analytic RBJ biquad magnitude, same cookbook as dub_filter.c (bq_highpass/bq_lowpass). */
static double bq_gain_db(int hp, double f0, double Q, double f)
{
    const double w0 = 2.0 * M_PI * f0 / FS, cw = cos(w0), sw = sin(w0), alpha = sw / (2.0 * Q), a0 = 1.0 + alpha;
    double b0, b1, b2;
    if (hp) { b0 = (1.0 + cw) * 0.5 / a0; b1 = -(1.0 + cw) / a0; b2 = b0; }
    else    { b0 = (1.0 - cw) * 0.5 / a0; b1 = (1.0 - cw) / a0;    b2 = b0; }
    const double a1 = -2.0 * cw / a0, a2 = (1.0 - alpha) / a0;
    const double w = 2.0 * M_PI * f / FS;
    const double z1r = cos(w), z1i = -sin(w), z2r = cos(2.0 * w), z2i = -sin(2.0 * w);
    const double nr = b0 + b1 * z1r + b2 * z2r, ni = b1 * z1i + b2 * z2i;
    const double dr = 1.0 + a1 * z1r + a2 * z2r, di = a1 * z1i + a2 * z2i;
    return 10.0 * log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

static float in_l[NS], buf_l[NS], buf_r[NS];

/* Feed a sine, return the steady-state output/input gain in dB. Both RMS values come from the same
 * float reference, so a bypassed filter measures exactly 0.0. */
static double measure_db(dub_filter_t *f, double f0)
{
    for (int i = 0; i < NS; i++) {
        const float x = 0.5f * (float)sin(2.0 * M_PI * f0 * (double)i / FS);
        in_l[i] = x; buf_l[i] = x; buf_r[i] = x;
    }
    dub_filter_reset(f);
    for (int i = 0; i < NS; i += 128) {
        const int n = (NS - i) < 128 ? (NS - i) : 128;
        dub_filter_process(f, &buf_l[i], &buf_r[i], n);
    }
    double so = 0.0, si = 0.0;
    for (int i = NS - NW; i < NS; i++) { so += (double)buf_l[i] * buf_l[i]; si += (double)in_l[i] * in_l[i]; }
    return 10.0 * log10(so / si);
}

int main(void)
{
    dub_filter_t *f = dub_filter_create((float)FS);
    if (!f) { printf("dub_filter_create failed\n"); return 2; }
    char b[160];

    const double FC = 633.0;                        /* mac_verb at half travel: 26 Hz -> 15.4 kHz geometric */
    dub_filter_set_enable(f, 1);
    dub_filter_set_hp_only(f, 1);
    dub_filter_set_hpf(f, (float)FC);
    dub_filter_set_resonance(f, 0.5f);              /* res 0..1 == Q 0.5..2.5, so 0.5 == Q 1.5 */
    measure_db(f, 1000.0);                          /* settle the control-rate smoothing before measuring */

    /* 1) hp_only is a pure HPF */
    {
        const double fts[] = { 50, 200, 633, 2000, 10000, 16000, 20000 };
        int all = 1; double worst = 0.0, worstf = 0.0;
        for (unsigned k = 0; k < sizeof fts / sizeof fts[0]; k++) {
            const double got = measure_db(f, fts[k]);
            const double want = bq_gain_db(1, FC, 1.5, fts[k]);
            const double d = fabs(got - want);
            if (d > worst) { worst = d; worstf = fts[k]; }
            if (d > 0.15) all = 0;
        }
        snprintf(b, sizeof b, "(worst %.3f dB at %.0f Hz vs the analytic RBJ HPF)", worst, worstf);
        check("hp_only matches a pure 2nd-order HPF (no LPF in the path)", all, b);
    }

    /* 1b) the same measurement in full mode must show the LPF that hp_only removes */
    {
        dub_filter_set_hp_only(f, 0);
        const double got = measure_db(f, 20000.0);
        const double hp = bq_gain_db(1, FC, 1.5, 20000.0);
        snprintf(b, sizeof b, "(full mode %+.2f dB at 20 kHz, HPF alone %+.2f dB)", got, hp);
        check("full mode keeps its LPF (the flag is load-bearing)", fabs(got - hp) > 0.8, b);
        dub_filter_set_hp_only(f, 1);
    }

    /* 2) the corner boost is 20*log10(Q) */
    {
        const double qs[] = { 0.5, 1.0, 2.5 };
        int all = 1; double worst = 0.0, worstq = 0.0;
        for (unsigned k = 0; k < sizeof qs / sizeof qs[0]; k++) {
            dub_filter_set_resonance(f, (float)((qs[k] - 0.5) / 2.0));
            const double got = measure_db(f, FC);
            const double want = 20.0 * log10(qs[k]);
            const double d = fabs(got - want);
            if (d > worst) { worst = d; worstq = qs[k]; }
            if (d > 0.15) all = 0;
        }
        snprintf(b, sizeof b, "(worst %.3f dB, at Q %.2f)", worst, worstq);
        check("corner boost = 20*log10(Q) for Q 0.5 / 1.0 / 2.5", all, b);
    }

    /* 3) bypass */
    dub_filter_set_resonance(f, 0.5f);
    dub_filter_set_enable(f, 0);
    check("a disabled filter is bit-transparent", measure_db(f, 1000.0) == 0.0, "");
    dub_filter_set_enable(f, 1);

    dub_filter_destroy(f);
    printf("\n%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
