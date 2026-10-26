/* macro_audio_test.c — does every macro actually reach the output, and does the delay behave?
 *
 * macro_test reads the parameter cells back; it cannot see that `mac_glitch` wrote three parameters
 * inside a bus that nothing could hear. On the device that macro did nothing at all (bit-identical
 * output), which this test catches: every macro must change the audio measurably.
 *
 * Second check: `dly_fbk` is the real feedback gain. It used to be scaled by 1.2 internally, so the
 * measured 0.95 of the VERB/FADE macros became 1.14 and the delay swelled to its limiter instead of
 * decaying ("Fade to Grey only makes the echo louder", heard on the device 2026-10-16). A repeat tail
 * at 0.95 must decay over 6 s.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "engine.h"

const mpc_engine_t *mpc_engine(void);
static const mpc_engine_t *E;

#define BLOCK 128
#define FS    44100
#define TOTAL (FS * 6)

static int fails = 0;
static void check(const char *what, int ok, const char *detail)
{
    printf("%-58s %s%s%s\n", what, ok ? "ok" : "FAIL", detail[0] ? "  " : "", detail);
    if (!ok) fails++;
}

static float *g_in, *g_out;
static unsigned g_rng = 12345u;
static float noise(void) { g_rng = g_rng * 1103515245u + 12345u; return (float)((g_rng >> 16) & 0x7fff) / 16384.0f - 1.0f; }

/* burst = 1: half a second of signal, then silence - a delay tail can only be judged after the input
 * stops (with continuous input any feedback fills up towards 1/(1-fbk), which looks like growth). */
static void make_input(int burst)
{
    g_rng = 12345u;
    for (int i = 0; i < TOTAL; i++) {
        float x = 0.25f * noise();
        if ((i % 11025) < 200) x += 0.5f;                 /* a click every 250 ms */
        g_in[i] = (burst && i > FS / 2) ? 0.0f : x;
    }
}

/* render `frames` samples with the macros neutral except `mac` = `m` */
static void render(const char *mac, const char *m, float *out, int frames)
{
    void *inst = E->create(NULL);
    E->set_param(inst, "sample_rate", "44100");
    E->set_param(inst, "mac_lo", "1"); E->set_param(inst, "mac_mid", "1");
    E->set_param(inst, "mac_hi", "1"); E->set_param(inst, "mac_lpf", "1");
    E->set_param(inst, "mac_glitch", "0"); E->set_param(inst, "mac_verb", "0");
    E->set_param(inst, "mac_fade", "0");  E->set_param(inst, "mac_fill", "0");
    if (mac) E->set_param(inst, mac, m);
    int16_t in[BLOCK * 2], o[BLOCK * 2];
    for (int i = 0; i < frames; i += BLOCK) {
        const int n = (frames - i) < BLOCK ? (frames - i) : BLOCK;
        for (int k = 0; k < n; k++) {
            float a = g_in[i + k]; if (a > 1) a = 1; if (a < -1) a = -1;
            const int16_t s = (int16_t)lrintf(a * 32767.0f);
            in[2 * k] = s; in[2 * k + 1] = s;
        }
        memset(o, 0, sizeof o);
        E->process(inst, in, o, n);
        for (int k = 0; k < n; k++) out[i + k] = (float)o[2 * k] / 32768.0f;
    }
    E->destroy(inst);
}

static double rms(const float *b, int from, int to)
{
    double s = 0;
    for (int i = from; i < to; i++) s += (double)b[i] * b[i];
    return sqrt(s / (double)(to - from));
}

static const char *getp(void *inst, const char *k)
{
    static char b[64];
    if (E->get_param(inst, k, b, sizeof b) <= 0) b[0] = 0;
    return b;
}

int main(void)
{
    E = mpc_engine();
    g_in = malloc(sizeof(float) * TOTAL);
    float *a = malloc(sizeof(float) * TOTAL), *b = malloc(sizeof(float) * TOTAL);
    if (!g_in || !a || !b) return 2;
    make_input(0);
    char d[160];

    /* 1) every macro must change the output */
    static const char *MACS[8] = { "mac_lo", "mac_mid", "mac_hi", "mac_lpf",
                                   "mac_glitch", "mac_verb", "mac_fade", "mac_fill" };
    for (int k = 0; k < 8; k++) {
        render(MACS[k], "0", a, TOTAL);
        render(MACS[k], "1", b, TOTAL);
        double num = 0, den = 0;
        for (int i = FS / 2; i < TOTAL; i++) { const double dd = (double)b[i] - a[i]; num += dd * dd; den += (double)a[i] * a[i]; }
        const double diff = sqrt(num / den);
        snprintf(d, sizeof d, "(diff/ref %.4f at macro 1)", diff);
        char what[80]; snprintf(what, sizeof what, "%s reaches the audio output", MACS[k]);
        check(what, diff > 0.05, d);
    }

    /* 2) the delay tail decays at dly_fbk 0.95 - no hidden gain, no runaway. Judged after the input
     *    stops (a burst), otherwise the tail is still filling up. */
    make_input(1);
    {
        const int n = FS * 6;
        render("mac_fade", "1", b, n);                     /* mac_fade drives dly_fbk 0.95, dly_mix 0.95 */
        const double early = rms(b, FS * 1, FS * 2);
        const double late  = rms(b, FS * 4, FS * 5);
        const double db = 20.0 * log10((late > 1e-9 ? late : 1e-9) / (early > 1e-9 ? early : 1e-9));
        snprintf(d, sizeof d, "(after the burst: 1..2 s %.4f -> 4..5 s %.4f = %+.1f dB)", early, late, db);
        check("a 95 % feedback tail decays (no self-oscillation in the macros)", db < -1.0, d);
    }

    /* 3) the parameter is the real gain: 0.95 decays, the old 1.14 would climb */
    {
        const int n = FS * 6;
        void *inst = E->create(NULL);
        E->set_param(inst, "sample_rate", "44100");
        E->set_param(inst, "mix", "1"); E->set_param(inst, "dly_mix", "1");
        E->set_param(inst, "dly_fbk", "0.95");
        int16_t in[BLOCK * 2], o[BLOCK * 2];
        for (int i = 0; i < n; i += BLOCK) {
            for (int k = 0; k < BLOCK; k++) {
                const float a2 = g_in[i + k];
                const int16_t s = (int16_t)lrintf(a2 * 32767.0f);
                in[2 * k] = s; in[2 * k + 1] = s;
            }
            memset(o, 0, sizeof o);
            E->process(inst, in, o, BLOCK);
            for (int k = 0; k < BLOCK; k++) b[i + k] = (float)o[2 * k] / 32768.0f;
        }
        E->destroy(inst);
        const double e = rms(b, FS * 1, FS * 2), l = rms(b, FS * 4, FS * 5);
        const double db = 20.0 * log10((l > 1e-9 ? l : 1e-9) / (e > 1e-9 ? e : 1e-9));
        snprintf(d, sizeof d, "(%.4f -> %.4f = %+.1f dB; the old 1.14 climbed instead)", e, l, db);
        check("dly_fbk 0.95 decays like 95 %, not like the old 114 %", db < -1.0, d);
    }

    /* 4) two macros must be usable at the same time: they share the wet amount (`mix`), and the last
     *    macro used to win the cell - so turning Fade down muted the reverb Verb had opened.
     *    ("im moment springt Verb wenn ich Fade nutze", owner on the device, 2026-10-16.) */
    {
        void *inst = E->create(NULL);
        E->set_param(inst, "sample_rate", "44100");
        E->set_param(inst, "mac_verb", "0.8");
        const double mix_verb = atof(getp(inst, "mix"));
        E->set_param(inst, "mac_fade", "0");
        const double mix_fade0 = atof(getp(inst, "mix"));
        E->set_param(inst, "mac_fade", "0.8");
        const double mix_both = atof(getp(inst, "mix"));
        snprintf(d, sizeof d, "(mix: verb .8 -> %.3f, then fade 0 -> %.3f, then fade .8 -> %.3f)",
                 mix_verb, mix_fade0, mix_both);
        check("Fade does not mute the wet bus Verb opened (the macros combine)",
              mix_fade0 >= mix_verb - 1e-6 && mix_both >= mix_verb - 1e-6, d);
        E->destroy(inst);
    }

    free(g_in); free(a); free(b);
    printf("\n%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
