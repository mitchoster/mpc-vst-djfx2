/* limiter.h - MPC DJ FX look-ahead brickwall limiter (own code).
 *
 * A short look-ahead (2 ms) peak limiter: the gain is computed from the incoming peak and applied to
 * the delayed signal, so nothing overshoots the ceiling. Fast attack, slower release.
 */
#ifndef DJFX_LIMITER_H
#define DJFX_LIMITER_H

typedef struct limiter limiter_t;

limiter_t *limiter_create(float fs);
void limiter_destroy(limiter_t *l);
void limiter_reset(limiter_t *l);
void limiter_set_sample_rate(limiter_t *l, float fs);
void limiter_set_on(limiter_t *l, int on);          /* 0 = bypass */
void limiter_set_ceiling(limiter_t *l, float db);   /* output ceiling in dBFS (<= 0) */

void limiter_process(limiter_t *l, float *left, float *right, int n);

#endif /* DJFX_LIMITER_H */
