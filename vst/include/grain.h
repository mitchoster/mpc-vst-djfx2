/* grain.h - MPC DJ FX 2 grain delay (own code, MIT). One candidate for the original's "Glitch" stage.
 *
 * Models the two parameters the original actually maps (measured 2026-10-15):
 *   - `rate`  = grain emission frequency, 1 .. 150 Hz  (Ableton "Frequency")
 *   - `time`  = read offset into the capture buffer, 22 .. 2 ms (Ableton "Time Delay")
 * plus `spray` (random read-position jitter) and `mix` (dry/wet).
 *
 * A grain is a Hann-windowed slice of the capture buffer; grains are spawned every 1/rate seconds and
 * are two periods long, so consecutive grains overlap by ~50 % and the summed envelope stays smooth.
 * Each grain gets a small random detune and pan so the result reads as a grain cloud rather than a
 * chopped delay. Allocation happens in grain_create() only; the RNG is reseeded by grain_reset(), so a
 * reset instance is deterministic (the tests rely on that).
 */
#ifndef DJFX_GRAIN_H
#define DJFX_GRAIN_H

typedef struct grain grain_t;

grain_t *grain_create(float fs);
void grain_destroy(grain_t *g);
void grain_reset(grain_t *g);                  /* clears buffer + voices, reseeds the RNG */
void grain_set_sample_rate(grain_t *g, float fs);

void grain_set_rate(grain_t *g, float hz);     /* 1 .. 200 grains per second */
void grain_set_time(grain_t *g, float ms);     /* read offset into the buffer, 0.5 .. 500 ms */
void grain_set_spray(grain_t *g, float s);     /* 0..1 random position jitter */
void grain_set_mix(grain_t *g, float m);       /* 0..1 wet; 0 = sample-exact passthrough and the
                                                * whole stage is skipped (the neutral default costs
                                                * nothing) */

/* In-place stereo block. */
void grain_process(grain_t *g, float *l, float *r, int n);

/* --- diagnostics (tests / bench only) --- */
unsigned long grain_spawned(const grain_t *g);  /* grains started since the last reset */
int grain_active_voices(const grain_t *g);      /* currently ringing grains */

#endif /* DJFX_GRAIN_H */
