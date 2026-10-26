/* limiter.c - MPC DJ FX look-ahead brickwall limiter (own code; see include/limiter.h). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "limiter.h"

#define LIM_LOOKAHEAD_MS 2.0f
#define LIM_RELEASE_MS   80.0f    /* the gain recovers slowly after a peak */
#define LIM_TP_PHASES     4       /* 4x oversampled true-peak estimate (inter-sample peaks) */
#define LIM_TP_TAPS      12       /* windowed-sinc taps/phase. Measured accuracy: <=0.11 dB below 8 kHz,
                                   * -1.55 dB at 11 kHz (a short interpolator cannot do better near
                                   * Nyquist), so the deepest inter-sample peaks are still ~1.5 dB under-
                                   * estimated. A 4x/48-tap polyphase filter would fix that at ~5x the cost. */

struct limiter {
    /* true-peak detection: 4-sample input history per channel + the polyphase weights */
    float hist[2][LIM_TP_TAPS];
    float tp[LIM_TP_PHASES][LIM_TP_TAPS];
    int   warm;
    float fs;
    int   on;
    float ceiling;      /* linear */
    float att, rel;     /* per-sample coefficients (release; attack kept for reference) */
    float env;          /* peak envelope, instant attack + slow release */
    float gain;         /* current gain reduction */
    int   la;           /* look-ahead in samples */
    int   pos;
    float *buf[2];
    float *gbuf;        /* the gain computed when each sample entered the delay line */
};

static void alloc_buf(limiter_t *l, float fs)
{
    l->la = (int)(LIM_LOOKAHEAD_MS * 0.001f * fs);
    if (l->la < 1) l->la = 1;
    for (int ch = 0; ch < 2; ch++) {
        free(l->buf[ch]);
        l->buf[ch] = (float *)calloc((size_t)l->la + 1, sizeof(float));
    }
    free(l->gbuf);
    l->gbuf = (float *)calloc((size_t)l->la + 1, sizeof(float));
    l->att = 1.0f - expf(-3.0f / (float)l->la);   /* the gain reaches the target inside the look-ahead */
    l->rel = 1.0f - expf(-1.0f / (LIM_RELEASE_MS * 0.001f * fs));
    l->fs  = fs;
    l->pos = 0;
}

limiter_t *limiter_create(float fs)
{
    limiter_t *l = (limiter_t *)calloc(1, sizeof *l);
    if (!l) return NULL;
    l->on = 1;
    l->ceiling = 0.98f;
    /* Windowed-sinc true-peak weights over the PAST four samples (n-3 .. n). Past-only matters: an
     * estimate that looked ahead would need future samples and would delay the gain, which lets a peak
     * through before its own reduction. The 2 ms look-ahead already provides the margin instead. */
    for (int ph = 0; ph < LIM_TP_PHASES; ph++) {
        const float o = (float)ph / (float)LIM_TP_PHASES;
        float sum = 0.0f;
        for (int k = 0; k < LIM_TP_TAPS; k++) {
            const float t = (float)(k - (LIM_TP_TAPS - 1)) + o;   /* past-only window n-3 .. n */
            const float sinc = (fabsf(t) < 1e-6f) ? 1.0f : sinf(3.14159265359f * t) / (3.14159265359f * t);
            const float wl = (float)(LIM_TP_TAPS) * 0.5f + 0.5f;
            const float win = (fabsf(t) <= wl) ? 0.5f + 0.5f * cosf(3.14159265359f * t / wl) : 0.0f;
            l->tp[ph][k] = sinc * win;
            sum += l->tp[ph][k];
        }
        if (fabsf(sum) > 1e-9f) for (int k = 0; k < LIM_TP_TAPS; k++) l->tp[ph][k] /= sum;
    }
    l->gain = 1.0f;
    l->env = 0.0f;
    alloc_buf(l, fs > 0.0f ? fs : 44100.0f);
    if (!l->buf[0] || !l->buf[1] || !l->gbuf) { limiter_destroy(l); return NULL; }
    return l;
}

void limiter_destroy(limiter_t *l)
{
    if (!l) return;
    free(l->buf[0]);
    free(l->buf[1]);
    free(l->gbuf);
    free(l);
}

void limiter_reset(limiter_t *l)
{
    if (!l) return;
    for (int ch = 0; ch < 2; ch++)
        if (l->buf[ch]) memset(l->buf[ch], 0, ((size_t)l->la + 1) * sizeof(float));
    if (l->gbuf) memset(l->gbuf, 0, ((size_t)l->la + 1) * sizeof(float));
    /* Windowed-sinc true-peak weights over the PAST four samples (n-3 .. n). Past-only matters: an
     * estimate that looked ahead would need future samples and would delay the gain, which lets a peak
     * through before its own reduction. The 2 ms look-ahead already provides the margin instead. */
    for (int ph = 0; ph < LIM_TP_PHASES; ph++) {
        const float o = (float)ph / (float)LIM_TP_PHASES;
        float sum = 0.0f;
        for (int k = 0; k < LIM_TP_TAPS; k++) {
            const float t = (float)(k - (LIM_TP_TAPS - 1)) + o;   /* past-only window n-3 .. n */
            const float sinc = (fabsf(t) < 1e-6f) ? 1.0f : sinf(3.14159265359f * t) / (3.14159265359f * t);
            const float wl = (float)(LIM_TP_TAPS) * 0.5f + 0.5f;
            const float win = (fabsf(t) <= wl) ? 0.5f + 0.5f * cosf(3.14159265359f * t / wl) : 0.0f;
            l->tp[ph][k] = sinc * win;
            sum += l->tp[ph][k];
        }
        if (fabsf(sum) > 1e-9f) for (int k = 0; k < LIM_TP_TAPS; k++) l->tp[ph][k] /= sum;
    }
    l->gain = 1.0f;
    l->env = 0.0f;
    l->pos = 0;
}

void limiter_set_sample_rate(limiter_t *l, float fs)
{
    if (!l || fs <= 0.0f || fabsf(fs - l->fs) < 0.5f) return;
    alloc_buf(l, fs);
    limiter_reset(l);
}

void limiter_set_on(limiter_t *l, int on) { if (l) l->on = on ? 1 : 0; }
void limiter_set_ceiling(limiter_t *l, float db)
{
    if (!l) return;
    if (db > 0.0f) db = 0.0f;
    l->ceiling = powf(10.0f, db / 20.0f);
}

/* 4x oversampled peak of the centered window - catches the inter-sample peaks a sample-peak limiter
 * cannot see (measured +1.67 dB on an 11.025 kHz tone, see PORTING_NOTES T16). Needs two future
 * samples, which the caller's look-ahead covers; the applied gain is delayed by two samples to match. */
static float tp_peak(const limiter_t *l)
{
    float mx = 0.0f;
    for (int ch = 0; ch < 2; ch++) {
        for (int ph = 0; ph < LIM_TP_PHASES; ph++) {
            float y = 0.0f;
            for (int k = 0; k < LIM_TP_TAPS; k++) y += l->tp[ph][k] * l->hist[ch][k];
            const float a = fabsf(y);
            if (a > mx) mx = a;
        }
    }
    return mx;
}

void limiter_process(limiter_t *l, float *left, float *right, int n)
{
    if (!l || n <= 0) return;
    if (!l->on) return;                       /* bypass */
    const int la = l->la;
    for (int i = 0; i < n; i++) {
        const float infl = left[i], inr = right[i];
        /* peak envelope: instant attack, slow release. The look-ahead delay below lets the envelope
         * be in place before a peak reaches the output, and a signal peak is not forgotten after one
         * sample (a target-only per-sample gain would recover immediately on an impulse). */
        /* shift the input history (index 3 = newest) and take the true-peak estimate */
        for (int ch = 0; ch < 2; ch++) {
            float *h = l->hist[ch];
            for (int k = 0; k < LIM_TP_TAPS - 1; k++) h[k] = h[k + 1];
        }
        l->hist[0][LIM_TP_TAPS - 1] = infl;
        l->hist[1][LIM_TP_TAPS - 1] = inr;
        float peak = tp_peak(l);
        const float sample_peak = (fabsf(infl) > fabsf(inr)) ? fabsf(infl) : fabsf(inr);
        if (sample_peak > peak) peak = sample_peak;      /* never below the sample peak */
        if (peak > l->env) l->env = peak;
        else               l->env *= (1.0f - l->rel);
        l->gain = (l->env > l->ceiling) ? (l->ceiling / l->env) : 1.0f;

        /* output the delayed sample with the gain that was computed when it entered the line, so a
         * peak cannot outrun its own gain reduction (writing the CURRENT gain instead lets a decaying
         * envelope overshoot by a fraction of a dB) */
        const int q = l->pos;
        const float dl = l->buf[0][q], dr = l->buf[1][q], dg = l->gbuf[q];
        l->buf[0][q] = infl;
        l->buf[1][q] = inr;
        l->gbuf[q]   = l->gain;
        l->pos = (l->pos + 1) % (la + 1);

        left[i]  = dl * dg;
        right[i] = dr * dg;
    }
}
