/* dub_filter.h - resonant feedback filter for MPC Dub Station (Milestone 1).
 *
 * Serial per-channel chain: HPF -> LPF -> Drive -> soft clip, intended for the
 * delay's feedback path. Coefficients are recomputed only when a (smoothed)
 * parameter actually changes, never per sample. Allocation-free processing.
 */
#ifndef DUB_FILTER_H
#define DUB_FILTER_H

typedef struct dub_filter dub_filter_t;

dub_filter_t *dub_filter_create(float fs);
void dub_filter_destroy(dub_filter_t *f);

/* Clears all filter state (predictable reset). */
void dub_filter_reset(dub_filter_t *f);

void dub_filter_set_enable(dub_filter_t *f, int on);   /* 0 = bypass (skips DSP) */
void dub_filter_set_hpf(dub_filter_t *f, float hz);    /* clamped to [20, 0.45*fs] */
void dub_filter_set_lpf(dub_filter_t *f, float hz);    /* clamped to [20, 0.45*fs] */
void dub_filter_set_resonance(dub_filter_t *f, float v);/* 0..1 -> bounded Q */
/* Run the HPF alone: skip the LPF biquad, the drive stage and the ceiling, i.e. a plain resonant
 * high-pass. Used for the VERB macro's wet-path high-pass, where the original has no low-pass at all;
 * the shared resonance still shapes the HPF corner. */
void dub_filter_set_hp_only(dub_filter_t *f, int on);
void dub_filter_set_drive(dub_filter_t *f, float v);   /* 0..1 saturation knee within the range from set_drive_range (gain-compensated) */
void dub_filter_set_drive_range(dub_filter_t *f, float lo, float hi); /* gain span for the drive knob (e.g. 1..8) */
void dub_filter_set_limit(dub_filter_t *f, float v);   /* 0..1 feedback ceiling (0 = soft clip only) */

/* One sample for channel ch (0 or 1). Returns x unchanged when disabled. */
float dub_filter_tick(dub_filter_t *f, int ch, float x);

/* Magnitude response (HPF and LPF) at one frequency, in dB; 0 when disabled (used by the EQ curve). */
float dub_filter_response_db(const dub_filter_t *f, float hz);

/* In-place block helper (calls tick per channel). */
void dub_filter_process(dub_filter_t *f, float *l, float *r, int n);

#endif /* DUB_FILTER_H */
