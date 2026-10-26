/* flanger.h - MPC DJ FX through-zero flanger (own module; DSP adapted from mda ThruZero, Paul Kellett,
 * MIT branch of the mda dual MIT/GPL-2.0 licence - see THIRD_PARTY_NOTICES.md and VENDORED.md).
 *
 * A modulated short delay subtracted from the dry signal (dry - wet), so the notches go through zero
 * at the modulation minimum - the classic "thru-zero" flange. Feedback can be positive or negative.
 */
#ifndef DJFX_FLANGER_H
#define DJFX_FLANGER_H

typedef struct flanger flanger_t;

flanger_t *flanger_create(float fs);
void flanger_destroy(flanger_t *f);
void flanger_reset(flanger_t *f);
void flanger_set_sample_rate(flanger_t *f, float fs);

void flanger_set_rate(flanger_t *f, float v);   /* 0..1 (log sweep rate; 0 stops the LFO) */
void flanger_set_depth(flanger_t *f, float v);  /* 0..1 -> up to ~45 ms of delay swing */
void flanger_set_fbk(flanger_t *f, float v);    /* 0..1 -> -0.95 .. +0.95 (0.5 = no feedback) */
void flanger_set_mix(flanger_t *f, float v);    /* 0..1: 0 = dry only */

void flanger_process(flanger_t *f, float *l, float *r, int n);

#endif /* DJFX_FLANGER_H */
