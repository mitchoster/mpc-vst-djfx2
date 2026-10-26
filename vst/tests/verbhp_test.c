/* verbhp_test.c — proves the VERB macro's wet-path high-pass is really in the audio path.
 *
 * macro_test reads the parameter cell back and dubfilter_test proves the filter itself, so a forgotten
 * dub_filter_process() call in the chain would pass both. This test links the whole engine and measures
 * the wet output instead.
 *
 * Method: the wet bus wide open (mix 1) but mac_verb at 0, so the two settings differ in the HPF alone;
 * the flanger and grain are muted, which leaves a static LTI chain and makes the level ratio the
 * filter's own gain ratio. Measured with the engine defaults: 26 Hz (neutral) vs 15.4 kHz / Q 2.5
 * (the macro's top) -> ~-77 dB at 200 Hz, ~-12.4 dB at 10 kHz.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "engine.h"

const mpc_engine_t *mpc_engine(void);
static const mpc_engine_t *E;

#define BLOCK 128
#define FS    44100
#define TOTAL (FS * 3)                 /* 3 s; the last second is measured (delay is 350 ms) */

static int fails = 0;
static void check(const char *what, int ok, const char *detail)
{
    printf("%-58s %s%s%s\n", what, ok ? "ok" : "FAIL", detail[0] ? "  " : "", detail);
    if (!ok) fails++;
}

/* RMS of the wet output for a steady sine at f0 (both channels) */
static double wet_rms(void *inst, double f0)
{
    E->set_param(inst, "reset", "1");
    int16_t in[BLOCK * 2], out[BLOCK * 2];
    double acc = 0.0;
    int counted = 0;
    for (int i = 0; i < TOTAL; i += BLOCK) {
        for (int k = 0; k < BLOCK; k++) {
            const float x = 0.1f * (float)sin(2.0 * M_PI * f0 * (double)(i + k) / (double)FS);
            const int16_t s = (int16_t)lrintf(x * 32768.0f);
            in[2 * k] = s; in[2 * k + 1] = s;
        }
        memset(out, 0, sizeof out);
        E->process(inst, in, out, BLOCK);
        if (i >= TOTAL - FS)
            for (int k = 0; k < BLOCK; k++) {
                const double a = out[2 * k] / 32768.0, b = out[2 * k + 1] / 32768.0;
                acc += a * a + b * b; counted += 2;
            }
    }
    return sqrt(acc / (double)counted);
}

int main(void)
{
    E = mpc_engine();
    void *inst = E->create(NULL);
    if (!inst) { printf("engine create failed\n"); return 2; }

    E->set_param(inst, "sample_rate", "44100");
    E->set_param(inst, "limiter", "0");
    E->set_param(inst, "fl_mix", "0");            /* keep the chain static -> the ratio is the filter's */
    E->set_param(inst, "mac_verb", "0");          /* reverb/delay feedback at neutral... */
    E->set_param(inst, "mix", "1");               /* ...but the wet bus itself wide open */

    char b[160];
    const double f_lo = 200.0, f_hi = 10000.0;

    E->set_param(inst, "hp_hz", "26");    E->set_param(inst, "hp_reso", "0");
    const double lo_neutral = wet_rms(inst, f_lo);
    const double hi_neutral = wet_rms(inst, f_hi);

    E->set_param(inst, "hp_hz", "15400"); E->set_param(inst, "hp_reso", "1");
    const double lo_hp = wet_rms(inst, f_lo);
    const double hi_hp = wet_rms(inst, f_hi);

    /* the reference must be audible, or both "drops" would be a silent wet path */
    snprintf(b, sizeof b, "(wet 200 Hz rms %.4f, 10 kHz rms %.4f)", lo_neutral, hi_neutral);
    check("the neutral wet bus carries the test tones (no silent-path false pass)", lo_neutral > 1e-2 && hi_neutral > 1e-2, b);

    const double d_lo = 20.0 * log10((lo_hp > 1e-9 ? lo_hp : 1e-9) / lo_neutral);
    snprintf(b, sizeof b, "(200 Hz %.4f -> %.6f = %.1f dB)", lo_neutral, lo_hp, d_lo);
    check("HPF at 15.4 kHz removes the wet 200 Hz tone (drop >= 40 dB)", d_lo < -40.0, b);

    const double d_hi = 20.0 * log10(hi_hp / hi_neutral);
    snprintf(b, sizeof b, "(10 kHz %.4f -> %.4f = %.1f dB; analytically -12.4 dB)", hi_neutral, hi_hp, d_hi);
    check("... while 10 kHz still passes (drop <= 15 dB)", d_hi > -15.0, b);

    E->destroy(inst);
    printf("\n%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
