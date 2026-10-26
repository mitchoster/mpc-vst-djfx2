/* dub_delay.h - true-stereo dub delay core for MPC Dub Station (Milestone 1).
 *
 * Derived from mda DubDelay (MIT), adapted to two independent delay lines and
 * decoupled controls. All memory is allocated in create(); process() never
 * allocates, locks or blocks. See PORTING_NOTES.md for the deviations.
 */
#ifndef DUB_DELAY_H
#define DUB_DELAY_H

typedef struct dub_delay dub_delay_t;

/* Allocates the maximum delay memory. Returns NULL on failure. */
dub_delay_t *dub_delay_create(float fs);
void dub_delay_destroy(dub_delay_t *d);

/* Clears all delay/filter state and re-primes the delay time (no start-up glide). */
void dub_delay_reset(dub_delay_t *d);

/* Controls. time_ms is snapped on the first set/reset, then glides (mda behaviour). */
void dub_delay_set_time_ms(dub_delay_t *d, float ms);   /* 1 .. 8000 */
void dub_delay_set_feedback(dub_delay_t *d, float v);   /* 0 .. 1 (1.0 can self-oscillate) */
void dub_delay_set_tone(dub_delay_t *d, float v);       /* 0 .. 1, mda tone/crossover */
void dub_delay_set_mod_rate(dub_delay_t *d, float v);   /* 0 .. 1, mda LFO rate */
void dub_delay_set_mod_depth(dub_delay_t *d, float v);  /* 0 .. 1, mda LFO depth */
void dub_delay_set_saturation(dub_delay_t *d, int hard);/* 0 = soft limit, 1 = hard clip */
void dub_delay_set_wet(dub_delay_t *d, float v);        /* 0 .. 1 */
void dub_delay_set_dry(dub_delay_t *d, float v);        /* 0 .. 1 */

/* Optional resonant filter inserted in the feedback path (NULL = none). Not owned. */
struct dub_filter;
void dub_delay_set_filter(dub_delay_t *d, struct dub_filter *f);

/* Milestone 3 delay-core features. */
void dub_delay_set_freeze(dub_delay_t *d, int on);      /* freeze/infinite repeat (input muted, buffer recirculates) */
void dub_delay_set_ping_pong(dub_delay_t *d, int on);   /* cross-feed the feedback between channels */

void dub_delay_process(dub_delay_t *d,
                       const float *in_l, const float *in_r,
                       float *out_l, float *out_r, int n);

#endif /* DUB_DELAY_H */
