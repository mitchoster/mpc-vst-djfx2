/* dub_reverb.h - first-party plate/hall reverb for MPC Dub Station.
 *
 * A Dattorro-style feedback-delay-network reverb (Jon Dattorro, "Effect Design
 * Part 1: Reverberator and Other Filters", JAES 1997): a pre-delay, an input
 * bandwidth low-pass, four input all-passes, then a two-half tank with modulated
 * decay diffusors, damping low-passes and cross feedback. Own implementation;
 * the algorithm is the published Dattorro topology, no third-party code.
 *
 * Two voicings: PLATE (the canonical tank) and HALL (a longer tank -> bigger space).
 * Allocation-free processing after dub_reverb_create(); 44.1 kHz, in-place stereo.
 */
#ifndef DUB_REVERB_H
#define DUB_REVERB_H

typedef struct dub_reverb dub_reverb_t;

enum { DUB_REVERB_PLATE = 0, DUB_REVERB_HALL = 1 };

dub_reverb_t *dub_reverb_create(float fs);
void dub_reverb_destroy(dub_reverb_t *rv);
void dub_reverb_reset(dub_reverb_t *rv);          /* clears all delay lines + filters */

void dub_reverb_set_enable(dub_reverb_t *rv, int on);
void dub_reverb_set_mode(dub_reverb_t *rv, int mode);      /* DUB_REVERB_PLATE / _HALL */
void dub_reverb_set_amount(dub_reverb_t *rv, float v);     /* 0..1 wet level */
void dub_reverb_set_decay(dub_reverb_t *rv, float v);      /* 0..1 tank decay (short..long) */
void dub_reverb_set_predelay(dub_reverb_t *rv, float v);   /* 0..1 -> 0..100 ms */
void dub_reverb_set_damping(dub_reverb_t *rv, float v);    /* 0..1 (more = darker tail) */
void dub_reverb_set_hpf(dub_reverb_t *rv, float v);        /* 0..1 input high-pass */
void dub_reverb_set_lpf(dub_reverb_t *rv, float v);        /* 0..1 input bandwidth low-pass */

/* In-place stereo; leaves the input untouched when disabled. */
void dub_reverb_process(dub_reverb_t *rv, float *l, float *r, int n);

#endif /* DUB_REVERB_H */
