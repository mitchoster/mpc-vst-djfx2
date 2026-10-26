/* dub_filter.c - resonant feedback filter (Milestone 1).
 *
 * HPF -> LPF -> Drive -> soft clip, per channel (true stereo). RBJ cookbook
 * biquads (transposed direct form II). Cutoff/resonance are smoothed
 * and coefficients are recomputed only at a control rate when a smoothed value
 * moves, so parameter changes do not step abruptly. Bypass skips the DSP.
 *
 * "HP only" mode (dub_filter_set_hp_only) runs the HPF alone - no LPF, no drive, no ceiling - for
 * callers that want a plain resonant high-pass. DjFx2 uses it for the VERB macro's wet-path HPF.
 */
#include <stdlib.h>
#include <math.h>
#include "dub_filter.h"
#include "dsp_common.h"

#define DUB_FILTER_TWO_PI 6.28318531f
#define DUB_FILTER_CTRL   32          /* samples between coefficient updates */
#define DUB_FILTER_Q_MIN  0.5f
#define DUB_FILTER_Q_MAX  2.5f

typedef struct { float b0, b1, b2, a1, a2; float s1, s2; } dub_biquad_t;

struct dub_filter {
    float fs;
    int   enable;
    int   ctrl;
    int   hp_only;                /* 1 = HPF only (skip the LPF biquad); see dub_filter_set_hp_only */

    /* target and smoothed controls */
    float hpf_hz, lpf_hz, res;
    float sm_hpf, sm_lpf, sm_res;
    float ap_hpf, ap_lpf, ap_res;   /* last applied (coefficient set) */
    float sm_coef;

    float drive, gain, inv_gain, drive_lo, drive_hi;   /* drive 0..1 maps into [drive_lo, drive_hi] gain */
    float limit, ceil;       /* feedback limiter ceiling (Milestone 3) */

    dub_biquad_t hp[2], lp[2];
};

static void bq_lowpass(dub_biquad_t *b, float f0, float q, float fs) {
    float w0 = DUB_FILTER_TWO_PI * f0 / fs;
    float cw = cosf(w0), sw = sinf(w0), alpha = sw / (2.0f * q), a0 = 1.0f + alpha;
    float b0 = (1.0f - cw) * 0.5f;
    b->b0 = b0 / a0; b->b1 = (1.0f - cw) / a0; b->b2 = b->b0;
    b->a1 = (-2.0f * cw) / a0; b->a2 = (1.0f - alpha) / a0;
}

static void bq_highpass(dub_biquad_t *b, float f0, float q, float fs) {
    float w0 = DUB_FILTER_TWO_PI * f0 / fs;
    float cw = cosf(w0), sw = sinf(w0), alpha = sw / (2.0f * q), a0 = 1.0f + alpha;
    float b0 = (1.0f + cw) * 0.5f;
    b->b0 = b0 / a0; b->b1 = (-(1.0f + cw)) / a0; b->b2 = b->b0;
    b->a1 = (-2.0f * cw) / a0; b->a2 = (1.0f - alpha) / a0;
}

static float bq_tick(dub_biquad_t *b, float x) {
    float y = b->b0 * x + b->s1;
    b->s1 = b->b1 * x - b->a1 * y + b->s2;
    b->s2 = b->b2 * x - b->a2 * y;
    return y;
}

static void bq_reset(dub_biquad_t *b) { b->s1 = 0.0f; b->s2 = 0.0f; }

static float clamp_hz(dub_filter_t *f, float hz) {
    float hi = 0.45f * f->fs;
    return db_clampf(hz, 20.0f, hi);
}

static void refilter(dub_filter_t *f) {
    float q = db_clampf(DUB_FILTER_Q_MIN + (DUB_FILTER_Q_MAX - DUB_FILTER_Q_MIN) * f->sm_res,
                        DUB_FILTER_Q_MIN, DUB_FILTER_Q_MAX);
    bq_highpass(&f->hp[0], f->sm_hpf, q, f->fs);
    bq_highpass(&f->hp[1], f->sm_hpf, q, f->fs);
    bq_lowpass(&f->lp[0], f->sm_lpf, q, f->fs);
    bq_lowpass(&f->lp[1], f->sm_lpf, q, f->fs);
    f->ap_hpf = f->sm_hpf; f->ap_lpf = f->sm_lpf; f->ap_res = f->sm_res;
}

dub_filter_t *dub_filter_create(float fs) {
    if (fs < 8000.0f) fs = 44100.0f;
    dub_filter_t *f = (dub_filter_t *)calloc(1, sizeof *f);
    if (!f) return NULL;
    f->fs = fs;
    f->enable = 1;
    f->hpf_hz = 20.0f;
    f->lpf_hz = clamp_hz(f, 18000.0f);
    f->res = 0.2f;
    f->drive = 0.0f;
    f->drive_lo = 1.0f;
    f->drive_hi = 8.0f;
    f->gain = 1.0f;
    f->inv_gain = 1.0f;
    f->limit = 0.0f;
    f->ceil = 1.0f;
    f->sm_hpf = f->hpf_hz; f->sm_lpf = f->lpf_hz; f->sm_res = f->res;
    /* One-pole coefficient. Note the smoothing step runs once per DUB_FILTER_CTRL samples, so the
     * effective control time constant is ~0.008 s * DUB_FILTER_CTRL (~0.26 s), not 8 ms - measured
     * 2026-10-16 while testing the VERB high-pass (kept as is: it is the shipped DJ-filter glide). */
    f->sm_coef = 1.0f - expf(-1.0f / (0.008f * fs));
    f->ctrl = 1;
    refilter(f);
    return f;
}

void dub_filter_destroy(dub_filter_t *f) { free(f); }

void dub_filter_reset(dub_filter_t *f) {
    if (!f) return;
    bq_reset(&f->hp[0]); bq_reset(&f->hp[1]);
    bq_reset(&f->lp[0]); bq_reset(&f->lp[1]);
}

void dub_filter_set_enable(dub_filter_t *f, int on) { if (f) f->enable = on ? 1 : 0; }
void dub_filter_set_hpf(dub_filter_t *f, float hz) { if (f) f->hpf_hz = clamp_hz(f, hz); }
void dub_filter_set_lpf(dub_filter_t *f, float hz) { if (f) f->lpf_hz = clamp_hz(f, hz); }
void dub_filter_set_resonance(dub_filter_t *f, float v) { if (f) f->res = db_clampf(v, 0.0f, 1.0f); }
void dub_filter_set_hp_only(dub_filter_t *f, int on) { if (f) f->hp_only = on ? 1 : 0; }
void dub_filter_set_drive(dub_filter_t *f, float v) {
    if (!f) return;
    f->drive = db_clampf(v, 0.0f, 1.0f);
    f->gain = f->drive_lo + (f->drive_hi - f->drive_lo) * f->drive;
    f->inv_gain = powf(f->gain, -0.85f);   /* PARTIAL make-up: a mild push (gain^0.15) with little added loop gain */
}

void dub_filter_set_drive_range(dub_filter_t *f, float lo, float hi) {
    if (!f) return;
    f->drive_lo = lo;
    f->drive_hi = hi;
    f->gain = f->drive_lo + (f->drive_hi - f->drive_lo) * f->drive;
    f->inv_gain = powf(f->gain, -0.85f);
}

void dub_filter_set_limit(dub_filter_t *f, float v) {
    if (!f) return;
    f->limit = db_clampf(v, 0.0f, 1.0f);
    f->ceil = 1.0f - 0.7f * f->limit;   /* 0 -> 1.0 (soft clip only), 1 -> 0.3 (tight) */
}

float dub_filter_tick(dub_filter_t *f, int ch, float x) {
    if (!f->enable) return x;
    ch = ch ? 1 : 0;

    if (ch == 0 && --f->ctrl <= 0) {   /* control rate: smooth + maybe recompute */
        f->sm_hpf += f->sm_coef * (f->hpf_hz - f->sm_hpf);
        f->sm_lpf += f->sm_coef * (f->lpf_hz - f->sm_lpf);
        f->sm_res += f->sm_coef * (f->res - f->sm_res);
        if (fabsf(f->sm_hpf - f->ap_hpf) > 0.5f ||
            fabsf(f->sm_lpf - f->ap_lpf) > 0.5f ||
            fabsf(f->sm_res - f->ap_res) > 0.005f) {
            refilter(f);
        }
        f->ctrl = DUB_FILTER_CTRL;
    }

    x = db_sanitize(x);
    x = bq_tick(&f->hp[ch], x);
    if (f->hp_only) return x;   /* hp_only: plain resonant high-pass - no LPF, no drive, no ceiling */
    x = bq_tick(&f->lp[ch], x);
    x = db_soft_clip(x * f->gain) * f->inv_gain;   /* drive = saturation knee; gain-compensated so it does not raise the feedback loop gain */
    if (f->ceil < 0.999f) x = db_soft_clip(x / f->ceil) * f->ceil;   /* feedback ceiling */
    return x;
}

void dub_filter_process(dub_filter_t *f, float *l, float *r, int n) {
    for (int i = 0; i < n; ++i) {
        l[i] = dub_filter_tick(f, 0, l[i]);
        r[i] = dub_filter_tick(f, 1, r[i]);
    }
}
