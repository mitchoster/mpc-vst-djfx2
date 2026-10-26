/* repeat.c - MPC DJ FX beat repeat (own code; the REC/PLAY idea is WrongPeople/JustRepeat, MIT,
 * see vst/third_party/justrepeat and vst/src/VENDORED.md; this implementation adds the grid enum,
 * gate, chance, repeat count and mix, sizes the buffer to the bar, and fades the slice edges).
 *
 * Per 4/4 bar (barTime = 240 / bpm seconds) the module:
 *   - rolls `chance` once (seeded RNG -> deterministic after reset); a miss passes the bar through;
 *   - captures the first slice (barTime / grid) while passing the input through;
 *   - replays that slice `count` times, looping it (sample-exact) with a short fade at each slice edge;
 *   - passes the input through for the rest of the bar.
 * `gate` is the played fraction of every slice; `mix` blends repeats against the dry signal.
 * `count` 0 = **hold**: the slice loops for the whole bar instead of `count` times - a tempo-locked roll
 * (what the owner's stutter wants), where a count would drop out after a few repeats.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "repeat.h"

#define REP_MAX_SECONDS 5.0f
#define REP_FADE 64            /* ~1.5 ms at 44.1 kHz */

/* grid divisions of a bar: 1/2, 1/4, 1/8, 1/8T, 1/6, 1/16, 1/32 -> 2, 4, 8, 12, 6, 16, 32
 * (1/6 is the sextuplet grid the original Auto-Fill uses) */
static const int DIVS[REP_MODES] = { 1, 2, 4, 8, 12, 6, 16, 32 };

struct repeat {
    float fs, bpm;
    int   mode;
    float gate, chance, mix;
    int   count;

    float *buf[2];
    int    cap;                    /* buffer capacity in samples */

    int    recSamples;             /* one bar */
    int    playSamples;            /* one slice */
    int    barPos;                 /* position in the bar */
    int    engaged;                /* this bar fired (chance) */
    unsigned rng;
};

static float clampf01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static unsigned rng_next(unsigned *s)
{
    unsigned x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x ? x : 0x1234567u;
    return *s;
}

static void lock_lengths(repeat_t *r)
{
    const float bar_sec = (r->bpm > 1.0f) ? (240.0f / r->bpm) : 2.0f;   /* 120 bpm default */
    int div = (r->mode >= REP_OFF && r->mode < REP_MODES) ? DIVS[r->mode] : 1;
    if (div < 1) div = 1;
    r->recSamples = (int)(bar_sec * r->fs);
    r->playSamples = (int)(bar_sec * r->fs / (float)div);
    if (r->recSamples > r->cap) r->recSamples = r->cap;
    if (r->playSamples < 1) r->playSamples = 1;
    if (r->playSamples > r->recSamples) r->playSamples = r->recSamples;
}

static void realloc_buf(repeat_t *r, float fs)
{
    const int cap = (int)(fs * REP_MAX_SECONDS);
    for (int ch = 0; ch < 2; ch++) {
        free(r->buf[ch]);
        r->buf[ch] = (float *)calloc((size_t)cap, sizeof(float));
    }
    r->cap = cap;
    r->fs = fs;
}

repeat_t *repeat_create(float fs)
{
    repeat_t *r = (repeat_t *)calloc(1, sizeof *r);
    if (!r) return NULL;
    r->fs = fs > 0.0f ? fs : 44100.0f;
    r->bpm = 120.0f;
    r->mode = REP_OFF;
    r->gate = 1.0f;
    r->chance = 1.0f;
    r->mix = 1.0f;
    r->count = 4;
    r->rng = 0x9e3779b9u;
    realloc_buf(r, r->fs);
    if (!r->buf[0] || !r->buf[1]) { repeat_destroy(r); return NULL; }
    lock_lengths(r);
    return r;
}

void repeat_destroy(repeat_t *r)
{
    if (!r) return;
    free(r->buf[0]);
    free(r->buf[1]);
    free(r);
}

void repeat_reset(repeat_t *r)
{
    if (!r) return;
    for (int ch = 0; ch < 2; ch++)
        if (r->buf[ch]) memset(r->buf[ch], 0, (size_t)r->cap * sizeof(float));
    r->barPos = 0;
    r->engaged = 0;
    r->rng = 0x9e3779b9u;
}

void repeat_set_sample_rate(repeat_t *r, float fs)
{
    if (!r || fs <= 0.0f || fabsf(fs - r->fs) < 0.5f) return;
    realloc_buf(r, fs);
    lock_lengths(r);
    repeat_reset(r);
}

void repeat_set_bpm(repeat_t *r, float bpm)
{
    if (!r || bpm <= 1.0f) return;
    r->bpm = bpm;
    lock_lengths(r);
}

void repeat_set_mode(repeat_t *r, int mode)
{
    if (!r) return;
    if (mode < REP_OFF || mode >= REP_MODES) mode = REP_OFF;
    if (mode != r->mode) {
        r->mode = mode;
        lock_lengths(r);
        r->barPos = 0;
        r->engaged = 0;
    }
}

void repeat_set_gate(repeat_t *r, float g)   { if (r) r->gate = clampf01(g); }
void repeat_set_chance(repeat_t *r, float c) { if (r) r->chance = clampf01(c); }
void repeat_set_mix(repeat_t *r, float m)    { if (r) r->mix = clampf01(m); }
void repeat_set_count(repeat_t *r, int n)    { if (r) r->count = (n < 0) ? 0 : (n > 8 ? 8 : n); }   /* 0 = hold */

static inline float slice_gain(int playPos, int window)
{
    /* short fade in/out inside the played window so the loop and the gate edges do not click */
    int f = REP_FADE;
    if (f > window / 2) f = window / 2;
    if (f <= 0) return 1.0f;
    if (playPos < f) return (float)playPos / (float)f;
    if (playPos >= window - f) return (float)(window - 1 - playPos) / (float)f;
    return 1.0f;
}

void repeat_process(repeat_t *r, float *l, float *rr, int n)
{
    if (!r || n <= 0) return;
    if (r->mode == REP_OFF || r->recSamples < 4 || r->playSamples < 2) return;   /* transparent */

    const int window = (int)((float)r->playSamples * r->gate);

    for (int i = 0; i < n; i++) {
        if (r->barPos == 0) {                       /* a new bar: decide once whether it fires */
            r->engaged = ((float)(rng_next(&r->rng) >> 8) / 16777216.0f) < r->chance;
        }
        const float inL = l[i], inR = rr[i];

        if (!r->engaged || r->barPos >= r->recSamples) {
            if (r->barPos >= r->recSamples) r->barPos = 0;
            /* pass through; advance the bar clock */
            r->barPos++;
            continue;
        }

        const int captured  = r->barPos < r->playSamples;
        const int in_repeat = !captured && (r->count == 0 || r->barPos < (1 + r->count) * r->playSamples);

        if (captured) {
            r->buf[0][r->barPos] = inL;             /* capture the first slice */
            r->buf[1][r->barPos] = inR;
            /* the input passes through while capturing */
        } else if (in_repeat) {
            const int playPos = (r->barPos - r->playSamples) % r->playSamples;
            float wetL = 0.0f, wetR = 0.0f;
            if (playPos < window) {
                const float g = slice_gain(playPos, window);
                wetL = r->buf[0][playPos] * g;
                wetR = r->buf[1][playPos] * g;
            }
            const float dry = 1.0f - r->mix;
            l[i]  = inL * dry + wetL * r->mix;
            rr[i] = inR * dry + wetR * r->mix;
        }
        /* outside the repeat window (and after the repeats) the input passes through unchanged */

        r->barPos++;
        if (r->barPos >= r->recSamples) r->barPos = 0;
    }
}
