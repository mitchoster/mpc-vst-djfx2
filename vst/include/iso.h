/* iso.h - MPC DJ FX 3-band isolator (own code).
 *
 * A DJ-style isolator: three bands (LOW / MID / HI) with two crossovers, split by two phase-matched
 * 4-pole (24 dB/oct) low-passes: low = LP(lo), mid = LP(hi) - LP(lo), high = x - LP(hi). The three
 * bands sum exactly back to the input at unity gains (0 dB is transparent). Each band has a gain in dB
 * from -24 (full kill: the band is muted) to +6. Allocation-free processing; 44.1 kHz stereo.
 */
#ifndef DJFX_ISO_H
#define DJFX_ISO_H

typedef struct iso iso_t;

iso_t *iso_create(float fs);
void iso_destroy(iso_t *s);
void iso_reset(iso_t *s);                 /* clears filter state */

void iso_set_lo_db(iso_t *s, float db);   /* -24 .. +6; -24 = kill */
void iso_set_mid_db(iso_t *s, float db);
void iso_set_hi_db(iso_t *s, float db);
void iso_set_lo_x(iso_t *s, float hz);    /* low/mid crossover */
void iso_set_hi_x(iso_t *s, float hz);    /* mid/high crossover */

void iso_set_sample_rate(iso_t *s, float fs);

/* In-place stereo block. */
void iso_process(iso_t *s, float *l, float *r, int n);

#endif /* DJFX_ISO_H */
