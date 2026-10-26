/* dub_delay.c - true-stereo dub delay core (Milestone 1).
 *
 * Adapted from the vendored mda DubDelay DSP (third_party/mda/mdaDubDelay.cpp, MIT).
 * Deliberate deviations from the original (see PORTING_NOTES.md):
 *   - two independent delay lines (true stereo) instead of one mono line fed from the left input;
 *   - wet and dry are separate controls instead of mda's coupled FX Mix;
 *   - saturation limit/clip is a mode switch, decoupled from the feedback amount;
 *   - a DC blocker sits in each feedback loop, and state is NaN/Inf-sanitized.
 * Kept from mda: the 100-sample control-rate delay smoothing + LFO, the tone crossover,
 * the limiter/clipper behaviour, and the denormal handling.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "dub_delay.h"
#include "dub_filter.h"
#include "dsp_common.h"

#define DUB_MAX_SECONDS 8.0f
#define DUB_MAX_FEEDBACK 1.0f   /* the parameter IS the feedback: 0.95 = 95 % per repeat. See the note on
                                 * dub_delay_set_feedback() - it used to be scaled by 1.2 internally. */

struct dub_delay {
    float fs;
    int   size;                 /* max delay length in samples */
    int   ipos;
    int   primed;

    float *buf_l, *buf_r;

    /* control-rate delay time + LFO (shared by both channels) */
    float del;                  /* target delay in samples */
    float db;                   /* smoothed delay */
    float dl;                   /* current modulated delay */
    float ddl;                  /* per-sample linear step */
    float phi, dphi;
    float mod_depth;
    int   k;                    /* control-rate counter (mda: every 100 samples) */

    /* tone crossover (mda) */
    float fil, lmix, hmix;
    float f0_l, f0_r;

    /* feedback + saturation (mda) */
    float fbk, rel;
    float env_l, env_r;
    db_dcblock_t dc_l, dc_r;

    float wet, dry;

    int freeze, ping_pong;       /* Milestone 3 */

    struct dub_filter *filter;   /* optional, not owned; applied per channel in the loop */
};

static float clamp_ms(dub_delay_t *d, float ms) {
    float max_ms = (float)(d->size - 2) * 1000.0f / d->fs;
    return db_clampf(ms, 1.0f, max_ms);
}

dub_delay_t *dub_delay_create(float fs) {
    if (fs < 8000.0f) fs = 44100.0f;
    dub_delay_t *d = (dub_delay_t *)calloc(1, sizeof *d);
    if (!d) return NULL;
    d->fs = fs;
    d->size = (int)(DUB_MAX_SECONDS * fs) + 2;
    d->buf_l = (float *)calloc((size_t)d->size + 1, sizeof(float));
    d->buf_r = (float *)calloc((size_t)d->size + 1, sizeof(float));
    if (!d->buf_l || !d->buf_r) { dub_delay_destroy(d); return NULL; }

    d->fbk = 0.55f;
    d->rel = 0.9997f;
    d->wet = 0.33f;
    d->dry = 1.0f;
    d->del = clamp_ms(d, 350.0f) * 0.001f * fs;
    d->db = d->dl = d->del;
    d->primed = 1;
    dub_delay_set_tone(d, 0.4f);
    dub_delay_set_mod_rate(d, 0.5f);
    d->mod_depth = 0.0f;
    db_dcblock_init(&d->dc_l, 10.0f, fs);
    db_dcblock_init(&d->dc_r, 10.0f, fs);
    return d;
}

void dub_delay_destroy(dub_delay_t *d) {
    if (!d) return;
    free(d->buf_l);
    free(d->buf_r);
    free(d);
}

void dub_delay_reset(dub_delay_t *d) {
    if (!d) return;
    memset(d->buf_l, 0, ((size_t)d->size + 1) * sizeof(float));
    memset(d->buf_r, 0, ((size_t)d->size + 1) * sizeof(float));
    d->ipos = 0; d->k = 0; d->phi = 0.0f;
    d->f0_l = d->f0_r = 0.0f;
    d->env_l = d->env_r = 0.0f;
    d->db = d->dl = d->del;           /* re-prime: no start-up glide */
    db_dcblock_reset(&d->dc_l);
    db_dcblock_reset(&d->dc_r);
}

void dub_delay_set_time_ms(dub_delay_t *d, float ms) {
    if (!d) return;
    d->del = clamp_ms(d, ms) * 0.001f * d->fs;
    if (!d->primed) { d->db = d->dl = d->del; d->primed = 1; }
}

/* The parameter is the real feedback gain: 0.95 means 95 % per repeat. It used to be multiplied by
 * DUB_MAX_FEEDBACK = 1.2 ("so 1.0 can self-oscillate"), which made every *measured* percent value too
 * hot: the VERB/FADE macros at 0.95 became 1.14, so the delay swelled to its internal limiter instead of
 * decaying - heard as "Fade to Grey only makes the echo louder" on the device, 2026-10-16. Unity is still
 * reachable, so the self-oscillation that scale existed for remains: at 1.0 the loop sustains at the
 * envelope limiter. */
void dub_delay_set_feedback(dub_delay_t *d, float v) {
    if (!d) return;
    d->fbk = db_clampf(v, 0.0f, 1.0f) * DUB_MAX_FEEDBACK;
}

void dub_delay_set_tone(dub_delay_t *d, float t) {
    if (!d) return;
    float fil = t, lmix, hmix;
    if (t > 0.5f) { fil = 0.5f * fil - 0.25f; lmix = -2.0f * fil; hmix = 1.0f; }
    else          { hmix = 2.0f * fil;        lmix = 1.0f - hmix; }
    d->fil  = expf(-6.2831853f * powf(10.0f, 2.2f + 4.5f * fil) / d->fs);
    d->lmix = lmix;
    d->hmix = hmix;
}

void dub_delay_set_mod_rate(dub_delay_t *d, float v) {
    if (!d) return;
    d->dphi = 628.31853f * powf(10.0f, 3.0f * v - 2.0f) / d->fs;
}

void dub_delay_set_mod_depth(dub_delay_t *d, float v) {
    if (!d) return;
    d->mod_depth = db_clampf(v, 0.0f, 1.0f);
}

void dub_delay_set_saturation(dub_delay_t *d, int hard) {
    if (!d) return;
    d->rel = hard ? 0.8f : 0.9997f;
}

void dub_delay_set_wet(dub_delay_t *d, float v) { if (d) d->wet = db_clampf(v, 0.0f, 1.5f); }
void dub_delay_set_dry(dub_delay_t *d, float v) { if (d) d->dry = db_clampf(v, 0.0f, 1.5f); }
void dub_delay_set_filter(dub_delay_t *d, struct dub_filter *f) { if (d) d->filter = f; }
void dub_delay_set_freeze(dub_delay_t *d, int on) { if (d) d->freeze = on ? 1 : 0; }
void dub_delay_set_ping_pong(dub_delay_t *d, int on) { if (d) d->ping_pong = on ? 1 : 0; }

void dub_delay_process(dub_delay_t *d,
                       const float *in_l, const float *in_r,
                       float *out_l, float *out_r, int n) {
    const int   s = d->size;
    const float f = d->fil, lx = d->lmix, hx = d->hmix;
    const float fb = d->fbk, r = d->rel, w = d->wet, y = d->dry;
    const float twopi = 6.2831853f;
    int   i = d->ipos, k = d->k;
    float dl = d->dl, dbsm = d->db, dphi = d->dphi, phi = d->phi;
    float depth = d->mod_depth, del = d->del;
    float f0l = d->f0_l, f0r = d->f0_r;
    float el = d->env_l, er = d->env_r;

    for (int idx = 0; idx < n; ++idx) {
        float a = db_sanitize(in_l[idx]);
        float b = db_sanitize(in_r[idx]);

        if (k == 0) {   /* mda: update delay length + LFO every 100 samples */
            float mod = 0.049f * depth * del;
            dbsm += 0.01f * (del - dbsm - mod - mod * sinf(phi));
            d->ddl = 0.01f * (dbsm - dl);
            phi += dphi; if (phi > twopi) phi -= twopi;
            k = 100;
        }
        k--;
        dl += d->ddl;

        i--; if (i < 0) i = s;

        if (dl < 1.0f) dl = 1.0f;
        if (dl > (float)(s - 2)) dl = (float)(s - 2);
        int   li = (int)dl;
        float frac = dl - (float)li;
        int   rl = li + i; if (rl > s) rl -= (s + 1);
        int   rl2 = rl + 1; if (rl2 > s) rl2 = 0;
        float oll = d->buf_l[rl]; oll += frac * (d->buf_l[rl2] - oll);
        float olr = d->buf_r[rl]; olr += frac * (d->buf_r[rl2] - olr);

        /* feedback (per channel; ping-pong cross-feeds, freeze mutes new input) */
        float fl = fb * (d->ping_pong ? olr : oll);
        float fr = fb * (d->ping_pong ? oll : olr);
        float tl = (d->freeze ? 0.0f : a) + fl;
        float tr = (d->freeze ? 0.0f : b) + fr;

        /* mda tone crossover */
        f0l = f * (f0l - tl) + tl; tl = lx * f0l + hx * tl;
        f0r = f * (f0r - tr) + tr; tr = lx * f0r + hx * tr;

        /* mda limiter / clipper */
        float gl = fabsf(tl); el *= r; if (gl > el) el = gl; if (el > 1.0f) tl /= el;
        float gr = fabsf(tr); er *= r; if (gr > er) er = gr; if (er > 1.0f) tr /= er;

        /* Dub Filter sits after the mda tone/limiter, before the DC blocker */
        if (d->filter) {
            tl = dub_filter_tick(d->filter, 0, tl);
            tr = dub_filter_tick(d->filter, 1, tr);
        }

        /* DC blocker + sanitize in the loop, then store */
        tl = db_dcblock(&d->dc_l, tl);
        tr = db_dcblock(&d->dc_r, tr);
        d->buf_l[i] = tl;
        d->buf_r[i] = tr;

        out_l[idx] = y * a + w * oll;
        out_r[idx] = y * b + w * olr;
    }

    d->ipos = i; d->k = k; d->dl = dl; d->db = dbsm; d->phi = phi;
    d->f0_l = f0l; d->f0_r = f0r; d->env_l = el; d->env_r = er;

    /* keep recursive state out of denormals when idle */
    if (fabsf(d->f0_l) < 1.0e-12f) d->f0_l = 0.0f;
    if (fabsf(d->f0_r) < 1.0e-12f) d->f0_r = 0.0f;
    if (fabsf(d->env_l) < 1.0e-12f) d->env_l = 0.0f;
    if (fabsf(d->env_r) < 1.0e-12f) d->env_r = 0.0f;
}
