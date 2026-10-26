/* flanger.c - MPC DJ FX through-zero flanger.
 *
 * DSP adapted from mda ThruZero (Copyright(c)1999-2000 Paul Kellett, mda; dual MIT / GPL-2.0-or-later -
 * this port elects the MIT branch). The structure is mda's: a 2048-sample delay line whose tap is
 * modulated by a saw LFO with a parabolic shape, read with linear interpolation, written back with
 * feedback, and SUBTRACTED from the dry signal (through-zero). Changes: fixed buffer (power of two),
 * 4 controls (rate/depth/feedback/mix) instead of mda's 5 (the depth-minimum parameter is gone, so the
 * delay reaches 0 at the modulation minimum), no VST2 host code.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "flanger.h"

#define FL_BUF 2048
#define FL_MASK (FL_BUF - 1)

struct flanger {
    float fs;
    float rat, dep, wet, dry, fb;
    float phi, fb1, fb2;
    float *buf[2];
    int    pos;
};

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static void update_rate(flanger_t *f, float v)
{
    v = clamp01(v);
    if (v < 0.01f) { f->rat = 0.0f; f->phi = 0.0f; }
    else           { f->rat = powf(10.0f, 3.0f * v - 2.0f) * 2.0f / f->fs; }
}

flanger_t *flanger_create(float fs)
{
    flanger_t *f = (flanger_t *)calloc(1, sizeof *f);
    if (!f) return NULL;
    f->fs = fs > 0.0f ? fs : 44100.0f;
    for (int ch = 0; ch < 2; ch++) {
        f->buf[ch] = (float *)calloc(FL_BUF, sizeof(float));
        if (!f->buf[ch]) { flanger_destroy(f); return NULL; }
    }
    f->wet = 0.5f; f->dry = 0.5f; f->fb = 0.0f;
    update_rate(f, 0.3f);
    flanger_set_depth(f, 0.5f);
    return f;
}

void flanger_destroy(flanger_t *f)
{
    if (!f) return;
    free(f->buf[0]);
    free(f->buf[1]);
    free(f);
}

void flanger_reset(flanger_t *f)
{
    if (!f) return;
    for (int ch = 0; ch < 2; ch++) memset(f->buf[ch], 0, FL_BUF * sizeof(float));
    f->phi = f->fb1 = f->fb2 = 0.0f;
    f->pos = 0;
}

void flanger_set_sample_rate(flanger_t *f, float fs)
{
    if (!f || fs <= 0.0f) return;
    const float v = (f->rat > 0.0f) ? (0.5f + log10f(f->rat * f->fs * 0.5f) / 3.0f) : 0.0f;
    f->fs = fs;
    update_rate(f, v);
    flanger_reset(f);
}

void flanger_set_rate(flanger_t *f, float v)  { if (f) update_rate(f, v); }
void flanger_set_depth(flanger_t *f, float v) { if (f) f->dep = 2000.0f * clamp01(v) * clamp01(v); }
void flanger_set_fbk(flanger_t *f, float v)   { if (f) f->fb = 1.9f * clamp01(v) - 0.95f; }
void flanger_set_mix(flanger_t *f, float v)
{
    if (!f) return;
    f->wet = clamp01(v);
    f->dry = 1.0f - f->wet;
}

void flanger_process(flanger_t *f, float *l, float *r, int n)
{
    if (!f || n <= 0) return;
    float ph = f->phi, f1 = f->fb1, f2 = f->fb2;
    int bp = f->pos;
    const float ra = f->rat, de = f->dep, we = f->wet, dr = f->dry, fb = f->fb;
    float *b0 = f->buf[0], *b1 = f->buf[1];

    for (int i = 0; i < n; i++) {
        const float a = l[i], b = r[i];

        ph += ra;
        if (ph > 1.0f) ph -= 2.0f;

        bp = (bp - 1) & FL_MASK;
        b0[bp] = a + fb * f1;
        b1[bp] = b + fb * f2;

        /* mda's parabolic delay shape: 0 at the modulation minimum (true through-zero), dep at the top */
        const float dpt = de * (1.0f - ph * ph);
        int ti = (int)dpt;
        const float frac = dpt - (float)ti;
        const int i0 = (ti + bp) & FL_MASK, i1 = (i0 + 1) & FL_MASK;
        f1 = b0[i0] + frac * (b0[i1] - b0[i0]);
        f2 = b1[i0] + frac * (b1[i1] - b1[i0]);

        l[i] = a * dr - f1 * we;     /* dry MINUS wet -> through-zero notches */
        r[i] = b * dr - f2 * we;
    }

    if (fabsf(f1) > 1.0e-10f) { f->fb1 = f1; f->fb2 = f2; } else { f->fb1 = f->fb2 = 0.0f; }
    f->phi = ph;
    f->pos = bp;
}
