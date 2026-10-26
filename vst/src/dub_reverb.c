/* dub_reverb.c - first-party Dattorro-style plate/hall reverb for MPC Dub Station.
 *
 * Topology (Jon Dattorro, "Effect Design Part 1", JAES 1997, public algorithm):
 *   in -> predelay -> bandwidth LPF -> input HPF
 *      -> 4 input all-passes (g 0.75, 0.75, 0.625, 0.625)
 *      -> tank, two halves; each half:
 *           + cross feedback (other half's post-damping delay * decay)
 *           -> modulated decay diffusor 1 (all-pass, -0.70)
 *           -> pre-damping delay -> damping LPF -> *decay
 *           -> decay diffusor 2 (all-pass)
 *           -> post-damping delay
 *   -> summed multi-tap stereo output.
 * All delay lengths are the paper's, scaled from its 29761 Hz reference to the
 * host rate. PLATE uses them as-is; HALL scales the tank 1.7x for a bigger space.
 *
 * Stereo input is summed to mono (standard for a reverb); output is stereo and the
 * processor returns dry + amount*wet, like the Airwindows Space it replaces.
 */
#include "dub_reverb.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dsp_common.h"

#define RV_REF_FS   29761.0f    /* Dattorro's reference rate: lengths are in samples there */
#define RV_HALL     1.7f        /* tank length factor for the HALL voicing */
#define RV_EXC      12.0f       /* modulation excursion, in reference samples */
#define RV_LFO_HZ   0.6f        /* decay-diffusor modulation rate */

typedef struct { float *b; int size, mask, w; } rv_line_t;

struct dub_reverb {
    float fs, k;                 /* k = fs / RV_REF_FS */
    int   enable, mode;
    float amount, decay, damping;
    float predelay_norm, hpf_n, lpf_n;

    /* derived coefficients (recomputed in the setters) */
    int   pd_len;                /* pre-delay in samples */
    float in_lp_c, in_hp_c;      /* one-pole coefficients */
    float damp_c;                /* tank damping one-pole coefficient */
    float exc;                   /* excursion in samples */

    /* input filters + tank damping states (mono in, per tank half) */
    float in_lp, in_hp;
    float damp[2];
    float lfo_phase, lfo_inc;

    rv_line_t pre;
    rv_line_t ind[4];
    rv_line_t dd1[2];
    rv_line_t preD[2];
    rv_line_t dd2[2];
    rv_line_t postD[2];
};

/* ---------------------------------------------------------------- delay primitives */

static int line_init(rv_line_t *l, int maxlen) {
    int size = 1;
    while (size < maxlen + 4) size <<= 1;
    l->b = (float *)calloc((size_t)size, sizeof(float));
    if (!l->b) return 0;
    l->size = size;
    l->mask = size - 1;
    l->w = 0;
    return 1;
}

static void line_free(rv_line_t *l) { free(l->b); l->b = NULL; }
static void line_clear(rv_line_t *l) { if (l->b) memset(l->b, 0, (size_t)l->size * sizeof(float)); l->w = 0; }

static inline int   l_len(const dub_reverb_t *v, float base) { return (int)lroundf(base * v->k * (v->mode == DUB_REVERB_HALL ? RV_HALL : 1.0f)); }
static inline float l_read(const rv_line_t *l, int off) { return l->b[(l->w - off) & l->mask]; }
static inline void  l_write(rv_line_t *l, float x) { l->b[l->w] = x; l->w = (l->w + 1) & l->mask; }

static inline float l_delay(rv_line_t *l, int off, float x) { float d = l_read(l, off); l_write(l, x); return d; }
static inline float l_allpass(rv_line_t *l, int off, float g, float x) {
    float d = l_read(l, off);
    float v = x - g * d;
    l_write(l, v);
    return d + g * v;
}

/* ---------------------------------------------------------------- create / destroy */

dub_reverb_t *dub_reverb_create(float fs) {
    dub_reverb_t *v = (dub_reverb_t *)calloc(1, sizeof *v);
    if (!v) return NULL;
    v->fs = fs > 1000.0f ? fs : 44100.0f;
    v->k = v->fs / RV_REF_FS;
    v->enable = 0;
    v->mode = DUB_REVERB_PLATE;
    v->amount = 0.3f;
    v->decay = 0.5f;
    v->damping = 0.4f;
    v->predelay_norm = 0.0f;
    v->hpf_n = 0.0f;
    v->lpf_n = 1.0f;

    /* largest possible lengths (hall) so buffers are big enough for both modes */
    const float hall = RV_HALL;
    int ok = 1;
    ok &= line_init(&v->pre, (int)(0.10f * v->fs * hall) + 8);
    static const float inDiff[4] = { 142, 107, 379, 277 };
    for (int i = 0; i < 4; i++) ok &= line_init(&v->ind[i], (int)ceilf(inDiff[i] * v->k * hall) + 8);
    static const float dd1[2]  = { 672, 908 };
    static const float preD[2] = { 4453, 4217 };
    static const float dd2[2]  = { 1800, 2656 };
    static const float postD[2]= { 3720, 3163 };
    for (int i = 0; i < 2; i++) {
        ok &= line_init(&v->dd1[i],   (int)ceilf(dd1[i]  * v->k * hall) + (int)(RV_EXC * v->k * hall) + 8);
        ok &= line_init(&v->preD[i],  (int)ceilf(preD[i] * v->k * hall) + 8);
        ok &= line_init(&v->dd2[i],   (int)ceilf(dd2[i]  * v->k * hall) + 8);
        ok &= line_init(&v->postD[i], (int)ceilf(postD[i]* v->k * hall) + 8);
    }
    if (!ok) { dub_reverb_destroy(v); return NULL; }

    v->exc = RV_EXC * v->k;
    v->lfo_inc = RV_LFO_HZ / v->fs;
    dub_reverb_set_predelay(v, v->predelay_norm);
    dub_reverb_set_hpf(v, v->hpf_n);
    dub_reverb_set_lpf(v, v->lpf_n);
    dub_reverb_set_damping(v, v->damping);
    dub_reverb_set_decay(v, v->decay);
    return v;
}

void dub_reverb_destroy(dub_reverb_t *v) {
    if (!v) return;
    line_free(&v->pre);
    for (int i = 0; i < 4; i++) line_free(&v->ind[i]);
    for (int i = 0; i < 2; i++) { line_free(&v->dd1[i]); line_free(&v->preD[i]); line_free(&v->dd2[i]); line_free(&v->postD[i]); }
    free(v);
}

void dub_reverb_reset(dub_reverb_t *v) {
    if (!v) return;
    line_clear(&v->pre);
    for (int i = 0; i < 4; i++) line_clear(&v->ind[i]);
    for (int i = 0; i < 2; i++) { line_clear(&v->dd1[i]); line_clear(&v->preD[i]); line_clear(&v->dd2[i]); line_clear(&v->postD[i]); }
    v->in_lp = v->in_hp = 0.0f;
    v->damp[0] = v->damp[1] = 0.0f;
    v->lfo_phase = 0.0f;
}

/* ---------------------------------------------------------------- setters */

void dub_reverb_set_enable(dub_reverb_t *v, int on) { if (v) v->enable = on ? 1 : 0; }
void dub_reverb_set_mode(dub_reverb_t *v, int mode) { if (v) v->mode = (mode == DUB_REVERB_HALL) ? DUB_REVERB_HALL : DUB_REVERB_PLATE; }
void dub_reverb_set_amount(dub_reverb_t *v, float a) { if (v) v->amount = db_clampf(a, 0.0f, 1.0f); }
void dub_reverb_set_decay(dub_reverb_t *v, float a) { if (v) v->decay = db_clampf(a, 0.0f, 1.0f); }

void dub_reverb_set_predelay(dub_reverb_t *v, float a) {
    if (!v) return;
    v->predelay_norm = db_clampf(a, 0.0f, 1.0f);
    v->pd_len = (int)(v->predelay_norm * 0.100f * v->fs);
}

void dub_reverb_set_damping(dub_reverb_t *v, float a) {
    if (!v) return;
    v->damping = db_clampf(a, 0.0f, 1.0f);
    v->damp_c = 1.0f - 0.9f * v->damping;      /* 0 -> no damping, 1 -> dark tail */
}

void dub_reverb_set_hpf(dub_reverb_t *v, float a) {
    if (!v) return;
    v->hpf_n = db_clampf(a, 0.0f, 1.0f);
    float fc = 20.0f + v->hpf_n * 1980.0f;
    v->in_hp_c = db_clampf(1.0f - expf(-6.2831853f * fc / v->fs), 0.0f, 1.0f);
}

void dub_reverb_set_lpf(dub_reverb_t *v, float a) {
    if (!v) return;
    v->lpf_n = db_clampf(a, 0.0f, 1.0f);
    float fc = 200.0f + v->lpf_n * 19800.0f;
    v->in_lp_c = db_clampf(1.0f - expf(-6.2831853f * fc / v->fs), 0.0f, 1.0f);
}

/* ---------------------------------------------------------------- process */

static inline void rv_sample(dub_reverb_t *v, float x, float *wl, float *wr) {
    /* pre-delay, then bandwidth LPF and input HPF */
    x = l_delay(&v->pre, v->pd_len > 0 ? v->pd_len : 1, x);
    v->in_lp += (x - v->in_lp) * v->in_lp_c;
    float y = v->in_lp;
    v->in_hp += (y - v->in_hp) * v->in_hp_c;
    y -= v->in_hp;

    /* input diffusion */
    static const float inDiff[4] = { 142, 107, 379, 277 };
    static const float inGain[4] = { 0.75f, 0.75f, 0.625f, 0.625f };
    for (int i = 0; i < 4; i++) y = l_allpass(&v->ind[i], l_len(v, inDiff[i]), inGain[i], y);

    float decay = v->decay;
    float dd2g = db_clampf(decay + 0.15f, 0.25f, 0.50f);

    v->lfo_phase += v->lfo_inc;
    if (v->lfo_phase >= 1.0f) v->lfo_phase -= 1.0f;
    int mod = (int)lroundf(v->exc * sinf(6.2831853f * v->lfo_phase));

    static const float dd1b[2]  = { 672, 908 };
    static const float preDb[2] = { 4453, 4217 };
    static const float dd2b[2]  = { 1800, 2656 };
    static const float postDb[2]= { 3720, 3163 };

    for (int i = 0; i < 2; i++) {
        float xf = y + l_read(&v->postD[i ^ 1], l_len(v, postDb[i ^ 1])) * decay;
        int off = l_len(v, dd1b[i]) + mod;
        if (off < 1) off = 1;
        if (off > v->dd1[i].mask) off = v->dd1[i].mask;
        xf = l_allpass(&v->dd1[i], off, -0.70f, xf);
        xf = l_delay(&v->preD[i], l_len(v, preDb[i]), xf);
        v->damp[i] += (xf - v->damp[i]) * v->damp_c;
        xf = v->damp[i] * decay;
        xf = l_allpass(&v->dd2[i], l_len(v, dd2b[i]), dd2g, xf);
        l_write(&v->postD[i], xf);
    }

    /* stereo output taps (Dattorro) */
    *wl = l_read(&v->preD[1], l_len(v, 266)) + l_read(&v->preD[1], l_len(v, 2974))
        - l_read(&v->dd2[1],  l_len(v, 1913)) + l_read(&v->postD[1], l_len(v, 1996))
        - l_read(&v->preD[0], l_len(v, 1990)) - l_read(&v->dd2[0],  l_len(v, 187))
        + l_read(&v->postD[0],l_len(v, 1066));
    *wr = l_read(&v->preD[0], l_len(v, 353)) + l_read(&v->preD[0], l_len(v, 3627))
        - l_read(&v->dd2[0],  l_len(v, 1228)) + l_read(&v->postD[0], l_len(v, 2673))
        - l_read(&v->preD[1], l_len(v, 2111)) - l_read(&v->dd2[1],  l_len(v, 335))
        + l_read(&v->postD[1],l_len(v, 121));
}

void dub_reverb_process(dub_reverb_t *v, float *l, float *r, int n) {
    if (!v || !v->enable || !l || !r || n <= 0) return;
    const float amt = v->amount;
    for (int i = 0; i < n; i++) {
        float wl, wr;
        rv_sample(v, db_sanitize(0.5f * (l[i] + r[i])), &wl, &wr);
        l[i] += amt * db_sanitize(wl);
        r[i] += amt * db_sanitize(wr);
    }
}
