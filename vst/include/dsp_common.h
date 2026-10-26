/* dsp_common.h - allocation-free DSP helpers for MPC Dub Station.
 *
 * Header-only, C (and C++) compatible, no allocation, no libm beyond <math.h>.
 * Everything here is safe to call from the audio thread. Parametric helpers
 * (smoothing/DC block) are pure state machines: init once in create(), then
 * call per sample. Nothing here allocates or blocks.
 *
 * Unit test: vst/tests/dsp_common_test.c (run vst/tests/run.sh).
 */
#ifndef DUB_DSP_COMMON_H
#define DUB_DSP_COMMON_H

#include <math.h>
#include <stdint.h>

/* Tiny DC added to recursive state each sample so it never decays into denormals
 * (which cost hundreds of cycles per op on the target CPU). -80 dBFS-ish. */
#ifndef DB_ANTI_DENORM
#define DB_ANTI_DENORM 1.0e-20f
#endif

/* --- scalar helpers ------------------------------------------------------- */

static inline float db_clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline float db_lerp(float a, float b, float t) { /* t in [0,1] */
    return a + t * (b - a);
}

/* Flush NaN and +/-Inf to 0 so they can never propagate through feedback.
 * Audio is never anywhere near 1e30, so a finite value of that size is a bug. */
static inline float db_sanitize(float x) {
    return (x > -1.0e30f && x < 1.0e30f) ? x : 0.0f;   /* NaN and Inf both fail */
}

/* Flush subnormal magnitudes to 0. Use on non-recursive outputs; recursive state
 * should instead receive DB_ANTI_DENORM (adding a constant keeps the filter live). */
static inline float db_flush_denormal(float x) {
    return (fabsf(x) < 1.0e-24f) ? 0.0f : x;
}

/* --- saturation / safety -------------------------------------------------- */

/* Smooth, bounded soft clip: x / sqrt(1 + x^2). Monotonic, |y| < 1, and
 * y ~= x for |x| << 1 (transparent at low level, unlike x/(1+|x|)). */
static inline float db_soft_clip(float x) {
    return x / sqrtf(1.0f + x * x);
}

static inline float db_hard_clip(float x, float ceiling) {
    return db_clampf(x, -ceiling, ceiling);
}

/* --- parameter smoothing (one-pole toward a target) ----------------------- */

typedef struct { float cur; } db_smooth_t;

/* Per-sample coefficient for a time constant `ms` at sample rate `fs`:
 * coef = 1 - exp(-1/(tau_samples)); coef -> 1 (instant) for tiny tau. */
static inline float db_smooth_coef(float ms, float fs) {
    float tau = (ms * 0.001f) * fs;
    return tau <= 1.0f ? 1.0f : (1.0f - expf(-1.0f / tau));
}

static inline void db_smooth_init(db_smooth_t *s, float v) { s->cur = db_sanitize(v); }

static inline float db_smooth(db_smooth_t *s, float target, float coef) {
    s->cur += coef * (target - s->cur);
    s->cur = db_sanitize(s->cur);
    return s->cur;
}

/* --- linear ramp (e.g. delay-time glide in fixed per-block steps) --------- */

typedef struct { float cur, step; int left; } db_ramp_t;

static inline void db_ramp_init(db_ramp_t *r, float v) {
    r->cur = v; r->step = 0.0f; r->left = 0;
}

/* Begin a glide to `target` over `samples` (>=0). 0 = jump immediately. */
static inline void db_ramp_to(db_ramp_t *r, float target, int samples) {
    if (samples <= 0) { r->cur = target; r->step = 0.0f; r->left = 0; return; }
    r->step = (target - r->cur) / (float)samples;
    r->left = samples;
}

static inline float db_ramp_next(db_ramp_t *r) {
    if (r->left > 0) { r->cur += r->step; if (--r->left == 0) r->step = 0.0f; }
    return r->cur;
}

/* --- DC blocker (one-pole high-pass, in the feedback loop) ---------------- */

typedef struct { float x1, y1, r; } db_dcblock_t;

/* `cut_hz` typically 5-20 Hz. */
static inline void db_dcblock_init(db_dcblock_t *d, float cut_hz, float fs) {
    d->r = 1.0f - (6.2831853f * cut_hz / fs);
    if (d->r < 0.0f) d->r = 0.0f;
    d->x1 = 0.0f; d->y1 = 0.0f;
}

static inline void db_dcblock_reset(db_dcblock_t *d) { d->x1 = 0.0f; d->y1 = 0.0f; }

static inline float db_dcblock(db_dcblock_t *d, float x) {
    x = db_sanitize(x);
    float y = x - d->x1 + d->r * d->y1;
    d->x1 = x;
    d->y1 = y + DB_ANTI_DENORM;   /* keep the pole out of denormals */
    return y;
}

#endif /* DUB_DSP_COMMON_H */
