/* repeat.h - MPC DJ FX beat repeat / stutter (own code, algorithm after WrongPeople/JustRepeat, MIT).
 *
 * Captures one slice of the incoming audio on a tempo-locked grid and replays it a fixed number of
 * times, then passes the input through until the next bar. `gate` shortens the played part of each
 * slice, `chance` decides per bar whether to fire at all, and `mix` blends the repeats against the dry
 * signal (mix 0 or grid Off = transparent). Short fades at the slice edges keep it click-free.
 *
 * The grid follows the host tempo (see repeat_set_bpm). Allocation happens in repeat_create() only.
 */
#ifndef DJFX_REPEAT_H
#define DJFX_REPEAT_H

typedef struct repeat repeat_t;

/* Grid divisions of a 4/4 bar; index 0 = Off. Keep in step with the rep_mode options in params.json. */
enum { REP_OFF = 0, REP_1_2, REP_1_4, REP_1_8, REP_1_8T, REP_1_6, REP_1_16, REP_1_32, REP_MODES };

repeat_t *repeat_create(float fs);
void repeat_destroy(repeat_t *r);
void repeat_reset(repeat_t *r);              /* clears buffers + state, reseeds the chance RNG */
void repeat_set_sample_rate(repeat_t *r, float fs);
void repeat_set_bpm(repeat_t *r, float bpm); /* host tempo; re-locks the bar/slice lengths */

void repeat_set_mode(repeat_t *r, int mode);   /* REP_OFF .. REP_1_32 */
void repeat_set_gate(repeat_t *r, float g);    /* 0..1: played fraction of each slice */
void repeat_set_chance(repeat_t *r, float c);  /* 0..1: probability a bar fires */
void repeat_set_count(repeat_t *r, int n);     /* 0 = hold the slice for the whole bar (stutter), 1..8 repeats */
void repeat_set_mix(repeat_t *r, float m);     /* 0..1: repeats vs dry */

/* In-place stereo block. */
void repeat_process(repeat_t *r, float *l, float *r_, int n);

#endif /* DJFX_REPEAT_H */
