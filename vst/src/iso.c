/* iso.c - MPC DJ FX 3-band isolator (own code; see include/iso.h).
 *
 * A classic DJ isolator built from three biquads in series (RBJ cookbook):
 *     LOW  = low shelf  at lo_x
 *     MID  = peaking EQ at sqrt(lo_x * hi_x) (Q = 0.7)
 *     HI   = high shelf at hi_x
 * At 0 dB every biquad is exactly unity (a shelf/peak with unity gain is the identity), so the
 * isolator is transparent; the bottom of a band's range is a deep shelf/peak (-60 dB) = kill.
 * This replaces an earlier complementary-split design, which summed exactly but could not kill a
 * band (the two low-passes are phase-shifted, so subtracting one does not remove the other's band).
 */
#include <math.h>
#include <stdlib.h>
#include "iso.h"

#define ISO_PI 3.14159265358979323846f
#define ISO_KILL_DB (-60.0f)   /* what the bottom of the range means: a kill */

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } bq_t;

#define ISO_SECT 2   /* cascaded shelf sections -> a steeper, tidier transition (less band bleed) */

struct iso {
    float fs;
    float lo_x, hi_x;       /* remembered so a sample-rate change can retune */
    bq_t  low[2][ISO_SECT], mid[2], high[2][ISO_SECT];
};

static float gain_db(float db)
{
    return (db <= -23.9f) ? ISO_KILL_DB : db;   /* bottom of the range = kill */
}

static void bq_reset(bq_t *f) { f->z1 = f->z2 = 0.0f; }

static inline float bq_tick(bq_t *f, float x)   /* transposed direct form II */
{
    const float y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}

static void set_low_shelf(bq_t *f, float fs, float f0, float db)
{
    const float A = powf(10.0f, db / 40.0f);
    const float w = 2.0f * ISO_PI * f0 / fs;
    const float cw = cosf(w), sw = sinf(w);
    const float alpha = sw * 0.5f * 1.41421356f;          /* S = 1 */
    const float sA = sqrtf(A);
    const float a0 =  (A + 1.0f) + (A - 1.0f) * cw + 2.0f * sA * alpha;
    f->b0 =  A * ((A + 1.0f) - (A - 1.0f) * cw + 2.0f * sA * alpha) / a0;
    f->b1 =  2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw) / a0;
    f->b2 =  A * ((A + 1.0f) - (A - 1.0f) * cw - 2.0f * sA * alpha) / a0;
    f->a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cw) / a0;
    f->a2 = ((A + 1.0f) + (A - 1.0f) * cw - 2.0f * sA * alpha) / a0;
}

static void set_high_shelf(bq_t *f, float fs, float f0, float db)
{
    const float A = powf(10.0f, db / 40.0f);
    const float w = 2.0f * ISO_PI * f0 / fs;
    const float cw = cosf(w), sw = sinf(w);
    const float alpha = sw * 0.5f * 1.41421356f;          /* S = 1 */
    const float sA = sqrtf(A);
    const float a0 =  (A + 1.0f) - (A - 1.0f) * cw + 2.0f * sA * alpha;
    f->b0 =  A * ((A + 1.0f) + (A - 1.0f) * cw + 2.0f * sA * alpha) / a0;
    f->b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw) / a0;
    f->b2 =  A * ((A + 1.0f) + (A - 1.0f) * cw - 2.0f * sA * alpha) / a0;
    f->a1 =  2.0f * ((A - 1.0f) - (A + 1.0f) * cw) / a0;
    f->a2 = ((A + 1.0f) - (A - 1.0f) * cw - 2.0f * sA * alpha) / a0;
}

static void set_peak(bq_t *f, float fs, float f0, float db, float q)
{
    const float A = powf(10.0f, db / 40.0f);
    const float w = 2.0f * ISO_PI * f0 / fs;
    const float cw = cosf(w), sw = sinf(w);
    const float alpha = sw / (2.0f * q);
    const float a0 = 1.0f + alpha / A;
    f->b0 = (1.0f + alpha * A) / a0;
    f->b1 = (-2.0f * cw) / a0;
    f->b2 = (1.0f - alpha * A) / a0;
    f->a1 = (-2.0f * cw) / a0;
    f->a2 = (1.0f - alpha / A) / a0;
}

iso_t *iso_create(float fs)
{
    iso_t *s = (iso_t *)calloc(1, sizeof *s);
    if (!s) return NULL;
    s->fs = fs > 0.0f ? fs : 44100.0f;
    s->lo_x = 250.0f;
    s->hi_x = 2500.0f;
    for (int ch = 0; ch < 2; ch++) {
        for (int k = 0; k < ISO_SECT; k++) {
            set_low_shelf(&s->low[ch][k], s->fs, s->lo_x, 0.0f);
            set_high_shelf(&s->high[ch][k], s->fs, s->hi_x, 0.0f);
        }
        set_peak(&s->mid[ch], s->fs, sqrtf(s->lo_x * s->hi_x), 0.0f, 2.0f);
    }
    return s;
}

void iso_destroy(iso_t *s) { free(s); }

void iso_reset(iso_t *s)
{
    if (!s) return;
    for (int ch = 0; ch < 2; ch++) {
        for (int k = 0; k < ISO_SECT; k++) { bq_reset(&s->low[ch][k]); bq_reset(&s->high[ch][k]); }
        bq_reset(&s->mid[ch]);
    }
}

void iso_set_lo_db(iso_t *s, float db)
{
    if (!s) return;
    const float half = gain_db(db) * 0.5f;
    for (int ch = 0; ch < 2; ch++)
        for (int k = 0; k < ISO_SECT; k++)
            set_low_shelf(&s->low[ch][k], s->fs, s->lo_x, half);
}

void iso_set_mid_db(iso_t *s, float db)
{
    if (!s) return;
    const float f0 = sqrtf(s->lo_x * s->hi_x);
    for (int ch = 0; ch < 2; ch++) set_peak(&s->mid[ch], s->fs, f0, gain_db(db), 2.0f);
}

void iso_set_hi_db(iso_t *s, float db)
{
    if (!s) return;
    const float half = gain_db(db) * 0.5f;
    for (int ch = 0; ch < 2; ch++)
        for (int k = 0; k < ISO_SECT; k++)
            set_high_shelf(&s->high[ch][k], s->fs, s->hi_x, half);
}

void iso_set_lo_x(iso_t *s, float hz)
{
    if (!s) return;
    if (hz < 40.0f) hz = 40.0f;
    if (hz > 1000.0f) hz = 1000.0f;
    s->lo_x = hz;
    /* keep the low shelf and the mid peak in step; the current band gains are re-applied by the caller */
    for (int ch = 0; ch < 2; ch++) {
        for (int k = 0; k < ISO_SECT; k++) set_low_shelf(&s->low[ch][k], s->fs, s->lo_x, 0.0f);
        set_peak(&s->mid[ch], s->fs, sqrtf(s->lo_x * s->hi_x), 0.0f, 2.0f);
    }
}

void iso_set_hi_x(iso_t *s, float hz)
{
    if (!s) return;
    if (hz < 500.0f) hz = 500.0f;
    if (hz > 12000.0f) hz = 12000.0f;
    s->hi_x = hz;
    for (int ch = 0; ch < 2; ch++) {
        for (int k = 0; k < ISO_SECT; k++) set_high_shelf(&s->high[ch][k], s->fs, s->hi_x, 0.0f);
        set_peak(&s->mid[ch], s->fs, sqrtf(s->lo_x * s->hi_x), 0.0f, 2.0f);
    }
}

void iso_set_sample_rate(iso_t *s, float fs)
{
    if (!s || fs <= 0.0f) return;
    s->fs = fs;
    iso_set_lo_x(s, s->lo_x);
    iso_set_hi_x(s, s->hi_x);
    iso_reset(s);
}

void iso_process(iso_t *s, float *l, float *r, int n)
{
    if (!s || n <= 0) return;
    for (int ch = 0; ch < 2; ch++) {
        float *x = ch ? r : l;
        bq_t *md = &s->mid[ch];
        for (int i = 0; i < n; i++) {
            float y = x[i];
            for (int k = 0; k < ISO_SECT; k++) y = bq_tick(&s->low[ch][k], y);
            y = bq_tick(md, y);
            for (int k = 0; k < ISO_SECT; k++) y = bq_tick(&s->high[ch][k], y);
            x[i] = y;
        }
    }
}
